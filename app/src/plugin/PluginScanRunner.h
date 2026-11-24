#pragma once

#include "PluginCatalog.h"

#include <functional>
#include <vector>

namespace earlab::plugin
{

/**
    崩溃隔离扫描。

    第三方 VST3 在同一进程里加载会把宿主一起带走：本机第一个插件就段错误。
    所以每个插件单独起一个子进程扫，父进程只收结果；超时或崩溃记为失败，
    不影响其它插件，也不影响主程序。
*/
class PluginScanRunner
{
public:
    struct ChildResult
    {
        std::vector<PluginInfo> plugins;
        bool crashed = false;
        bool timedOut = false;
        bool cancelled = false;      // 调用方主动取消，不应记入失败列表
        juce::String error;
    };

    /** 子进程入口：扫描单个 .vst3，把结果写成 JSON。返回进程退出码。 */
    static int runScanChild (const juce::String& pluginPath, const juce::File& outFile);

    /**
        加载前预检：在隔离子进程里真正实例化并跑一个块，确认不会把宿主带走。

        扫描子进程只读元数据，插件在 createPluginInstance/prepareToPlay/首帧
        processBlock 里崩溃的问题扫不出来；这里补上这一段。
    */
    struct PreflightResult
    {
        bool ok = false;
        bool crashed = false;
        bool timedOut = false;
        bool unavailable = false;   // 起不了子进程（受限环境），调用方可选择继续
        bool cancelled = false;
        juce::String error;
    };

    /** 子进程入口：实例化插件、准备、跑一个静音块。返回进程退出码（0 = 通过）。 */
    static int runPreflightChild (const juce::String& pluginPath);

    /** 父进程：起子进程做加载预检。 */
    static PreflightResult preflightInChild (const juce::File& selfExe,
                                             const juce::String& pluginPath,
                                             int timeoutMs = 25000,
                                             std::function<bool()> shouldCancel = {});

    /**
        父进程：起子进程扫描单个插件。

        P1-2：子进程起不来/返回空结果时如实报错，绝不退回同进程扫描；
        GUI 加载路径也不能在空结果时自行调用 scanInProcess。
    */
    static ChildResult scanInChild (const juce::File& selfExe,
                                    const juce::String& pluginPath,
                                    int timeoutMs,
                                    std::function<bool()> shouldCancel = {});

    /**
        同进程扫描，带崩溃兜底（Windows SEH）。

        仅供 EarLabPluginTests 的 --file 诊断路径使用（产品 GUI 不可达）：
        它能从访问违例里恢复继续扫下一个，但崩溃的插件已经污染了进程堆。
        P1-2 之后任何产品路径都不得调用这里。
    */
    static ChildResult scanInProcess (const juce::String& pluginPath);

    /** 找出目录下所有 .vst3 包（去重、排序）。 */
    static juce::StringArray enumerateBundles (const juce::StringArray& directories);

    /** 把单个插件扫描结果落盘再读回来（父子进程之间唯一的通道）。 */
    static bool writeResult (const juce::File& file, const std::vector<PluginInfo>& plugins);
    static bool readResult (const juce::File& file, std::vector<PluginInfo>& plugins);

    /**
        扫描临时目录：依次探测真正可写的位置。

        实测受限环境里子进程写不了 %TEMP%（父进程却能写），
        所以不能假设 temp 可用，必须写一次试一次。
    */
    static juce::File scanTempDirectory();
};

} // namespace earlab::plugin
