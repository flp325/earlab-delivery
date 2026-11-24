#pragma once

#include "PluginCatalog.h"

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <memory>
#include <vector>

namespace earlab::plugin
{

/** 宿主侧看到的一个自动化参数。 */
struct HostParameter
{
    int index = -1;
    juce::String name;
    juce::String id;
    juce::String label;
    float defaultValue = 0.0f;   // 归一化
    float currentValue = 0.0f;   // 归一化
    bool isDiscrete = false;
    int numSteps = 0;
    bool isBypassLike = false;   // 名字像旁通/混音比，出题时排除
    bool isMetaParameter = false;
};

/**
    插件宿主：负责实例化、参数读写。

    注意：VST3 实例必须在具备 MessageManager 的环境里创建，
    控制台测试要先 ScopedJuceInitialiser_GUI。
*/
class PluginHost
{
public:
    PluginHost() = default;

    /**
        预检与正式加载共用的插件探测配置（P1-3）。

        PluginScanRunner 的预检子进程和 GUI/测试的离线探针实例必须用同一套
        采样率与块大小；否则「预检通过」并不代表正式加载的配置能通过
        （P0-1 就是预检 512、加载 16384 的缝隙里漏出来的）。
    */
    static constexpr double probeSampleRate = 48000.0;
    static constexpr int probeBlockSize = 512;

    /**
        P0-1 回归：预热首帧需要的声道数。

        多输出插件（如 0-in/64-out 音源）会按输出 bus 数量写 buffer 的
        channels[0..out-1]；只按输入声道分配会让插件越界写宿主内存。
        JUCE 宿主侧至少需要 2 个声道（单声道插件也按 stereo 准备）。
    */
    [[nodiscard]] static int preheatChannelCount (int numInputs, int numOutputs) noexcept;

    /**
        P0-1 回归：插件实例报告 0/0 声道时按扫描元数据兜底，再算预热声道数。
    */
    [[nodiscard]] static int resolvedPreheatChannelCount (int instanceInputs,
                                                          int instanceOutputs,
                                                          int metadataInputs,
                                                          int metadataOutputs) noexcept;

    /**
        按目录里的条目加载一个实例；失败返回 nullptr 并写 error。

        创建后会 prepareToPlay + reset + 跑一个静音首帧（见 prepareAndWarmUp）。
    */
    static std::unique_ptr<juce::AudioPluginInstance> create (const PluginInfo& info,
                                                              double sampleRate,
                                                              int blockSize,
                                                              juce::String& error);

    /**
        prepareToPlay + reset + 静音首帧 processBlock。

        P0-1：缓冲声道数 = resolvedPreheatChannelCount（必须覆盖插件输出），
        预检与正式加载共用同一份缓冲构造逻辑，避免两处配置漂移。
        metadata* 用于插件实例报告 0/0 时的兜底。
        返回首帧输出是否全部有限（NaN/Inf 表示插件拒绝了这次调用）。
    */
    static bool prepareAndWarmUp (juce::AudioPluginInstance& instance,
                                  double sampleRate,
                                  int blockSize,
                                  int metadataInputs,
                                  int metadataOutputs,
                                  int warmUpSamples = 32);

    /**
        P1-3 回归：预检与正式加载共用的类型选择规则。

        第一个非音源；全是音源时退回第一个；空列表返回 -1。
    */
    [[nodiscard]] static int chooseLoadableIndex (const std::vector<PluginInfo>& plugins) noexcept;

    static std::vector<HostParameter> listParameters (juce::AudioPluginInstance& instance);

    static bool setParameter (juce::AudioPluginInstance& instance, int index, float normalisedValue);

    static void setAllToDefault (juce::AudioPluginInstance& instance);

    /** 让插件把当前参数状态发回宿主（写档/刷新 UI 用）。 */
    static void refreshState (juce::AudioPluginInstance& instance);

    /** 适合出题的参数：连续、非旁通、非元参数，按启发式排序靠前的优先。 */
    static std::vector<HostParameter> trainingCandidates (const std::vector<HostParameter>& all, int maxCount);

    /**
        激活与某个参数同组的开关。

        典型 EQ 的频段默认没启用（Pro-Q 的 "Band 1 Used"），
        只改增益不会有任何效果。这里按名字前缀找到同组的
        Used/Enabled/On/Active 并置 1。返回被打开的数量。
    */
    static std::vector<std::pair<int, float>> activateRelatedSwitches (
        juce::AudioPluginInstance& instance,
        const std::vector<HostParameter>& all,
        int parameterIndex);
};

} // namespace earlab::plugin
