#include "PluginProbe.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace earlab::plugin
{

namespace
{
constexpr int numWarmupBlocks  = 1;
constexpr int numMeasureBlocks = 4;
constexpr float noiseAmplitude = 0.12f;

struct FftWorkspace
{
    juce::dsp::FFT fft { PluginProbe::fftOrder };
    juce::dsp::WindowingFunction<float> window { PluginProbe::fftSize, juce::dsp::WindowingFunction<float>::hann };

    // performRealOnlyForwardTransform 需要 2 * fftSize 个 float
    std::vector<float> dry;
    std::vector<float> wet;

    FftWorkspace() : dry (static_cast<size_t> (PluginProbe::fftSize) * 2),
                     wet (static_cast<size_t> (PluginProbe::fftSize) * 2)
    {
    }

    /** 把一段样本搬进 FFT 缓冲、加窗、变换，返回每 bin 的功率。 */
    void transform (const float* samples, std::vector<float>& buffer, std::vector<float>& powerOut)
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        std::copy (samples, samples + PluginProbe::fftSize, buffer.begin());
        window.multiplyWithWindowingTable (buffer.data(), static_cast<size_t> (PluginProbe::fftSize));
        fft.performRealOnlyForwardTransform (buffer.data(), true);

        powerOut.assign (static_cast<size_t> (PluginProbe::fftSize / 2 + 1), 0.0f);

        for (size_t bin = 0; bin < powerOut.size(); ++bin)
        {
            const float re = buffer[bin * 2];
            const float im = buffer[bin * 2 + 1];
            powerOut[bin] = re * re + im * im;
        }
    }
};
} // namespace

const std::array<float, 10>& PluginProbe::bandCentres()
{
    static const std::array<float, 10> centres { 31.5f, 63.0f, 125.0f, 250.0f, 500.0f,
                                                 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };
    return centres;
}

juce::String PluginProbe::formatBandLabel (float hz)
{
    if (hz < 1000.0f)
        return juce::String (juce::roundToInt (hz)) + " Hz";

    return juce::String (hz / 1000.0f, 1) + " kHz";
}

int PluginProbe::dominantDirection (const ProbeResult& result) noexcept
{
    return result.dominantDeltaDb >= 0.0f ? 1 : -1;
}

bool PluginProbe::isBroadband (const ProbeResult& result) noexcept
{
    // 整体电平变化：宽带变化与最强频段变化接近，且低频段没有单独塌陷
    return std::abs (result.broadbandDeltaDb) >= 1.0f
        && std::abs (result.broadbandDeltaDb - result.dominantDeltaDb) <= 1.5f;
}

ProbeResult PluginProbe::measureWithParameter (juce::AudioPluginInstance& instance,
                                               double sampleRate,
                                               int parameterIndex,
                                               float normalisedValue)
{
    if (parameterIndex >= 0)
        PluginHost::setParameter (instance, parameterIndex, normalisedValue);

    return measure (instance, sampleRate);
}

ProbeResult PluginProbe::measure (juce::AudioPluginInstance& instance, double sampleRate)
{
    ProbeResult result;
    result.bands.resize (bandCentres().size());

    if (sampleRate <= 0.0)
    {
        result.error = utf8 ("采样率无效");
        return result;
    }

    const int numIn  = juce::jmax (1, instance.getTotalNumInputChannels());
    const int numOut = juce::jmax (1, instance.getTotalNumOutputChannels());
    const int numCh  = juce::jmax (numIn, numOut);

    juce::AudioBuffer<float> buffer (numCh, fftSize);
    juce::MidiBuffer midi;

    juce::Random rng (0x51ab7e1);
    FftWorkspace workspace;

    // 每个 bin 的累计功率，跨测量块与声道求平均
    std::vector<float> dryPower (static_cast<size_t> (fftSize / 2 + 1), 0.0f);
    std::vector<float> wetPower (static_cast<size_t> (fftSize / 2 + 1), 0.0f);
    std::vector<float> blockDry, blockWet;
    juce::AudioBuffer<float> dryCopy (numCh, fftSize);

    juce::ScopedNoDenormals noDenormals;
    instance.reset();

    // P1-1/P1-3：processBlock 的块不能超过 prepareToPlay 声明的 maxSamplesPerBlock。
    // 探针窗口是 fftSize（16384），宿主实例按 PluginHost::probeBlockSize（512）
    // prepare，所以这里按实例实际块大小切成视图喂给插件。
    const int processBlockSize = juce::jmax (1, instance.getBlockSize());

    const int totalBlocks = numWarmupBlocks + numMeasureBlocks;

    for (int block = 0; block < totalBlocks; ++block)
    {
        buffer.clear();

        // 确定性噪声（同一个 rng 序列贯穿全部块）
        for (int ch = 0; ch < numIn; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);

            for (int i = 0; i < fftSize; ++i)
                data[i] = rng.nextFloat() * 2.0f * noiseAmplitude - noiseAmplitude;
        }

        dryCopy.makeCopyOf (buffer, true);

        for (int offset = 0; offset < fftSize; offset += processBlockSize)
        {
            const int chunk = juce::jmin (processBlockSize, fftSize - offset);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(),
                                           numCh, offset, chunk);
            instance.processBlock (view, midi);
        }

        // 检查输出是否有效（NaN/Inf 说明插件拒绝了这次调用）
        bool finite = true;

        for (int ch = 0; ch < numOut && finite; ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);

            for (int i = 0; i < fftSize; ++i)
                if (! std::isfinite (data[i]))
                {
                    finite = false;
                    break;
                }
        }

        if (! finite)
        {
            result.error = utf8 ("插件输出包含 NaN/Inf");
            return result;
        }

        if (block < numWarmupBlocks)
            continue;

        for (int ch = 0; ch < juce::jmin (numOut, 2); ++ch)
        {
            const int sourceCh = juce::jmin (ch, numIn - 1);

            workspace.transform (dryCopy.getReadPointer (sourceCh), workspace.dry, blockDry);
            workspace.transform (buffer.getReadPointer (ch), workspace.wet, blockWet);

            for (size_t bin = 0; bin < dryPower.size(); ++bin)
            {
                dryPower[bin] += blockDry[bin];
                wetPower[bin] += blockWet[bin];
            }
        }
    }

    const double binHz = sampleRate / static_cast<double> (fftSize);
    const auto& centres = bandCentres();

    for (size_t band = 0; band < centres.size(); ++band)
    {
        const auto centre = static_cast<double> (centres[band]);
        const double lo = centre / std::pow (2.0, 1.0 / 6.0);
        const double hi = centre * std::pow (2.0, 1.0 / 6.0);

        double drySum = 0.0, wetSum = 0.0;

        for (size_t bin = 1; bin < dryPower.size(); ++bin)
        {
            const double hz = static_cast<double> (bin) * binHz;

            if (hz < lo || hz > hi)
                continue;

            drySum += dryPower[bin];
            wetSum += wetPower[bin];
        }

        const double eps = 1.0e-12;
        auto& change = result.bands[band];
        change.centreHz = centres[band];
        change.dryDb = static_cast<float> (10.0 * std::log10 (drySum + eps));
        change.wetDb = static_cast<float> (10.0 * std::log10 (wetSum + eps));
        change.deltaDb = change.wetDb - change.dryDb;
    }

    // 宽带：只在 20 Hz – 20 kHz 求和
    double dryBroad = 0.0, wetBroad = 0.0;

    for (size_t bin = 1; bin < dryPower.size(); ++bin)
    {
        const double hz = static_cast<double> (bin) * binHz;

        if (hz < 20.0 || hz > 20000.0)
            continue;

        dryBroad += dryPower[bin];
        wetBroad += wetPower[bin];
    }

    result.broadbandDeltaDb = static_cast<float> (10.0 * std::log10 ((wetBroad + 1.0e-12) / (dryBroad + 1.0e-12)));
    result.maxAbsDeltaDb = 0.0f;

    for (size_t band = 0; band < result.bands.size(); ++band)
    {
        if (std::abs (result.bands[band].deltaDb) > result.maxAbsDeltaDb)
        {
            result.maxAbsDeltaDb = std::abs (result.bands[band].deltaDb);
            result.dominantBand = static_cast<int> (band);
            result.dominantDeltaDb = result.bands[band].deltaDb;
        }
    }

    result.ok = true;
    return result;
}

} // namespace earlab::plugin
