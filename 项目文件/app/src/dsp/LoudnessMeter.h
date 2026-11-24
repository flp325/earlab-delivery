#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <limits>
#include <vector>

namespace earlab::dsp
{

/**
    ITU-R BS.1770-4 / EBU R128 响度计。

    只做「积分响度」与「短时响度」，用途单一：给训练题做等响对齐。
    干湿两路各测一次，差多少补多少，再叠加随机抖动防止用户靠响度作弊。

    48 kHz 直接使用标准给出的双二阶系数（逐位对齐 BS.1770）；
    其他采样率按 RBJ 原型重新推导，并自动把 1 kHz 增益校准回 0 dB，
    保证「1 kHz 正弦的读数等于它的 RMS」这条基本性质不随采样率漂移。
*/
class LoudnessMeter
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();

    void process (const juce::AudioBuffer<float>& buffer);

    /** 整段积分响度（LUFS）。样本不足或全静音时返回 -inf。 */
    [[nodiscard]] double getIntegratedLufs() const;

    /** 最近一个 400 ms 窗的短时响度（LUFS）。 */
    [[nodiscard]] double getMomentaryLufs() const;

    [[nodiscard]] int getBlockCount() const noexcept { return static_cast<int> (blockLoudness.size()); }

    /** 便捷函数：离线测量一整段音频。 */
    static double measureIntegrated (const juce::AudioBuffer<float>& buffer,
                                     double sampleRate,
                                     int numChannels);

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        double processSample (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void reset() noexcept { z1 = z2 = 0.0; }
    };

    static Biquad designHighShelf (double sampleRate);
    static Biquad designHighPass (double sampleRate);
    static double gainAtOneKilohertz (const Biquad& shelf, const Biquad& highPass, double sampleRate);

    [[nodiscard]] double lufsFromEnergy (double energy) const noexcept
    {
        return energy > 0.0 ? lufsOffset + 10.0 * std::log10 (energy)
                            : -std::numeric_limits<double>::infinity();
    }

    [[nodiscard]] double energyFromLufs (double lufs) const noexcept
    {
        return std::pow (10.0, (lufs - lufsOffset) / 10.0);
    }

    void pushSubBlock();

    double sr = 48000.0;
    double lufsOffset = -0.691;
    int channels = 2;
    int subBlockSamples = 4800;   // 100 ms
    int sinceSubBlock = 0;

    std::vector<Biquad> preFilter;
    std::vector<Biquad> rlbFilter;

    double subBlockEnergy = 0.0;
    std::vector<double> subBlockEnergies;   // 每 100 ms 一个
    std::vector<double> blockLoudness;      // 每 100 ms 一个 400 ms 窗

    JUCE_LEAK_DETECTOR (LoudnessMeter)
};

} // namespace earlab::dsp
