#pragma once

#include "TrainableProcessor.h"
#include "../util/LatestSnapshot.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <vector>

namespace earlab
{

/**
    8 段参数 EQ + 高低切。

    湿链用这个替代单段峰值滤波器：训练时只启用一段，
    手动试听时可以八段全开，配上频响曲线与实时频谱。

    线程模型：消息线程写、音频线程读，零锁。
    - 所有参数（含 enabled）先打包进一个不可变 CoeffSet，再整页发布；
    - 最新值邮箱在写者、读者与交换槽间转移页面所有权；
    - 消费者持有页面期间，任意多次发布均不会覆盖它，不依赖 UI 更新频率。
*/
class MultiBandEqProcessor final : public ITrainableProcessor
{
public:
    static constexpr int numBands = 8;

    struct Band
    {
        float frequency = 1000.0f;
        float gainDb = 0.0f;
        float q = 1.0f;
        bool enabled = false;
    };

    struct Settings
    {
        Band bands[numBands];
        bool hpEnabled = false;
        float hpHz = 30.0f;
        bool lpEnabled = false;
        float lpHz = 18000.0f;

        /** 默认把 8 段铺开在不同频率上（全关），方便在曲线上直接抓取/新建。 */
        Settings();
    };

    juce::String getName() const override { return "EarLab 8-Band EQ"; }
    int getLatencySamples() const override { return 0; }

    void prepare (double sampleRate, int maximumBlockSize, int numChannels) override;
    void reset() override;
    void process (juce::AudioBuffer<float>& buffer) override;

    void applySettings (const Settings& newSettings);
    [[nodiscard]] Settings getSettings() const;

    /** 训练用：只有第 0 段生效，其余频段归零（高低切保持不变）。 */
    void setSingleBand (float frequencyHz, float gainDb, float q);

    /**
        开/关高低切，保留其它设置。

        P1-3：压缩题必须显式关闭高低切——校准用原始素材，实际湿链却会先过滤，
        保留高低切会让正确答案与实际听到的压缩量不一致。
    */
    void setHighLowPassEnabled (bool shouldBeEnabled);

    /** 当前设置下的幅频响应（dB），用于画曲线。 */
    [[nodiscard]] std::vector<float> getMagnitudeDb (const std::vector<float>& frequencies,
                                                     double sampleRate) const;

    [[nodiscard]] double getSampleRate() const noexcept { return sr; }

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    struct CoeffSet
    {
        Biquad bands[numBands];
        bool bandOn[numBands] {};
        Biquad hp, lp;
        bool hpOn = false, lpOn = false;
    };

    struct ChannelState
    {
        double z1[numBands + 2] {};
        double z2[numBands + 2] {};
    };

    void rebuildCoefficients();

    [[nodiscard]] CoeffSet makeCoefficients (double sampleRate) const;

    double sr = 48000.0;
    int channels = 2;

    Settings settings;
    LatestSnapshot<CoeffSet> coefficients;
    std::vector<ChannelState> channelState;
};

} // namespace earlab
