#pragma once

#include "PluginHost.h"

#include <array>
#include <vector>

namespace earlab::plugin
{

struct BandChange
{
    float centreHz = 0.0f;
    float deltaDb = 0.0f;
    float dryDb = 0.0f;
    float wetDb = 0.0f;
};

/**
    一次探针测量的结果。

    测量方法：同一段确定性白噪声分别过干/湿两条路径，做 1/3 倍频程能量比较。
    白噪声是平稳信号，因此插件引入的延迟不影响频谱比较——不需要对齐。
*/
struct ProbeResult
{
    bool ok = false;
    juce::String error;

    float broadbandDeltaDb = 0.0f;   // 20 Hz–20 kHz 总能量变化
    float maxAbsDeltaDb = 0.0f;      // 单个频段最大变化
    int dominantBand = 0;            // 变化最大的频段下标
    float dominantDeltaDb = 0.0f;

    std::vector<BandChange> bands;
};

class PluginProbe
{
public:
    static constexpr int fftOrder = 14;
    static constexpr int fftSize  = 1 << fftOrder;   // 16384 -> 2.93 Hz/bin

    /** 10 个 1/3 倍频程中心频率。 */
    static const std::array<float, 10>& bandCentres();

    static juce::String formatBandLabel (float hz);

    /** 用当前参数测一次。调用方负责 prepareToPlay。 */
    static ProbeResult measure (juce::AudioPluginInstance& instance, double sampleRate);

    /** 改一个参数再测，返回测量结果。 */
    static ProbeResult measureWithParameter (juce::AudioPluginInstance& instance,
                                             double sampleRate,
                                             int parameterIndex,
                                             float normalisedValue);

    /** 测量结果里变化最大的方向：+1 提升，-1 衰减。 */
    static int dominantDirection (const ProbeResult& result) noexcept;

    /** 是否为「整体响度变化」而不是某个频段变化。 */
    static bool isBroadband (const ProbeResult& result) noexcept;
};

} // namespace earlab::plugin
