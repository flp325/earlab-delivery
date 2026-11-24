#pragma once

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <functional>
#include <vector>

namespace earlab::plugin
{

/** 源码里的中文都是 UTF-8，必须显式按 UTF-8 构造 juce::String。 */
inline juce::String utf8 (const char* text)
{
    return juce::String (juce::CharPointer_UTF8 (text));
}

/** 扫描到的一个插件条目。只保存元数据，不持有实例。 */
struct PluginInfo
{
    juce::String name;
    juce::String manufacturer;
    juce::String category;
    juce::String version;
    juce::String format;             // 目前只会是 "VST3"
    juce::String identifier;         // PluginDescription::createIdentifierString()
    juce::String fileOrIdentifier;   // .vst3 包路径
    int uniqueId = 0;
    int numInputs = 0;
    int numOutputs = 0;
    bool isInstrument = false;

    juce::var toVar() const;
    static PluginInfo fromVar (const juce::var& value);

    /** 厂商 + 名称，列表里显示用。 */
    [[nodiscard]] juce::String getDisplayName() const;

    /** 能当训练处理器用的效果器：不是音源、有输入输出。 */
    [[nodiscard]] bool isEffect() const noexcept;
};

/** 扫描失败记录：路径 + 原因，下次扫描默认跳过。 */
struct PluginFailure
{
    juce::String file;
    juce::String reason;
};

/**
    第三方插件目录。

    刻意不复用 juce::KnownPluginList / PluginDirectoryScanner（它们在 juce_audio_processors
    里，会把 GUI 模块拖进控制台测试），改为直接调 AudioPluginFormat::findAllTypesForFile，
    这样扫描逻辑可以在无头测试里跑真实插件。
*/
class PluginCatalog
{
public:
    PluginCatalog();

    /** 标准 VST3 搜索路径（每台机器可能都有差异，运行时再探测）。 */
    static juce::StringArray defaultVST3Directories();

    static juce::File defaultCacheFile();

    /**
        扫描结果是否应该覆盖现有目录。

        全部命中黑名单（found=0, failed=0, skipped>0）或全部失败时不能覆盖，
        否则一次临时性故障就会把唯一一份可用缓存清空；只有「确实什么都没有」才清空。
    */
    static bool shouldReplaceCatalog (int found, int failed, int skipped);

    struct ScanReport
    {
        int candidates = 0;
        int found = 0;
        int failed = 0;
        int skipped = 0;          // 命中失败列表而跳过
        int instruments = 0;      // 找到的音源数量（列表里不显示）
        bool cancelled = false;
        juce::StringArray failures;

        [[nodiscard]] bool isUsable() const noexcept { return found > 0; }
    };

    /**
        扫描结果快照（U-P1-4）。

        扫描在 worker 线程执行，而 PluginBrowser 的列表仍会在消息循环里重绘。
        若 worker 直接改 catalog 的 vector，消息线程会读到被释放的存储（UAF）。
        因此扫描只产出不可变快照，由消息线程调用 applyScanOutcome() 一次性发布。
    */
    struct ScanOutcome
    {
        ScanReport report;
        bool replaceCatalog = false;
        std::vector<PluginInfo> plugins;
        std::vector<PluginFailure> failures;
    };

    /**
        扫描目录；会清空旧结果。每个插件在独立子进程中扫描，超时/崩溃记为失败。
        parallelWorkers <= 0 时按 CPU 数自动选（最多 4 路）。
    */
    ScanReport scan (const juce::StringArray& directories,
                     const std::function<void (int done, int total, const juce::String& current)>& progress = {},
                     int timeoutPerPluginMs = 20000,
                     bool ignoreBlacklist = false,
                     const std::function<bool()>& shouldCancel = {},
                     int parallelWorkers = 0);

    /** 只扫描、不修改 catalog（worker 线程安全）；结果用 applyScanOutcome 在消息线程发布。 */
    ScanOutcome scanDetached (const juce::StringArray& directories,
                              const std::function<void (int done, int total, const juce::String& current)>& progress = {},
                              int timeoutPerPluginMs = 20000,
                              bool ignoreBlacklist = false,
                              const std::function<bool()>& shouldCancel = {},
                              int parallelWorkers = 0) const;

    /** 在消息线程一次性发布扫描结果（不可变快照 → catalog 核心容器）。 */
    void applyScanOutcome (ScanOutcome&& outcome);

    bool save (const juce::File& file) const;
    bool load (const juce::File& file);
    void clear();

    [[nodiscard]] const std::vector<PluginInfo>& getPlugins() const noexcept { return plugins; }
    [[nodiscard]] const std::vector<PluginFailure>& getFailures() const noexcept { return failureList; }
    [[nodiscard]] const juce::StringArray& getExtraDirectories() const noexcept { return extraDirectories; }
    [[nodiscard]] int getNumEffects() const;
    [[nodiscard]] int getNumInstruments() const;
    [[nodiscard]] const PluginInfo* findByName (const juce::String& name) const;
    [[nodiscard]] const PluginInfo* findByFile (const juce::File& file) const;
    [[nodiscard]] bool isBlacklisted (const juce::String& path) const;

    void addExtraDirectory (const juce::String& path);
    void clearBlacklist();

    /** 默认目录 + 用户添加的目录。 */
    [[nodiscard]] juce::StringArray getAllScanDirectories() const;

    [[nodiscard]] juce::AudioPluginFormatManager& getFormatManager() noexcept { return formatManager; }

private:
    juce::AudioPluginFormat* findVst3Format() const;

    juce::AudioPluginFormatManager formatManager;
    std::vector<PluginInfo> plugins;
    std::vector<PluginFailure> failureList;
    juce::StringArray extraDirectories;
};

} // namespace earlab::plugin
