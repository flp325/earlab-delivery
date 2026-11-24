#include "PluginCatalog.h"

#include "PluginScanRunner.h"
#include "../util/AtomicFile.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>

namespace earlab::plugin
{

juce::var PluginInfo::toVar() const
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("name", name);
    object->setProperty ("manufacturer", manufacturer);
    object->setProperty ("category", category);
    object->setProperty ("version", version);
    object->setProperty ("format", format);
    object->setProperty ("identifier", identifier);
    object->setProperty ("file", fileOrIdentifier);
    object->setProperty ("uniqueId", uniqueId);
    object->setProperty ("inputs", numInputs);
    object->setProperty ("outputs", numOutputs);
    object->setProperty ("instrument", isInstrument);
    return juce::var (object);
}

PluginInfo PluginInfo::fromVar (const juce::var& value)
{
    PluginInfo info;

    if (auto* object = value.getDynamicObject())
    {
        info.name           = object->getProperty ("name").toString();
        info.manufacturer   = object->getProperty ("manufacturer").toString();
        info.category       = object->getProperty ("category").toString();
        info.version        = object->getProperty ("version").toString();
        info.format         = object->getProperty ("format").toString();
        info.identifier     = object->getProperty ("identifier").toString();
        info.fileOrIdentifier = object->getProperty ("file").toString();
        info.uniqueId       = static_cast<int> (object->getProperty ("uniqueId"));
        info.numInputs      = static_cast<int> (object->getProperty ("inputs"));
        info.numOutputs     = static_cast<int> (object->getProperty ("outputs"));
        info.isInstrument   = static_cast<bool> (object->getProperty ("instrument"));
    }

    return info;
}

juce::String PluginInfo::getDisplayName() const
{
    return manufacturer.isEmpty() ? name : manufacturer + " - " + name;
}

bool PluginInfo::isEffect() const noexcept
{
    return ! isInstrument && numInputs > 0 && numOutputs > 0;
}

PluginCatalog::PluginCatalog()
{
    // 只注册无头格式；JUCE_PLUGINHOST_VST3 打开时其中包含 VST3
    juce::addHeadlessDefaultFormatsToManager (formatManager);
}

juce::StringArray PluginCatalog::defaultVST3Directories()
{
    juce::StringArray dirs;

   #if JUCE_WINDOWS
    const auto common = juce::File ("C:\\Program Files\\Common Files\\VST3");
    // 用户级标准位置是 %LOCALAPPDATA%\Programs\Common\VST3，
    // 不是 %APPDATA%（Roaming）——少一层 Local 会永远扫不到这类安装。
    const auto localPrograms = juce::File::getSpecialLocation (juce::File::windowsLocalAppData)
                                   .getChildFile ("Programs")
                                   .getChildFile ("Common")
                                   .getChildFile ("VST3");
   #else
    const auto common = juce::File ("/Library/Audio/Plug-Ins/VST3");
    const auto localPrograms = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                   .getChildFile ("Library")
                                   .getChildFile ("Audio")
                                   .getChildFile ("Plug-Ins")
                                   .getChildFile ("VST3");
   #endif

    if (common.isDirectory())
        dirs.add (common.getFullPathName());

    if (localPrograms.isDirectory())
        dirs.add (localPrograms.getFullPathName());

    return dirs;
}

juce::File PluginCatalog::defaultCacheFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("EarLab")
        .getChildFile ("plugins.json");
}

bool PluginCatalog::shouldReplaceCatalog (int found, int failed, int skipped)
{
    // 有成功结果，或确实一个候选都没有 → 可以覆盖
    // 全失败 / 全 skipped → 保留旧缓存（可能是暂时性故障或黑名单）
    return found > 0 || (failed == 0 && skipped == 0);
}

juce::AudioPluginFormat* PluginCatalog::findVst3Format() const
{
    for (auto* format : formatManager.getFormats())
        if (format->getName().equalsIgnoreCase ("VST3"))
            return format;

    return nullptr;
}

PluginCatalog::ScanOutcome PluginCatalog::scanDetached (
    const juce::StringArray& directories,
    const std::function<void (int, int, const juce::String&)>& progress,
    int timeoutPerPluginMs,
    bool ignoreBlacklist,
    const std::function<bool()>& shouldCancel,
    int parallelWorkers) const
{
    ScanOutcome outcome;

    // U-P1-4：worker 线程只写这个局部快照，绝不碰 catalog 的成员容器
    ScanReport& report = outcome.report;

    if (findVst3Format() == nullptr)
    {
        report.failures.add (utf8 ("未注册 VST3 格式（JUCE_PLUGINHOST_VST3 是否打开？）"));
        return outcome;
    }

    const auto candidates = PluginScanRunner::enumerateBundles (directories);
    report.candidates = candidates.size();

    const auto selfExe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

    // 失败列表：忽略黑名单时清空重来；否则保留仍然存在的条目，扫描时直接跳过
    std::vector<PluginFailure> carried;

    if (! ignoreBlacklist)
        for (const auto& failure : failureList)
            if (juce::File (failure.file).exists())
                carried.push_back (failure);

    juce::StringArray blacklist;

    for (const auto& failure : carried)
        blacklist.addIfNotAlreadyThere (failure.file);

    const int total = candidates.size();
    const int workers = juce::jlimit (1, 8,
        parallelWorkers > 0 ? parallelWorkers
                            : juce::jmin (4, juce::jmax (1, juce::SystemStats::getNumCpus() / 2)));

    std::vector<PluginInfo> result;
    std::vector<PluginFailure> newFailures;
    juce::StringArray failureMessages;

    std::atomic<int> nextIndex { 0 }, completed { 0 }, skippedCount { 0 },
                     foundCount { 0 }, failedCount { 0 }, instrumentCount { 0 };
    std::atomic<bool> cancelled { false };
    std::mutex resultMutex, progressMutex;

    const auto reportProgress = [&] (const juce::String& current)
    {
        const std::lock_guard<std::mutex> lock (progressMutex);
        if (progress)
            progress (completed.load(), total, current);
    };

    const auto workerFn = [&]
    {
        for (;;)
        {
            if (shouldCancel && shouldCancel())
            {
                cancelled.store (true);
                return;
            }

            const int index = nextIndex.fetch_add (1);

            if (index >= total)
                return;

            const auto& path = candidates[static_cast<size_t> (index)];
            const auto name = juce::File (path).getFileName();

            if (blacklist.contains (path))
            {
                ++skippedCount;
                ++completed;
                reportProgress (name);
                continue;
            }

            reportProgress (name);

            // P1-2：只走子进程扫描；scanInProcess 不再作为产品回退。
            const auto child = PluginScanRunner::scanInChild (selfExe, path, timeoutPerPluginMs,
                                                              shouldCancel);

            if (child.cancelled)
            {
                cancelled.store (true);
                return;
            }

            {
                const std::lock_guard<std::mutex> lock (resultMutex);

                if (child.plugins.empty())
                {
                    const auto reason = child.error.isNotEmpty() ? child.error : utf8 ("未找到插件类型");
                    newFailures.push_back ({ path, reason });
                    failureMessages.add (name + utf8 ("：") + reason);
                    ++failedCount;
                }
                else
                {
                    for (auto info : child.plugins)
                    {
                        if (info.isInstrument)
                            ++instrumentCount;

                        result.push_back (std::move (info));
                    }

                    foundCount += static_cast<int> (child.plugins.size());
                }
            }

            ++completed;
            reportProgress ({});
        }
    };

    std::vector<std::thread> threads;
    threads.reserve (static_cast<size_t> (workers));

    for (int i = 0; i < workers; ++i)
        threads.emplace_back (workerFn);

    for (auto& thread : threads)
        thread.join();

    report.found = foundCount.load();
    report.failed = failedCount.load();
    report.skipped = skippedCount.load();
    report.instruments = instrumentCount.load();
    report.cancelled = cancelled.load();
    report.failures = failureMessages;

    // 取消：丢弃本次部分结果，保留原有目录（否则 UI 的旧行索引会越界）
    if (report.cancelled)
    {
        if (progress)
            progress (completed.load(), total, {});

        return outcome;
    }

    // 全部命中黑名单 / 全部失败时不要用空列表覆盖一份可用的缓存
    const bool replaceCatalog = shouldReplaceCatalog (report.found, report.failed, report.skipped);

    // 黑名单条目保留（跳过不等于修好），新失败追加
    std::vector<PluginFailure> mergedFailures = carried;

    for (auto& failure : newFailures)
        mergedFailures.push_back (std::move (failure));

    std::sort (mergedFailures.begin(), mergedFailures.end(),
               [] (const PluginFailure& a, const PluginFailure& b)
               {
                   return a.file < b.file;
               });

    report.failures.sort (true);

    std::sort (result.begin(), result.end(), [] (const PluginInfo& a, const PluginInfo& b)
    {
        const auto keyA = (a.manufacturer + " " + a.name).toLowerCase();
        const auto keyB = (b.manufacturer + " " + b.name).toLowerCase();
        return keyA < keyB;
    });

    outcome.replaceCatalog = replaceCatalog;
    outcome.plugins = std::move (result);
    outcome.failures = std::move (mergedFailures);

    if (progress)
        progress (completed.load(), total, {});

    return outcome;
}

PluginCatalog::ScanReport PluginCatalog::scan (
    const juce::StringArray& directories,
    const std::function<void (int, int, const juce::String&)>& progress,
    int timeoutPerPluginMs,
    bool ignoreBlacklist,
    const std::function<bool()>& shouldCancel,
    int parallelWorkers)
{
    auto outcome = scanDetached (directories, progress, timeoutPerPluginMs,
                                 ignoreBlacklist, shouldCancel, parallelWorkers);

    const auto report = outcome.report;
    applyScanOutcome (std::move (outcome));
    return report;
}

void PluginCatalog::applyScanOutcome (ScanOutcome&& outcome)
{
    if (outcome.replaceCatalog)
        plugins = std::move (outcome.plugins);

    failureList = std::move (outcome.failures);
}

bool PluginCatalog::save (const juce::File& file) const
{
    juce::Array<juce::var> entries;

    for (const auto& info : plugins)
        entries.add (info.toVar());

    juce::Array<juce::var> failureVars;

    for (const auto& failure : failureList)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("file", failure.file);
        object->setProperty ("reason", failure.reason);
        failureVars.add (juce::var (object));
    }

    juce::Array<juce::var> directoryVars;

    for (const auto& dir : extraDirectories)
        directoryVars.add (dir);

    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("scannedAt", juce::Time::currentTimeMillis());
    root->setProperty ("plugins", entries);
    root->setProperty ("failures", failureVars);
    root->setProperty ("directories", directoryVars);

    file.getParentDirectory().createDirectory();
    return writeTextAtomically (file, juce::JSON::toString (juce::var (root), false));
}

bool PluginCatalog::load (const juce::File& file)
{
    if (! file.existsAsFile())
    {
        clear();
        return false;
    }

    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    auto* root = parsed.getDynamicObject();

    // 解析失败时保留现有目录，避免把一个坏文件当成「插件全删了」
    if (root == nullptr)
        return false;

    std::vector<PluginInfo> loadedPlugins;
    std::vector<PluginFailure> loadedFailures;
    juce::StringArray loadedDirectories;

    if (auto* entries = root->getProperty ("plugins").getArray())
        for (const auto& entry : *entries)
            loadedPlugins.push_back (PluginInfo::fromVar (entry));

    if (auto* failures = root->getProperty ("failures").getArray())
        for (const auto& entry : *failures)
            if (auto* object = entry.getDynamicObject())
                loadedFailures.push_back ({ object->getProperty ("file").toString(),
                                            object->getProperty ("reason").toString() });

    if (auto* directories = root->getProperty ("directories").getArray())
        for (const auto& entry : *directories)
            loadedDirectories.addIfNotAlreadyThere (entry.toString());

    plugins = std::move (loadedPlugins);
    failureList = std::move (loadedFailures);
    extraDirectories = std::move (loadedDirectories);
    return true;
}

void PluginCatalog::clear()
{
    plugins.clear();
    failureList.clear();
    extraDirectories.clear();
}

bool PluginCatalog::isBlacklisted (const juce::String& path) const
{
    const juce::File candidate (path);

    for (const auto& failure : failureList)
        if (juce::File (failure.file) == candidate)
            return true;

    return false;
}

void PluginCatalog::addExtraDirectory (const juce::String& path)
{
    const juce::File dir (path);

    if (dir.isDirectory())
        extraDirectories.addIfNotAlreadyThere (dir.getFullPathName());
}

void PluginCatalog::clearBlacklist()
{
    failureList.clear();
}

juce::StringArray PluginCatalog::getAllScanDirectories() const
{
    auto dirs = defaultVST3Directories();

    for (const auto& extra : extraDirectories)
        dirs.addIfNotAlreadyThere (extra);

    return dirs;
}

int PluginCatalog::getNumEffects() const
{
    return static_cast<int> (std::count_if (plugins.begin(), plugins.end(),
                                            [] (const PluginInfo& info) { return info.isEffect(); }));
}

int PluginCatalog::getNumInstruments() const
{
    return static_cast<int> (std::count_if (plugins.begin(), plugins.end(),
                                            [] (const PluginInfo& info) { return info.isInstrument; }));
}

const PluginInfo* PluginCatalog::findByName (const juce::String& name) const
{
    for (const auto& info : plugins)
        if (info.name.equalsIgnoreCase (name))
            return &info;

    // 退化为子串匹配，方便测试与命令行
    for (const auto& info : plugins)
        if (info.name.containsIgnoreCase (name))
            return &info;

    return nullptr;
}

const PluginInfo* PluginCatalog::findByFile (const juce::File& file) const
{
    for (const auto& info : plugins)
        if (juce::File (info.fileOrIdentifier) == file)
            return &info;

    return nullptr;
}

} // namespace earlab::plugin
