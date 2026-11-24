#include "LoudnessMeter.h"

#include <cmath>
#include <complex>

namespace earlab::dsp
{

namespace
{
constexpr double kAbsoluteGate  = -70.0;   // LUFS
constexpr double kRelativeGate  = -10.0;   // LU
constexpr double kItuOffset     = -0.691;  // BS.1770 的校准常数
} // namespace

//==============================================================================
// 非 48 kHz 时的兜底设计。48 kHz 走标准系数，不走这里。
LoudnessMeter::Biquad LoudnessMeter::designHighShelf (double sampleRate)
{
    constexpr double f0     = 1681.974450955533;
    constexpr double gainDb = 3.999843853973347;
    constexpr double q      = 0.7071752369554196;

    const double A     = std::pow (10.0, gainDb / 40.0);
    const double w0    = 2.0 * juce::MathConstants<double>::pi * f0 / sampleRate;
    const double cosw  = std::cos (w0);
    const double sinw  = std::sin (w0);
    const double alpha = sinw / (2.0 * q);
    const double sqrtA = std::sqrt (A);
    const double a0    = (A + 1.0) - (A - 1.0) * cosw + 2.0 * sqrtA * alpha;

    Biquad bq;
    bq.b0 = A * ((A + 1.0) + (A - 1.0) * cosw + 2.0 * sqrtA * alpha) / a0;
    bq.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw) / a0;
    bq.b2 = A * ((A + 1.0) + (A - 1.0) * cosw - 2.0 * sqrtA * alpha) / a0;
    bq.a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw) / a0;
    bq.a2 = ((A + 1.0) - (A - 1.0) * cosw - 2.0 * sqrtA * alpha) / a0;
    return bq;
}

LoudnessMeter::Biquad LoudnessMeter::designHighPass (double sampleRate)
{
    constexpr double f0 = 38.13547087602444;
    constexpr double q  = 0.5003270373238773;

    const double w0    = 2.0 * juce::MathConstants<double>::pi * f0 / sampleRate;
    const double cosw  = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * q);
    const double a0    = 1.0 + alpha;

    Biquad bq;
    bq.b0 = (1.0 + cosw) * 0.5 / a0;
    bq.b1 = -(1.0 + cosw) / a0;
    bq.b2 = (1.0 + cosw) * 0.5 / a0;
    bq.a1 = -2.0 * cosw / a0;
    bq.a2 = (1.0 - alpha) / a0;
    return bq;
}

double LoudnessMeter::gainAtOneKilohertz (const Biquad& shelf,
                                          const Biquad& highPass,
                                          double sampleRate)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * 1000.0 / sampleRate;
    const std::complex<double> z = std::polar (1.0, -w);

    const auto response = [&z] (const Biquad& bq)
    {
        return (bq.b0 + bq.b1 * z + bq.b2 * z * z)
             / (1.0 + bq.a1 * z + bq.a2 * z * z);
    };

    return 20.0 * std::log10 (std::abs (response (shelf) * response (highPass)));
}

//==============================================================================
void LoudnessMeter::prepare (double sampleRate, int numChannels)
{
    sr       = sampleRate > 0.0 ? sampleRate : 48000.0;
    channels = juce::jmax (1, numChannels);

    subBlockSamples = juce::jmax (1, static_cast<int> (std::lround (sr * 0.1)));

    const bool isStandardRate = std::abs (sr - 48000.0) < 0.5;

    if (isStandardRate)
    {
        // ITU-R BS.1770-4 在 48 kHz 给出的标准系数
        Biquad shelf;
        shelf.b0 = 1.53512485958697;
        shelf.b1 = -2.69169618940638;
        shelf.b2 = 1.19839281085285;
        shelf.a1 = -1.69065929318241;
        shelf.a2 = 0.73248077421585;

        Biquad highPass;
        highPass.b0 = 1.0;
        highPass.b1 = -2.0;
        highPass.b2 = 1.0;
        highPass.a1 = -1.99004745483398;
        highPass.a2 = 0.99007225036621;

        preFilter.assign (static_cast<size_t> (channels), shelf);
        rlbFilter.assign (static_cast<size_t> (channels), highPass);
        lufsOffset = kItuOffset;
    }
    else
    {
        preFilter.assign (static_cast<size_t> (channels), designHighShelf (sr));
        rlbFilter.assign (static_cast<size_t> (channels), designHighPass (sr));

        // 1 kHz 增益校准：保证「1 kHz 正弦的读数等于其 RMS」
        lufsOffset = -gainAtOneKilohertz (preFilter.front(), rlbFilter.front(), sr);
    }

    reset();
}

void LoudnessMeter::reset()
{
    for (auto& bq : preFilter) bq.reset();
    for (auto& bq : rlbFilter) bq.reset();

    subBlockEnergy = 0.0;
    sinceSubBlock = 0;
    subBlockEnergies.clear();
    blockLoudness.clear();
}

void LoudnessMeter::process (const juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh      = juce::jmin (buffer.getNumChannels(), channels);

    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            if (ch == 3)   // LFE 不计入响度
                continue;

            const double x = static_cast<double> (buffer.getSample (ch, i));
            const double y = rlbFilter[static_cast<size_t> (ch)]
                                 .processSample (preFilter[static_cast<size_t> (ch)].processSample (x));
            subBlockEnergy += y * y;
        }

        if (++sinceSubBlock >= subBlockSamples)
            pushSubBlock();
    }
}

void LoudnessMeter::pushSubBlock()
{
    subBlockEnergies.push_back (subBlockEnergy / static_cast<double> (subBlockSamples));
    subBlockEnergy = 0.0;
    sinceSubBlock  = 0;

    if (subBlockEnergies.size() >= 4)
    {
        const size_t n = subBlockEnergies.size();
        const double e = (subBlockEnergies[n - 1] + subBlockEnergies[n - 2]
                          + subBlockEnergies[n - 3] + subBlockEnergies[n - 4]) * 0.25;
        blockLoudness.push_back (lufsFromEnergy (e));
    }
}

double LoudnessMeter::getMomentaryLufs() const
{
    return blockLoudness.empty() ? -std::numeric_limits<double>::infinity()
                                 : blockLoudness.back();
}

double LoudnessMeter::getIntegratedLufs() const
{
    if (blockLoudness.empty())
        return -std::numeric_limits<double>::infinity();

    double sumAbsolute = 0.0;
    int    countAbsolute = 0;

    for (const double lufs : blockLoudness)
    {
        if (lufs > kAbsoluteGate)
        {
            sumAbsolute += energyFromLufs (lufs);
            ++countAbsolute;
        }
    }

    if (countAbsolute == 0)
        return -std::numeric_limits<double>::infinity();

    const double relativeThreshold = lufsFromEnergy (sumAbsolute / countAbsolute) + kRelativeGate;

    double sum = 0.0;
    int    count = 0;

    for (const double lufs : blockLoudness)
    {
        if (lufs > kAbsoluteGate && lufs > relativeThreshold)
        {
            sum += energyFromLufs (lufs);
            ++count;
        }
    }

    return count > 0 ? lufsFromEnergy (sum / count)
                     : -std::numeric_limits<double>::infinity();
}

double LoudnessMeter::measureIntegrated (const juce::AudioBuffer<float>& buffer,
                                         double sampleRate,
                                         int numChannels)
{
    LoudnessMeter meter;
    meter.prepare (sampleRate, numChannels);
    meter.process (buffer);
    return meter.getIntegratedLufs();
}

} // namespace earlab::dsp
