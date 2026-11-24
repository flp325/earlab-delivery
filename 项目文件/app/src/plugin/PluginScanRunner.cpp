#include "PluginScanRunner.h"
#include "PluginHost.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cwchar>

#if JUCE_WINDOWS
 #include <csetjmp>
 #include <windows.h>

 #include <vector>
#elif JUCE_MAC
 #include <cerrno>
 #include <csignal>
 #include <crt_externs.h>
 #include <spawn.h>
 #include <sys/wait.h>
#endif

namespace earlab::plugin
{

namespace
{
#if JUCE_WINDOWS
/**
    用 Job Object 启动子进程。

    juce::ChildProcess 不暴露进程句柄，超时后 TerminateProcess 也管不住插件自己拉起的进程；
    Job Object 会把整棵进程树一起杀掉，这是插件扫描唯一可靠的做法。
*/
struct ScanProcess
{
    HANDLE process = nullptr;
    HANDLE job = nullptr;

    ~ScanProcess() { close(); }

    void close()
    {
        if (process != nullptr) { CloseHandle (process); process = nullptr; }
        if (job != nullptr)     { CloseHandle (job);     job = nullptr; }
    }

    bool start (const juce::StringArray& arguments)
    {
        job = CreateJobObjectW (nullptr, nullptr);

        if (job != nullptr)
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject (job, JobObjectExtendedLimitInformation, &limits, sizeof (limits));
        }

        juce::StringArray quoted;
        for (const auto& argument : arguments)
            quoted.add (argument.containsAnyOf (" \t") ? argument.quoted() : argument);
        const auto commandLine = quoted.joinIntoString (" ");
        const auto wide = commandLine.toWideCharPointer();
        std::vector<wchar_t> buffer (wide, wide + std::wcslen (wide) + 1);

        STARTUPINFOW startup {};
        startup.cb = sizeof (startup);
        PROCESS_INFORMATION info {};

        if (! CreateProcessW (nullptr, buffer.data(), nullptr, nullptr, FALSE,
                              CREATE_NO_WINDOW | CREATE_SUSPENDED,
                              nullptr, nullptr, &startup, &info))
            return false;

        // 进程已在别的 job 里时会失败，但不影响等待与强杀
        if (job != nullptr)
            AssignProcessToJobObject (job, info.hProcess);

        ResumeThread (info.hThread);
        CloseHandle (info.hThread);
        process = info.hProcess;
        return true;
    }

    enum class WaitState { exited, timeout, cancelled };

    WaitState wait (int timeoutMs, const std::function<bool()>& shouldCancel) const
    {
        if (process == nullptr)
            return WaitState::timeout;

        int remaining = timeoutMs;

        while (remaining > 0)
        {
            if (shouldCancel && shouldCancel())
                return WaitState::cancelled;

            const DWORD slice = static_cast<DWORD> (juce::jmin (100, remaining));

            switch (WaitForSingleObject (process, slice))
            {
                case WAIT_OBJECT_0: return WaitState::exited;
                case WAIT_FAILED:   return WaitState::timeout;
                default:            break;
            }

            remaining -= static_cast<int> (slice);
        }

        return WaitState::timeout;
    }

    void terminate() const
    {
        if (job != nullptr)
            TerminateJobObject (job, 1);

        if (process != nullptr)
            TerminateProcess (process, 1);
    }

    [[nodiscard]] juce::uint32 getExitCode() const
    {
        DWORD code = 0;

        if (process != nullptr)
            GetExitCodeProcess (process, &code);

        return static_cast<juce::uint32> (code);
    }
};
#elif JUCE_MAC
struct ScanProcess
{
    enum class WaitState { exited, timeout, cancelled };
    pid_t pid = -1;
    int exitCode = 1;

    bool start (const juce::StringArray& args)
    {
        std::vector<char*> argv;
        for (const auto& arg : args)
            argv.push_back (const_cast<char*> (arg.toRawUTF8()));
        argv.push_back (nullptr);

        posix_spawnattr_t attr;
        if (posix_spawnattr_init (&attr) != 0)
            return false;
        pid_t child = -1;
        const bool ok = posix_spawnattr_setpgroup (&attr, 0) == 0
                     && posix_spawnattr_setflags (&attr, POSIX_SPAWN_SETPGROUP) == 0
                     && posix_spawn (&child, argv.front(), nullptr, &attr, argv.data(), *_NSGetEnviron()) == 0;
        posix_spawnattr_destroy (&attr);
        if (ok)
            pid = child;
        return ok;
    }

    WaitState wait (int timeoutMs, const std::function<bool()>& shouldCancel)
    {
        const auto end = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
        for (;;)
        {
            if (shouldCancel && shouldCancel())
                return WaitState::cancelled;
            int status = 0;
            const auto done = waitpid (pid, &status, WNOHANG);
            if (done == pid || (done == -1 && errno != EINTR))
            {
                pid = -1;
                exitCode = done > 0 && WIFEXITED (status) ? WEXITSTATUS (status) : 1;
                return WaitState::exited;
            }
            if (juce::Time::getMillisecondCounterHiRes() >= end)
                return WaitState::timeout;
            juce::Thread::sleep (20);
        }
    }

    void terminate()
    {
        if (pid > 0)
        {
            kill (-pid, SIGKILL);
            while (waitpid (pid, nullptr, 0) == -1 && errno == EINTR) {}
            pid = -1;
        }
    }

    ~ScanProcess() { terminate(); }
    int getExitCode() const { return exitCode; }
};
#endif

#if JUCE_WINDOWS
thread_local jmp_buf* activeJumpTarget = nullptr;
thread_local bool crashedInProcess = false;

LONG CALLBACK vehDispatcher (EXCEPTION_POINTERS* info)
{
    if (activeJumpTarget == nullptr || info == nullptr || info->ExceptionRecord == nullptr)
        return EXCEPTION_CONTINUE_SEARCH;

    // 只接管硬崩溃；C++ 异常必须继续走正常展开
    switch (info->ExceptionRecord->ExceptionCode)
    {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            longjmp (*activeJumpTarget, 1);

        // 不接管 EXCEPTION_STACK_OVERFLOW：guard page 没有恢复，
        // longjmp 出去通常立刻二次溢出，反而更难诊断
        default:
            break;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

void installVehOnce()
{
    static const bool installed = []
    {
        AddVectoredExceptionHandler (1, vehDispatcher);
        return true;
    }();

    juce::ignoreUnused (installed);
}
#endif

juce::AudioPluginFormat* findVst3Format (juce::AudioPluginFormatManager& manager)
{
    for (auto* format : manager.getFormats())
        if (format->getName().equalsIgnoreCase ("VST3"))
            return format;

    return nullptr;
}

PluginInfo makeInfo (const juce::PluginDescription& description, const juce::String& path)
{
    PluginInfo info;
    info.name             = description.name;
    info.manufacturer     = description.manufacturerName;
    info.category         = description.category;
    info.version          = description.version;
    info.format           = description.pluginFormatName;
    info.identifier       = description.createIdentifierString();
    info.fileOrIdentifier = path;
    info.uniqueId         = description.uniqueId;
    info.numInputs        = description.numInputChannels;
    info.numOutputs       = description.numOutputChannels;
    info.isInstrument     = description.isInstrument;
    return info;
}
} // namespace

int PluginScanRunner::runScanChild (const juce::String& pluginPath, const juce::File& outFile)
{
    std::fprintf (stderr, "[scan-child] path=%s out=%s\n",
                  pluginPath.toRawUTF8(), outFile.getFullPathName().toRawUTF8());

    juce::AudioPluginFormatManager manager;
    juce::addHeadlessDefaultFormatsToManager (manager);

    auto* format = findVst3Format (manager);

    if (format == nullptr)
        return 2;

    juce::OwnedArray<juce::PluginDescription> descriptions;

    try
    {
        format->findAllTypesForFile (descriptions, pluginPath);
    }
    catch (...)
    {
        return 3;
    }

    std::vector<PluginInfo> plugins;

    for (const auto* description : descriptions)
        plugins.push_back (makeInfo (*description, pluginPath));

    std::fprintf (stderr, "[scan-child] descriptions=%d\n", descriptions.size());

    const bool wrote = writeResult (outFile, plugins);
    std::fprintf (stderr, "[scan-child] wrote=%d\n", wrote ? 1 : 0);

    return wrote ? 0 : 4;
}

int PluginScanRunner::runPreflightChild (const juce::String& pluginPath)
{
    std::fprintf (stderr, "[preflight-child] path=%s\n", pluginPath.toRawUTF8());

    juce::AudioPluginFormatManager manager;
    juce::addHeadlessDefaultFormatsToManager (manager);

    auto* format = findVst3Format (manager);

    if (format == nullptr)
        return 2;

    juce::OwnedArray<juce::PluginDescription> descriptions;

    try
    {
        format->findAllTypesForFile (descriptions, pluginPath);
    }
    catch (...)
    {
        return 3;
    }

    if (descriptions.isEmpty())
        return 4;

    // P1-3：与正式加载共用同一类型规则（第一个非音源，否则第一个），
    // 避免「预检 A 类型、加载 B 类型」。同一套配置常量见 PluginHost。
    std::vector<PluginInfo> described;

    for (const auto* description : descriptions)
        described.push_back (makeInfo (*description, pluginPath));

    const int chosenIndex = PluginHost::chooseLoadableIndex (described);

    if (chosenIndex < 0)
        return 4;

    auto* chosen = descriptions[chosenIndex];
    const auto& chosenInfo = described[static_cast<size_t> (chosenIndex)];

    juce::String error;
    auto instance = manager.createPluginInstance (*chosen, PluginHost::probeSampleRate,
                                                  PluginHost::probeBlockSize, error);

    if (instance == nullptr)
        return 5;

    // 与正式加载共用 prepareToPlay + 静音首帧 + 预热声道数（P0-1），
    // 并跑满一个 probeBlockSize 的块确认整块尺寸下输出有限。
    const bool finite = PluginHost::prepareAndWarmUp (*instance,
                                                      PluginHost::probeSampleRate,
                                                      PluginHost::probeBlockSize,
                                                      chosenInfo.numInputs,
                                                      chosenInfo.numOutputs,
                                                      PluginHost::probeBlockSize);

    instance->releaseResources();
    return finite ? 0 : 6;
}

PluginScanRunner::PreflightResult PluginScanRunner::preflightInChild (const juce::File& selfExe,
                                                                      const juce::String& pluginPath,
                                                                      int timeoutMs,
                                                                      std::function<bool()> shouldCancel)
{
    PreflightResult result;
    if (shouldCancel && shouldCancel())
    {
        result.cancelled = true;
        result.error = utf8 ("已取消插件预检");
        return result;
    }

   #if JUCE_WINDOWS || JUCE_MAC
    juce::StringArray command;
    command.add (selfExe.getFullPathName());
    command.add ("--vst3-preflight");
    command.add (pluginPath);

    ScanProcess process;

    if (! process.start (command))
    {
        result.unavailable = true;
        result.error = utf8 ("环境禁止创建预检子进程");
        return result;
    }

    const auto state = process.wait (timeoutMs, shouldCancel);
    if (state != ScanProcess::WaitState::exited)
    {
        process.terminate();
        result.cancelled = state == ScanProcess::WaitState::cancelled;
        result.timedOut = ! result.cancelled;
        result.error = result.cancelled ? utf8 ("已取消插件预检")
                                        : utf8 ("插件初始化超时（可能在等待授权/登录窗口）");
        return result;
    }

    const auto exitCode = static_cast<int> (process.getExitCode());

    if (exitCode == 0)
    {
        result.ok = true;
        return result;
    }

    result.crashed = true;
    result.error = utf8 ("插件初始化失败或崩溃（退出码 ") + juce::String (exitCode) + utf8 ("）");
    return result;
   #else
    result.unavailable = true;
    juce::ignoreUnused (selfExe, pluginPath, timeoutMs);
    return result;
   #endif
}

juce::File PluginScanRunner::scanTempDirectory()
{
    // 多线程扫描会并发调用，缓存一次，避免重复探测
    static const juce::File cached = []
    {
        const juce::File candidates[] =
        {
            juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("EarLabScan"),
            juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                .getChildFile ("EarLab").getChildFile ("scan"),
            juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                .getParentDirectory().getChildFile (".earlab-scan")
        };

        for (const auto& dir : candidates)
        {
            if (! dir.createDirectory().wasOk())
                continue;

            auto probe = dir.getChildFile ("write-probe.txt");

            if (probe.replaceWithText ("ok") && probe.existsAsFile())
            {
                probe.deleteFile();
                return dir;
            }
        }

        return juce::File::getSpecialLocation (juce::File::tempDirectory);
    }();

    return cached;
}

PluginScanRunner::ChildResult PluginScanRunner::scanInChild (const juce::File& selfExe,
                                                             const juce::String& pluginPath,
                                                             int timeoutMs,
                                                             std::function<bool()> shouldCancel)
{
    ChildResult result;

    static std::atomic<int> scanSerial { 0 };
    const auto temp = scanTempDirectory().getNonexistentChildFile (
        "scan-" + juce::String (scanSerial.fetch_add (1)) + "-", ".json", false);

    if (temp == juce::File())
    {
        result.error = utf8 ("无法创建临时文件");
        return result;
    }

    juce::StringArray command;
    command.add (selfExe.getFullPathName());
    command.add ("--vst3-scan-one");
    command.add (pluginPath);
    command.add ("--vst3-scan-out");
    command.add (temp.getFullPathName());

   #if JUCE_WINDOWS || JUCE_MAC
    ScanProcess process;

    // 不捕获 stdout/stderr：结果统一走临时 JSON 文件
    if (! process.start (command))
    {
        // P1-2：绝不退回主进程扫描——第三方插件崩溃会直接带走宿主。
        temp.deleteFile();
        result.error = utf8 ("环境禁止创建扫描子进程");
        return result;
    }

    const auto state = process.wait (timeoutMs, shouldCancel);

    if (state == ScanProcess::WaitState::cancelled)
    {
        process.terminate();   // 取消也要把整棵进程树收回
        result.cancelled = true;
        result.error = utf8 ("已取消");
        temp.deleteFile();
        return result;
    }

    if (state != ScanProcess::WaitState::exited)
    {
        process.terminate();   // Job Object：连插件自己拉起的进程一起杀
        result.timedOut = true;
        result.error = utf8 ("扫描超时");
        temp.deleteFile();
        return result;
    }

    const auto exitCode = process.getExitCode();
   #else
    juce::ChildProcess process;

    if (! process.start (command, 0))
    {
        // P1-2：绝不退回主进程扫描
        temp.deleteFile();
        result.error = utf8 ("环境禁止创建扫描子进程");
        return result;
    }

    bool finished = false;

    for (int remaining = timeoutMs; remaining > 0; )
    {
        if (shouldCancel && shouldCancel())
        {
            process.kill();
            result.cancelled = true;
            result.error = utf8 ("已取消");
            temp.deleteFile();
            return result;
        }

        const int slice = juce::jmin (100, remaining);

        if (process.waitForProcessToFinish (slice))
        {
            finished = true;
            break;
        }

        remaining -= slice;
    }

    if (! finished)
    {
        process.kill();
        result.timedOut = true;
        result.error = utf8 ("扫描超时");
        temp.deleteFile();
        return result;
    }

    const auto exitCode = process.getExitCode();
   #endif

    const bool readOk = readResult (temp, result.plugins);
    temp.deleteFile();

    if (! readOk)
    {
        result.crashed = true;
        result.error = utf8 ("扫描进程异常退出（退出码 ") + juce::String (exitCode) + utf8 ("）");
        return result;
    }

    return result;
}

juce::StringArray PluginScanRunner::enumerateBundles (const juce::StringArray& directories)
{
    juce::StringArray candidates;

    for (const auto& path : directories)
    {
        const juce::File dir (path);

        if (! dir.isDirectory())
            continue;

       #if JUCE_MAC
        constexpr auto bundleType = juce::File::findDirectories;
       #else
        constexpr auto bundleType = juce::File::findFiles;
       #endif
        for (const auto& entry : dir.findChildFiles (bundleType, true, "*.vst3"))
            if (! candidates.contains (entry.getFullPathName()))
                candidates.add (entry.getFullPathName());
    }

    candidates.sort (true);
    return candidates;
}

PluginScanRunner::ChildResult PluginScanRunner::scanInProcess (const juce::String& pluginPath)
{
    // 跨 longjmp 之后局部变量可能失效，结果放线程局部
    static thread_local ChildResult inproc;
    inproc = ChildResult();

   #if JUCE_WINDOWS
    installVehOnce();

    jmp_buf buffer;

    if (setjmp (buffer) != 0)
    {
        activeJumpTarget = nullptr;
        inproc.crashed = true;
        inproc.plugins.clear();
        inproc.error = utf8 ("插件触发访问违例（已隔离）");
        return inproc;
    }

    activeJumpTarget = &buffer;
   #endif

    juce::AudioPluginFormatManager manager;
    juce::addHeadlessDefaultFormatsToManager (manager);

    if (auto* format = findVst3Format (manager))
    {
        juce::OwnedArray<juce::PluginDescription> descriptions;

        try
        {
            format->findAllTypesForFile (descriptions, pluginPath);
        }
        catch (...)
        {
            inproc.error = utf8 ("加载异常");
        }

        for (const auto* description : descriptions)
            inproc.plugins.push_back (makeInfo (*description, pluginPath));
    }

   #if JUCE_WINDOWS
    activeJumpTarget = nullptr;
   #endif

    if (inproc.plugins.empty() && inproc.error.isEmpty())
        inproc.error = utf8 ("未找到插件类型");

    return inproc;
}

bool PluginScanRunner::writeResult (const juce::File& file, const std::vector<PluginInfo>& plugins)
{
    juce::Array<juce::var> entries;

    for (const auto& info : plugins)
        entries.add (info.toVar());

    auto* root = new juce::DynamicObject();
    root->setProperty ("plugins", entries);

    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (juce::var (root), false));
}

bool PluginScanRunner::readResult (const juce::File& file, std::vector<PluginInfo>& plugins)
{
    plugins.clear();

    if (! file.existsAsFile() || file.getSize() == 0)
        return false;

    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    auto* root = parsed.getDynamicObject();

    if (root == nullptr)
        return false;

    if (auto* entries = root->getProperty ("plugins").getArray())
        for (const auto& entry : *entries)
            plugins.push_back (PluginInfo::fromVar (entry));

    return true;
}

} // namespace earlab::plugin
