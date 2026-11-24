// A5 插件测试：VST3 扫描、参数枚举、频段探针。

#include "plugin/PluginCatalog.h"
#include "plugin/PluginHost.h"
#include "plugin/PluginMemory.h"
#include "plugin/PluginProbe.h"
#include "plugin/PluginScanRunner.h"
#include "plugin/PluginTask.h"
#include "plugin/PluginTrainableProcessor.h"

#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
int failures = 0;

void check (const char* name, bool ok, const std::string& detail = {})
{
    std::printf ("[%s] %-40s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());

    if (! ok)
        ++failures;
}

std::string fmt (const juce::String& text)
{
    return text.toStdString();
}

std::string fmtDb (float db)
{
    char buffer[32];
    std::snprintf (buffer, sizeof (buffer), "%+.2f dB", static_cast<double> (db));
    return buffer;
}

/**
    P1-1 回归用：最小的 AudioPluginInstance，只记录真实收到的 processBlock 块大小。

    PluginTrainableProcessor 必须按 prepareToPlay 的 maxBlock 切块，
    不能把调用方（等响测量最长 10 s）的超块原样丢给第三方插件。
*/
class BlockRecordingPlugin final : public juce::AudioPluginInstance
{
public:
    BlockRecordingPlugin() = default;

    const juce::String getName() const override { return "Block Recorder"; }

    void prepareToPlay (double, int maximumExpectedSamplesPerBlock) override
    {
        preparedBlockSize = maximumExpectedSamplesPerBlock;
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        maxBlockSeen = juce::jmax (maxBlockSeen, buffer.getNumSamples());
        ++processCalls;
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    void fillInPluginDescription (juce::PluginDescription& description) const override
    {
        description.name = "Block Recorder";
    }

    int preparedBlockSize = 0;
    int maxBlockSeen = 0;
    int processCalls = 0;
};

void runChunkingChecks()
{
    // P1-1 回归：调用方给出 4096 样本（> prepare 的 512）时，包装器必须切块。
    int preparedBlock = -1;
    int maxBlockSeen = -1;
    int preparedMax = -1;
    int observedMax = -1;
    int requested = -1;

    {
        auto fake = std::make_unique<BlockRecordingPlugin>();
        auto* raw = fake.get();

        earlab::plugin::PluginTask task;
        task.pluginName = "Block Recorder";

        earlab::plugin::PluginTrainableProcessor processor (std::move (fake), task);
        processor.prepare (48000.0, 512, 2);

        juce::AudioBuffer<float> buffer (2, 4096);
        buffer.clear();
        processor.process (buffer);

        preparedBlock = raw->preparedBlockSize;
        maxBlockSeen = raw->maxBlockSeen;
        preparedMax = processor.getPreparedMaxBlockSize();
        observedMax = processor.getMaxObservedBlockSize();
        requested = processor.getLastRequestedSamples();
    }

    check ("P1-1 processBlock 不超过 prepare 块",
           preparedBlock == 512 && maxBlockSeen > 0 && maxBlockSeen <= 512
               && preparedMax == 512 && observedMax > 0 && observedMax <= preparedMax
               && requested == 4096,
           "prepared=" + std::to_string (preparedBlock)
               + " seen=" + std::to_string (maxBlockSeen)
               + " observed=" + std::to_string (observedMax)
               + " requested=" + std::to_string (requested));
}

/** 打印插件的参数表，用来决定出题启发式。 */
void dumpParameters (const earlab::plugin::PluginInfo& info)
{
    juce::String error;
    auto instance = earlab::plugin::PluginHost::create (
        info,
        earlab::plugin::PluginHost::probeSampleRate,
        earlab::plugin::PluginHost::probeBlockSize,
        error);

    if (instance == nullptr)
    {
        std::printf ("加载失败: %s\n", fmt (error).c_str());
        return;
    }

    auto parameters = earlab::plugin::PluginHost::listParameters (*instance);

    std::printf ("\n%s  (in=%d out=%d, latency=%d, params=%d)\n",
                 fmt (info.getDisplayName()).c_str(),
                 instance->getTotalNumInputChannels(),
                 instance->getTotalNumOutputChannels(),
                 instance->getLatencySamples(),
                 static_cast<int> (parameters.size()));

    int shown = 0;

    for (const auto& parameter : parameters)
    {
        if (shown++ >= 80)
        {
            std::printf ("  ... (%d more)\n", static_cast<int> (parameters.size()) - shown + 1);
            break;
        }

        std::printf ("  [%3d] %-34s def=%.3f steps=%d %s\n",
                     parameter.index,
                     fmt (parameter.name).c_str(),
                     static_cast<double> (parameter.defaultValue),
                     parameter.numSteps,
                     parameter.isBypassLike ? "bypass-like" : "");
    }
}

void runProbeReport (const earlab::plugin::PluginInfo& info)
{
    juce::String error;
    auto instance = earlab::plugin::PluginHost::create (
        info,
        earlab::plugin::PluginHost::probeSampleRate,
        earlab::plugin::PluginHost::probeBlockSize,
        error);

    if (instance == nullptr)
    {
        std::printf ("加载失败: %s\n", fmt (error).c_str());
        return;
    }

    const auto baseline = earlab::plugin::PluginProbe::measure (*instance, 48000.0);
    std::printf ("\n基线探针 ok=%d broadband=%s maxAbs=%.2f dB band=%s\n",
                 baseline.ok ? 1 : 0,
                 fmtDb (baseline.broadbandDeltaDb).c_str(),
                 static_cast<double> (baseline.maxAbsDeltaDb),
                 fmt (earlab::plugin::PluginProbe::formatBandLabel (
                     earlab::plugin::PluginProbe::bandCentres()[static_cast<size_t> (baseline.dominantBand)])).c_str());

    for (const auto& band : baseline.bands)
        std::printf ("  %-8s dry=%7.2f wet=%7.2f delta=%s\n",
                     fmt (earlab::plugin::PluginProbe::formatBandLabel (band.centreHz)).c_str(),
                     static_cast<double> (band.dryDb),
                     static_cast<double> (band.wetDb),
                     fmtDb (band.deltaDb).c_str());

    auto parameters = earlab::plugin::PluginHost::trainingCandidates (
        earlab::plugin::PluginHost::listParameters (*instance), 12);

    std::printf ("\n候选参数（前 12）:\n");

    for (const auto& parameter : parameters)
    {
        earlab::plugin::PluginHost::setAllToDefault (*instance);
        const auto low = earlab::plugin::PluginProbe::measureWithParameter (*instance, 48000.0,
                                                                           parameter.index, 0.0f);
        const auto high = earlab::plugin::PluginProbe::measureWithParameter (*instance, 48000.0,
                                                                            parameter.index, 1.0f);

        std::printf ("  [%3d] %-30s  0->%s (%s @%s)   1->%s (%s @%s)\n",
                     parameter.index,
                     fmt (parameter.name).c_str(),
                     fmtDb (low.maxAbsDeltaDb * (low.dominantDeltaDb >= 0 ? 1 : -1)).c_str(),
                     fmt (earlab::plugin::PluginProbe::formatBandLabel (
                         earlab::plugin::PluginProbe::bandCentres()[static_cast<size_t> (low.dominantBand)])).c_str(),
                     low.ok ? "ok" : "bad",
                     fmtDb (high.maxAbsDeltaDb * (high.dominantDeltaDb >= 0 ? 1 : -1)).c_str(),
                     fmt (earlab::plugin::PluginProbe::formatBandLabel (
                         earlab::plugin::PluginProbe::bandCentres()[static_cast<size_t> (high.dominantBand)])).c_str(),
                     high.ok ? "ok" : "bad");
    }
}

} // namespace

/** 选项构造是纯函数，不需要真实插件，所以每个模式都先跑一遍。 */
void runOptionBuilderChecks()
{
    juce::Random rng (20261023u);
    bool amountValid = true;
    bool amountUnique = true;
    bool directionValid = true;
    bool directionUnique = true;
    bool broadbandOk = true;

    for (int i = 0; i < 300; ++i)
    {
        const float measured = 2.0f + static_cast<float> (i % 90) * 0.1f;
        const auto amount = earlab::plugin::buildAmountOptions (measured, rng);

        if (! amount.valid)
            amountValid = false;

        juce::StringArray labels;
        for (const auto& label : amount.labels)
            labels.add (label);

        labels.removeDuplicates (false);

        if (labels.size() != 4)
            amountUnique = false;

        const bool broadband = (i % 2) == 0;
        const auto direction = earlab::plugin::buildDirectionOptions (
            broadband, i % 10, (i % 3) == 0 ? 1 : -1, rng);

        if (! direction.valid)
            directionValid = false;

        juce::StringArray directionLabels;
        for (const auto& label : direction.labels)
            directionLabels.add (label);

        directionLabels.removeDuplicates (false);

        if (directionLabels.size() != 4)
            directionUnique = false;

        if (broadband)
        {
            int overallCount = 0;

            for (const auto& label : direction.labels)
                if (label.startsWith (juce::String::fromUTF8 ("整体")))
                    ++overallCount;

            if (overallCount != 2)
                broadbandOk = false;
        }
    }

    check ("幅度题选项互异", amountValid && amountUnique, "");
    check ("方向题选项互异", directionValid && directionUnique, "");
    check ("宽带题只含一个正确项", broadbandOk, "");

    const auto six = earlab::plugin::buildAmountOptions (6.4f, rng);
    check ("幅度题正确项=最近档位",
           six.valid && six.labels[static_cast<size_t> (six.correctIndex)]
                            == juce::String::fromUTF8 ("约 6 dB"),
           fmt (six.labels[static_cast<size_t> (six.correctIndex)]));
}

void runHostPureChecks()
{
    using earlab::plugin::PluginHost;

    const auto memoryFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("earlab-memory-regression", ".json", false);
    const auto memoryBackup = memoryFile.getSiblingFile (memoryFile.getFileName() + ".bak");
    earlab::plugin::PluginMemoryStore memory;
    earlab::plugin::PluginTask task;
    task.parameterIndex = 7;
    memory.record ("test", "Test", task);
    memory.save (memoryFile);
    memory.save (memoryFile);
    const auto backupText = memoryBackup.loadFileAsString();
    memoryFile.replaceWithText ("{}");
    earlab::plugin::PluginMemoryStore recovered;
    check ("插件记忆结构损坏从备份恢复", recovered.load (memoryFile) && recovered.find ("test") != nullptr);
    recovered.save (memoryFile);
    check ("插件记忆恢复后不覆盖好备份", memoryBackup.loadFileAsString() == backupText);
    memoryFile.deleteFile();
    check ("插件记忆主文件缺失恢复备份", recovered.load (memoryFile) && recovered.find ("test") != nullptr);
    memoryFile.deleteFile();
    memoryBackup.deleteFile();

    // P0-1 回归：多输出插件必须按输出声道分配预热缓冲。
    check ("P0-1 预热声道 (0,64)->64", PluginHost::preheatChannelCount (0, 64) == 64,
           std::to_string (PluginHost::preheatChannelCount (0, 64)));
    check ("P0-1 预热声道 (4,2)->4", PluginHost::preheatChannelCount (4, 2) == 4,
           std::to_string (PluginHost::preheatChannelCount (4, 2)));
    check ("P0-1 预热声道 (1,1)->2", PluginHost::preheatChannelCount (1, 1) == 2,
           std::to_string (PluginHost::preheatChannelCount (1, 1)));

    // 实例报告 0/0 时按扫描元数据兜底。
    check ("P0-1 声道元数据兜底",
           PluginHost::resolvedPreheatChannelCount (0, 0, 0, 64) == 64
               && PluginHost::resolvedPreheatChannelCount (0, 0, 2, 2) == 2,
           std::to_string (PluginHost::resolvedPreheatChannelCount (0, 0, 0, 64)) + "/"
               + std::to_string (PluginHost::resolvedPreheatChannelCount (0, 0, 2, 2)));

    // P1-3 回归：预检与加载共用同一类型规则。
    std::vector<earlab::plugin::PluginInfo> mixed (2);
    mixed[0].isInstrument = true;
    mixed[1].isInstrument = false;

    std::vector<earlab::plugin::PluginInfo> instrumentsOnly (1);
    instrumentsOnly[0].isInstrument = true;

    check ("P1-3 类型规则：第一个非音源，否则 front",
           PluginHost::chooseLoadableIndex (mixed) == 1
               && PluginHost::chooseLoadableIndex (instrumentsOnly) == 0
               && PluginHost::chooseLoadableIndex ({}) == -1,
           std::to_string (PluginHost::chooseLoadableIndex (mixed)));
}

void runChildScanFailureCheck()
{
    // P1-2 回归：子进程起不来时只能返回错误，不能静默退回主进程扫描。
    const auto missingExe = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("earlab-no-such-scanner-exe.exe");
    missingExe.deleteFile();

    const auto result = earlab::plugin::PluginScanRunner::scanInChild (
        missingExe, juce::String ("C:\\no-such-plugin.vst3"), 1000);

    check ("P1-2 子进程不可用不回退主进程",
           result.plugins.empty() && result.error.isNotEmpty()
               && ! result.crashed && ! result.timedOut && ! result.cancelled,
           fmt (result.error));

    // P1-2：预检子进程不可用时必须报 unavailable（UI 会据此拒绝加载）。
    const auto preflight = earlab::plugin::PluginScanRunner::preflightInChild (
        missingExe, juce::String ("C:\\no-such-plugin.vst3"), 1000);

    check ("P1-2 预检不可用=拒绝加载",
           preflight.unavailable && ! preflight.ok && preflight.error.isNotEmpty(),
           fmt (preflight.error));

    const auto cancelled = earlab::plugin::PluginScanRunner::preflightInChild (
        missingExe, "unused", 1000, [] { return true; });
    check ("取消的预检不启动进程且不记为超时",
           cancelled.cancelled && ! cancelled.ok && ! cancelled.timedOut && ! cancelled.unavailable);

    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto stopped = earlab::plugin::PluginScanRunner::preflightInChild (
        juce::File::getSpecialLocation (juce::File::currentExecutableFile),
        "--earlab-test-child-wait", 10000,
        [started] { return juce::Time::getMillisecondCounterHiRes() - started >= 150.0; });
    check ("预检等待可取消且及时回收子进程",
           stopped.cancelled && ! stopped.ok && ! stopped.timedOut
               && juce::Time::getMillisecondCounterHiRes() - started < 2000.0);
}

void runCatalogCacheChecks()
{
    using earlab::plugin::PluginCatalog;

    check ("缓存覆盖：全部 skipped 时保留", ! PluginCatalog::shouldReplaceCatalog (0, 0, 5));
    check ("缓存覆盖：全部失败时保留", ! PluginCatalog::shouldReplaceCatalog (0, 3, 0));
    check ("缓存覆盖：有成功结果时替换", PluginCatalog::shouldReplaceCatalog (2, 0, 0));
    check ("缓存覆盖：完全没有候选时清空", PluginCatalog::shouldReplaceCatalog (0, 0, 0));
}

int main (int argc, char** argv)
{
    if (argc == 3 && juce::String (argv[1]) == "--vst3-preflight"
        && juce::String (argv[2]) == "--earlab-test-child-wait")
    {
        juce::Thread::sleep (5000);
        return 0;
    }
    // VST3 实例需要 MessageManager
    juce::ScopedJuceInitialiser_GUI juceInit;

    // ---- 子进程模式：只扫一个插件，把结果写成 JSON
    {
        juce::String scanOne, scanOut;

        for (int i = 1; i < argc; ++i)
        {
            const juce::String arg (argv[i]);

            if (arg == "--vst3-scan-one" && i + 1 < argc)
                scanOne = juce::String (argv[i + 1]).unquoted();
            else if (arg == "--vst3-scan-out" && i + 1 < argc)
                scanOut = juce::String (argv[i + 1]).unquoted();
        }

        if (scanOne.isNotEmpty() && scanOut.isNotEmpty())
            return earlab::plugin::PluginScanRunner::runScanChild (scanOne, juce::File (scanOut));
    }

    runOptionBuilderChecks();
    runHostPureChecks();
    runChildScanFailureCheck();
    runCatalogCacheChecks();
    runChunkingChecks();

    const auto cacheFile = earlab::plugin::PluginScanRunner::scanTempDirectory()
                               .getChildFile ("earlab-plugin-cache.json");

    bool forceRescan = false;
    juce::String paramsName, probeName, scanDir;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);

        if (arg == "--rescan")
            forceRescan = true;
        else if (arg.startsWith ("--params="))
            paramsName = arg.substring (9);
        else if (arg.startsWith ("--probe="))
            probeName = arg.substring (8);
        else if (arg.startsWith ("--scan-dir="))
            scanDir = arg.substring (11).unquoted();
    }

    juce::StringArray directories = earlab::plugin::PluginCatalog::defaultVST3Directories();

    if (scanDir.isNotEmpty())
    {
        directories.clear();
        directories.add (scanDir);
    }

    // 绝对路径直达：跳过目录扫描（受限环境里扫描会被出事的插件终止进程）
    juce::String directFile;
    juce::String parentOne;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg (argv[i]);

        if (arg.startsWith ("--file="))
            directFile = arg.substring (7).unquoted();
        else if (arg.startsWith ("--parent-one="))
            parentOne = arg.substring (13).unquoted();
    }

    if (parentOne.isNotEmpty())
    {
        const auto selfExe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        const auto result = earlab::plugin::PluginScanRunner::scanInChild (selfExe, parentOne, 20000);

        std::printf ("父进程扫描: 插件 %d，崩溃 %d，超时 %d，错误 %s\n",
                     static_cast<int> (result.plugins.size()),
                     result.crashed ? 1 : 0, result.timedOut ? 1 : 0,
                     fmt (result.error).c_str());

        if (! result.plugins.empty())
            std::printf ("  名称: %s\n", fmt (result.plugins.front().name).c_str());

        return 0;
    }

    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--unit-only")
        {
            std::printf ("%s: %d failures (unit-only; real plugins not scanned)\n",
                         failures == 0 ? "GREEN" : "RED", failures);
            return failures == 0 ? 0 : 1;
        }

    earlab::plugin::PluginCatalog catalog;
    earlab::plugin::PluginCatalog::ScanReport report;

    const bool loadedFromCache = directFile.isNotEmpty()
                              || (! forceRescan && catalog.load (cacheFile));
    if (directFile.isNotEmpty())
    {
        // 不扫目录
    }
    else if (loadedFromCache)
    {
        std::printf ("从缓存载入 %d 个插件（%s）\n",
                     static_cast<int> (catalog.getPlugins().size()),
                     fmt (cacheFile.getFullPathName()).c_str());
    }
    else
    {
        std::printf ("VST3 目录:\n");

        for (const auto& dir : directories)
            std::printf ("  %s\n", fmt (dir).c_str());

        const auto start = juce::Time::getMillisecondCounterHiRes();

        report = catalog.scan (directories, [] (int done, int total, const juce::String& current)
        {
            if (total > 0 && (done == total || done % 10 == 0))
                std::printf ("  扫描 %d/%d %s\n", done, total, fmt (current).c_str());
        });

        const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;

        std::printf ("\n扫描完成: 候选 %d，找到 %d，失败 %d，效果器 %d，耗时 %.1f 秒\n",
                     report.candidates, report.found, report.failed,
                     catalog.getNumEffects(), elapsed);

        for (const auto& failure : report.failures)
            std::printf ("  失败: %s\n", fmt (failure).c_str());

        // 空结果不写缓存，否则下次会一直从空缓存启动
        if (report.found > 0)
            catalog.save (cacheFile);
    }

    if (paramsName.isNotEmpty() || probeName.isNotEmpty())
    {
        const auto* info = catalog.findByName (paramsName.isNotEmpty() ? paramsName : probeName);

        if (info == nullptr)
        {
            std::printf ("\n找不到插件: %s\n", fmt (paramsName.isNotEmpty() ? paramsName : probeName).c_str());
            return 1;
        }

        if (paramsName.isNotEmpty())
            dumpParameters (*info);
        else
            runProbeReport (*info);

        return 0;
    }

    // 绝对路径直达：跳过目录扫描（受限环境里扫描会被出事的插件终止进程）
    if (directFile.isNotEmpty())
    {
        // 先取真实描述，name/uniqueId 不对 VST3 工厂会拒绝加载
        const auto described = earlab::plugin::PluginScanRunner::scanInProcess (directFile);

        if (described.plugins.empty())
        {
            std::printf ("无法读取插件描述: %s\n", fmt (described.error).c_str());
            return 1;
        }

        const auto& info = described.plugins.front();

        juce::String error;
        auto instance = earlab::plugin::PluginHost::create (
        info,
        earlab::plugin::PluginHost::probeSampleRate,
        earlab::plugin::PluginHost::probeBlockSize,
        error);

        if (instance == nullptr)
        {
            std::printf ("加载失败: %s\n", fmt (error).c_str());
            return 1;
        }

        std::printf ("加载成功: %s\n", fmt (instance->getName()).c_str());

        const auto parameters = earlab::plugin::PluginHost::listParameters (*instance);
        std::printf ("参数 %d 个，输入 %d，输出 %d，延迟 %d 样本\n",
                     static_cast<int> (parameters.size()),
                     instance->getTotalNumInputChannels(),
                     instance->getTotalNumOutputChannels(),
                     instance->getLatencySamples());

        int shown = 0;

        for (const auto& parameter : parameters)
        {
            if (shown++ >= 100)
                break;

            std::printf ("  [%3d] %-36s def=%.3f steps=%d%s\n",
                         parameter.index,
                         fmt (parameter.name).c_str(),
                         static_cast<double> (parameter.defaultValue),
                         parameter.numSteps,
                         parameter.isBypassLike ? "  (bypass-like)" : "");
        }

        std::printf ("\n基线探针:\n");
        const auto baseline = earlab::plugin::PluginProbe::measure (*instance, 48000.0);
        std::printf ("  ok=%d broadband=%s maxAbs=%.2f dB\n",
                     baseline.ok ? 1 : 0,
                     fmtDb (baseline.broadbandDeltaDb).c_str(),
                     static_cast<double> (baseline.maxAbsDeltaDb));

        for (const auto& band : baseline.bands)
            std::printf ("  %-8s delta=%s\n",
                         fmt (earlab::plugin::PluginProbe::formatBandLabel (band.centreHz)).c_str(),
                         fmtDb (band.deltaDb).c_str());

        std::printf ("\n候选参数动态扫描:\n");
        auto candidates = earlab::plugin::PluginHost::trainingCandidates (parameters, 16);

        for (const auto& parameter : candidates)
        {
            earlab::plugin::PluginHost::setAllToDefault (*instance);
            const auto switches = earlab::plugin::PluginHost::activateRelatedSwitches (*instance, parameters,
                                                                                       parameter.index);
            const auto low  = earlab::plugin::PluginProbe::measureWithParameter (*instance, 48000.0, parameter.index, 0.0f);
            const auto high = earlab::plugin::PluginProbe::measureWithParameter (*instance, 48000.0, parameter.index, 1.0f);

            std::printf ("  [%3d] %-30s +%d开关 low: %6.2f dB @%-7s high: %6.2f dB @%-7s\n",
                         parameter.index,
                         fmt (parameter.name).c_str(),
                         static_cast<int> (switches.size()),
                         static_cast<double> (low.dominantDeltaDb),
                         fmt (earlab::plugin::PluginProbe::formatBandLabel (
                             earlab::plugin::PluginProbe::bandCentres()[static_cast<size_t> (low.dominantBand)])).c_str(),
                         static_cast<double> (high.dominantDeltaDb),
                         fmt (earlab::plugin::PluginProbe::formatBandLabel (
                             earlab::plugin::PluginProbe::bandCentres()[static_cast<size_t> (high.dominantBand)])).c_str());
        }

        // ---- 中间值验证：确认参数映射是连续的，二分才有意义
        std::printf ("\n中间值验证（Band 1 Gain，参数 3）:\n");

        for (const float value : { 0.50f, 0.55f, 0.60f, 0.75f, 0.90f })
        {
            earlab::plugin::PluginHost::setAllToDefault (*instance);
            earlab::plugin::PluginHost::activateRelatedSwitches (*instance, parameters, 3);
            const auto probe = earlab::plugin::PluginProbe::measureWithParameter (*instance, 48000.0, 3, value);
            std::printf ("  %.2f -> %+6.2f dB @%s ok=%d\n",
                         static_cast<double> (value),
                         static_cast<double> (probe.dominantDeltaDb),
                         fmt (earlab::plugin::PluginProbe::formatBandLabel (
                             earlab::plugin::PluginProbe::bandCentres()[static_cast<size_t> (probe.dominantBand)])).c_str(),
                         probe.ok ? 1 : 0);
        }

        // ---- 出题验证
        std::printf ("\n出题（目标 3 / 6 / 10 dB）:\n");

        for (const float target : { 3.0f, 6.0f, 10.0f })
        {
            earlab::plugin::PluginHost::setAllToDefault (*instance);
            earlab::plugin::PluginTaskGenerator generator (20251023u);
            const auto task = generator.generate (*instance, info, parameters, target, target / 12.0f);

            if (task.parameterIndex < 0)
            {
                std::printf ("  目标 %4.0f dB: 没有可用参数\n", static_cast<double> (target));
                continue;
            }

            std::printf ("  目标 %4.0f dB -> %s = %.3f，实测 %+.2f dB @%s\n",
                         static_cast<double> (target),
                         fmt (task.parameterName).c_str(),
                         static_cast<double> (task.parameterValue),
                         static_cast<double> (task.measuredDeltaDb),
                         fmt (earlab::plugin::PluginProbe::formatBandLabel (
                             earlab::plugin::PluginProbe::bandCentres()[static_cast<size_t> (task.bandIndex)])).c_str());

            for (int i = 0; i < 4; ++i)
                std::printf ("      %c %s\n", i == task.correctIndex ? '*' : ' ',
                             fmt (task.optionLabels[static_cast<size_t> (i)]).c_str());

            // 复现：按任务描述的参数渲染，应该得到同样的频谱变化
            earlab::plugin::PluginTaskGenerator::applyToPlugin (*instance, task);
            const auto replay = earlab::plugin::PluginProbe::measure (*instance, 48000.0);
            std::printf ("     复现: %+.2f dB，误差 %.2f dB\n",
                         static_cast<double> (replay.dominantDeltaDb),
                         static_cast<double> (std::abs (replay.dominantDeltaDb - task.measuredDeltaDb)));
        }

        // ---- 断言：这些数字就是 A5 的验收依据
        std::printf ("\n------------------------------\n");

        check ("真实插件加载", instance != nullptr);
        check ("参数枚举", parameters.size() >= 20, std::to_string (parameters.size()));
        check ("基线透明", baseline.ok && baseline.maxAbsDeltaDb < 0.05f,
               fmtDb (baseline.maxAbsDeltaDb).c_str());

        for (const float target : { 3.0f, 8.0f })
        {
            earlab::plugin::PluginHost::setAllToDefault (*instance);
            earlab::plugin::PluginTaskGenerator generator (4242u);
            const auto task = generator.generate (*instance, info, parameters, target, target / 12.0f);

            const auto label = "目标 " + std::to_string (static_cast<int> (target)) + " dB";
            check ((label + " 生成").c_str(), task.parameterIndex >= 0,
                   fmt (task.parameterName).c_str());

            if (task.parameterIndex < 0)
                continue;

            check ((label + " 幅度").c_str(),
                   std::abs (std::abs (task.measuredDeltaDb) - target) <= 0.6f,
                   fmtDb (task.measuredDeltaDb).c_str());

            juce::StringArray unique;

            for (const auto& option : task.optionLabels)
                unique.addIfNotAlreadyThere (option);

            check ((label + " 选项互异").c_str(), unique.size() == 4, std::to_string (unique.size()));
            check ((label + " 答案有效").c_str(),
                   task.correctIndex >= 0 && task.correctIndex < 4,
                   std::to_string (task.correctIndex));

            earlab::plugin::PluginTaskGenerator::applyToPlugin (*instance, task);
            const auto replay = earlab::plugin::PluginProbe::measure (*instance, 48000.0);
            check ((label + " 可复现").c_str(),
                   std::abs (replay.dominantDeltaDb - task.measuredDeltaDb) <= 0.3f,
                   fmtDb (replay.dominantDeltaDb - task.measuredDeltaDb).c_str());
        }

        // ---- 幅度题：选项必须是 dB 档位，实测要命中档位
        {
            earlab::plugin::PluginHost::setAllToDefault (*instance);
            earlab::plugin::PluginTaskGenerator generator (5150u);
            const auto task = generator.generate (*instance, info, parameters, 6.0f, 0.5f, 48000.0, -1,
                                                  earlab::plugin::PluginQuestionKind::bandAmount);

            check ("幅度题生成", task.parameterIndex >= 0
                                 && task.kind == earlab::plugin::PluginQuestionKind::bandAmount,
                   fmt (task.parameterName).c_str());

            if (task.parameterIndex >= 0)
            {
                check ("幅度题命中档位",
                       std::abs (std::abs (task.measuredDeltaDb) - task.amountDb) <= 0.6f,
                       fmtDb (task.measuredDeltaDb) + " / " + fmtDb (task.amountDb));

                bool labelsOk = true;

                for (const auto& label : task.optionLabels)
                    labelsOk = labelsOk && label.startsWith (juce::String::fromUTF8 ("约 "))
                                        && label.endsWith (" dB");

                check ("幅度题选项格式", labelsOk, "");

                const auto& answer = task.optionLabels[static_cast<size_t> (task.correctIndex)];
                check ("幅度题答案匹配档位",
                       answer.contains (juce::String (juce::roundToInt (task.amountDb))),
                       fmt (answer).c_str());

                earlab::plugin::PluginTaskGenerator::applyToPlugin (*instance, task);
                const auto replay = earlab::plugin::PluginProbe::measure (*instance, 48000.0);
                check ("幅度题可复现",
                       std::abs (replay.dominantDeltaDb - task.measuredDeltaDb) <= 0.3f,
                       fmtDb (replay.dominantDeltaDb - task.measuredDeltaDb).c_str());
            }
        }

        // ---- 插件记忆：参数与题型计数能落盘回读
        {
            earlab::plugin::PluginMemoryStore memory;

            earlab::plugin::PluginTask first;
            first.kind = earlab::plugin::PluginQuestionKind::bandAmount;
            first.parameterIndex = 3;

            earlab::plugin::PluginTask second;
            second.kind = earlab::plugin::PluginQuestionKind::bandDirection;
            second.parameterIndex = 5;

            memory.record ("test-id", "Test Plugin", first);
            memory.record ("test-id", "Test Plugin", second);

            const auto* entry = memory.find ("test-id");
            check ("插件记忆计数", entry != nullptr && entry->tasks == 2
                                   && entry->preferredParameter == 5,
                   entry != nullptr ? std::to_string (entry->tasks) : "null");

            check ("插件记忆题型分布", entry != nullptr && entry->amountTasks == 1
                                        && entry->directionTasks == 1, "");

            auto file = earlab::plugin::PluginScanRunner::scanTempDirectory()
                            .getChildFile ("earlab-plugin-memory.json");
            file.deleteFile();

            const bool saved = memory.save (file);

            earlab::plugin::PluginMemoryStore reloaded;
            const bool loaded = reloaded.load (file);
            const auto* back = reloaded.find ("test-id");

            check ("插件记忆落盘", saved && loaded && back != nullptr
                                    && back->preferredParameter == 5
                                    && back->tasks == 2,
                   std::to_string (back != nullptr ? back->tasks : 0));

            file.deleteFile();
        }

        // ---- 确定性：同 seed、同初始状态必须生成同一道题
        {
            earlab::plugin::PluginHost::setAllToDefault (*instance);
            earlab::plugin::PluginTaskGenerator a (777u);
            const auto first = a.generate (*instance, info, parameters, 6.0f, 0.5f);

            earlab::plugin::PluginHost::setAllToDefault (*instance);
            earlab::plugin::PluginTaskGenerator b (777u);
            const auto second = b.generate (*instance, info, parameters, 6.0f, 0.5f);

            const bool same = first.parameterIndex == second.parameterIndex
                           && std::abs (first.parameterValue - second.parameterValue) < 1.0e-6f
                           && first.correctIndex == second.correctIndex
                           && first.optionLabels == second.optionLabels;

            check ("同 seed 结果一致", same,
                   std::to_string (first.parameterIndex) + "/" + std::to_string (second.parameterIndex));
        }

        // ---- 处理器：插件包装后必须真的改变音频
        {
            earlab::plugin::PluginHost::setAllToDefault (*instance);
            earlab::plugin::PluginTaskGenerator generator (99u);
            const auto task = generator.generate (*instance, info, parameters, 8.0f, 0.7f);

            juce::String createError;
            auto second = earlab::plugin::PluginHost::create (
                info,
                earlab::plugin::PluginHost::probeSampleRate,
                earlab::plugin::PluginHost::probeBlockSize,
                createError);
            check ("第二个实例", second != nullptr, fmt (createError).c_str());

            if (second != nullptr && task.parameterIndex >= 0)
            {
                earlab::plugin::PluginTrainableProcessor processor (std::move (second), task);
                processor.prepare (earlab::plugin::PluginHost::probeSampleRate,
                                   earlab::plugin::PluginHost::probeBlockSize, 2);

                juce::AudioBuffer<float> buffer (2, 512);
                juce::Random noise (1234);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        buffer.setSample (ch, i, noise.nextFloat() * 0.2f - 0.1f);

                juce::AudioBuffer<float> before;
                before.makeCopyOf (buffer);

                processor.process (buffer);

                double difference = 0.0;

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        difference = juce::jmax (difference,
                                                 static_cast<double> (std::abs (buffer.getSample (ch, i)
                                                                                - before.getSample (ch, i))));

                check ("处理器改变音频", difference > 0.001, std::to_string (difference));
                check ("处理器名称", processor.getName() == task.pluginName,
                       fmt (processor.getName()).c_str());

                // P1-1 回归（真实插件）：10 s 级超块必须切成 <= prepare 的 maxBlock。
                juce::AudioBuffer<float> longBuffer (2, 48000);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < longBuffer.getNumSamples(); ++i)
                        longBuffer.setSample (ch, i,
                                              0.1f * static_cast<float> (std::sin (0.01 * i)));

                processor.process (longBuffer);

                check ("真实插件超块按 maxBlock 分块",
                       processor.getPreparedMaxBlockSize() == 512
                           && processor.getMaxObservedBlockSize() > 0
                           && processor.getMaxObservedBlockSize() <= processor.getPreparedMaxBlockSize()
                           && processor.getLastRequestedSamples() == 48000,
                       "observed=" + std::to_string (processor.getMaxObservedBlockSize())
                           + " prepared=" + std::to_string (processor.getPreparedMaxBlockSize()));
            }
        }

        std::printf ("------------------------------\n%s: %d 项失败\n",
                     failures == 0 ? "GREEN" : "RED", failures);

        return failures == 0 ? 0 : 1;
    }

    std::printf ("\n插件清单:\n");

    for (const auto& info : catalog.getPlugins())
        std::printf ("  %-14s %-40s in=%d out=%d %s\n",
                     fmt (info.manufacturer).c_str(),
                     fmt (info.name).c_str(),
                     info.numInputs, info.numOutputs,
                     info.isEffect() ? "effect" : "instrument/other");


    std::printf ("\n------------------------------\n");

    if (catalog.getPlugins().empty())
    {
        std::printf ("\n------------------------------\n");
        std::printf ("SKIP: 当前环境没有可用的插件目录/禁止创建扫描子进程，无法验证目录扫描。\n");
        std::printf ("      这不算通过：请在普通桌面会话里运行本程序确认扫描结果。\n");
        return 0;
    }

    const auto tmpCache = earlab::plugin::PluginScanRunner::scanTempDirectory()
                              .getChildFile ("earlab-plugin-cache-roundtrip.json");
    check ("目录可保存", catalog.save (tmpCache));

    earlab::plugin::PluginCatalog reloaded;
    const bool reloadOk = reloaded.load (tmpCache);
    const auto reloadedCount = reloaded.getPlugins().size();
    const auto originalCount = catalog.getPlugins().size();

    std::printf ("  [cache] load=%d reloaded=%zu original=%zu file=%s\n",
                 reloadOk ? 1 : 0, reloadedCount, originalCount,
                 fmt (tmpCache.getFullPathName()).c_str());

    check ("目录可载入", reloadOk && reloadedCount == originalCount,
           std::to_string (reloadedCount) + "/" + std::to_string (originalCount));
    tmpCache.deleteFile();

    std::printf ("------------------------------\n%s: %d 项失败\n",
                 failures == 0 ? "GREEN" : "RED", failures);
    return failures == 0 ? 0 : 1;
}
