#include "MaterialPlayer.h"

#include <cmath>
#include <vector>

namespace earlab
{

namespace
{
bool resample (const juce::AudioBuffer<float>& source, double ratio, juce::AudioBuffer<float>& destination)
{
    const int count = source.getNumSamples();
    const double length = std::floor (count / ratio);
    if (count <= 0 || ! std::isfinite (ratio) || ratio <= 0.0
        || ! std::isfinite (length) || length > (1 << 28))
        return false;

    const int outputCount = juce::jmax (1, static_cast<int> (length));
    destination.setSize (source.getNumChannels(), outputCount);
    for (int ch = 0; ch < source.getNumChannels(); ++ch)
    {
        juce::LagrangeInterpolator interpolator;
        interpolator.process (ratio, source.getReadPointer (ch), destination.getWritePointer (ch),
                              outputCount, count, 0);
    }
    return true;
}
}

bool MaterialPlayer::load (const juce::File& file, double targetSampleRate)
{
    if (! file.existsAsFile())
        return false;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0
        || reader->numChannels == 0 || ! std::isfinite (reader->sampleRate)
        || reader->sampleRate <= 0.0)
        return false;

    const int srcChannels = juce::jlimit (1, 2, static_cast<int> (reader->numChannels));
    const int srcLength   = static_cast<int> (juce::jmin<juce::int64> (reader->lengthInSamples,
                                                                     static_cast<juce::int64> (1) << 28));

    juce::AudioBuffer<float> src (srcChannels, srcLength);
    src.clear();
    if (! reader->read (&src, 0, srcLength, 0, true, true))
        return false;

    const double nextRate = std::isfinite (targetSampleRate) && targetSampleRate > 0.0
                                ? targetSampleRate : 48000.0;

    const double ratio   = reader->sampleRate / nextRate;
    if (ratio == 1.0)
        buffer = std::move (src);
    else
    {
        juce::AudioBuffer<float> decoded;
        if (! resample (src, ratio, decoded))
            return false;
        buffer = std::move (decoded);
    }
    engineSampleRate = nextRate;
    name = file.getFileName();
    sourceFile = file;
    noiseType = NoiseType::none;
    position = 0;
    return true;
}

bool MaterialPlayer::startNoise (NoiseType type, double targetSampleRate, double seconds)
{
    if (type == NoiseType::none || ! std::isfinite (seconds) || seconds <= 0.0
        || ! std::isfinite (targetSampleRate) || targetSampleRate <= 0.0
        || seconds * targetSampleRate > (1 << 28))
        return false;

    engineSampleRate = targetSampleRate > 0.0 ? targetSampleRate : 48000.0;
    noiseSeconds = seconds;

    const int numSamples = juce::jmax (1, static_cast<int> (std::ceil (seconds * engineSampleRate)));
    std::vector<float> mono (static_cast<size_t> (numSamples), 0.0f);

    juce::Random rng (noiseSeed);

    if (type == NoiseType::white)
    {
        for (auto& sample : mono)
            sample = rng.nextFloat() * 2.0f - 1.0f;
    }
    else
    {
        // Paul Kellet 的经济型粉噪近似（三点一阶滤波，接近 −3 dB/oct）
        double b0 = 0.0, b1 = 0.0, b2 = 0.0;

        for (auto& sample : mono)
        {
            const double white = static_cast<double> (rng.nextFloat()) * 2.0 - 1.0;
            b0 = 0.99765 * b0 + white * 0.0990460;
            b1 = 0.96300 * b1 + white * 0.2965164;
            b2 = 0.57000 * b2 + white * 1.0526913;
            sample = static_cast<float> (b0 + b1 + b2 + white * 0.1848);
        }
    }

    // 归一到 -20 dBFS RMS：和普通素材同一量级，等响 trim 不会顶到 ±30 dB 边界
    double sumSquares = 0.0;

    for (const auto sample : mono)
        sumSquares += static_cast<double> (sample) * sample;

    const double rms = std::sqrt (sumSquares / static_cast<double> (numSamples));
    const float gain = rms > 1.0e-9 ? static_cast<float> (0.1 / rms) : 0.0f;

    buffer.setSize (2, numSamples);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample (ch, i, juce::jlimit (-1.0f, 1.0f,
                                                   mono[static_cast<size_t> (i)] * gain));

    sourceFile = juce::File();
    noiseType = type;
    name = type == NoiseType::white ? juce::String::fromUTF8 ("白噪声")
                                    : juce::String::fromUTF8 ("粉噪声");
    position = 0;
    return true;
}

bool MaterialPlayer::reloadForSampleRate (double newSampleRate)
{
    if (noiseType != NoiseType::none)
        return startNoise (noiseType, newSampleRate, noiseSeconds);

    if (sourceFile == juce::File() || ! std::isfinite (newSampleRate) || newSampleRate <= 0.0)
        return false;

    if (load (sourceFile, newSampleRate))
        return true;

    // Removable media may have disappeared since loading. The decoded material is still available.
    juce::AudioBuffer<float> converted;
    if (! resample (buffer, engineSampleRate / newSampleRate, converted))
        return false;
    buffer = std::move (converted);
    engineSampleRate = newSampleRate;
    position = 0;
    return true;
}

void MaterialPlayer::reset()
{
    position = 0;
}

double MaterialPlayer::getDurationSeconds() const noexcept
{
    return engineSampleRate > 0.0
         ? static_cast<double> (buffer.getNumSamples()) / engineSampleRate
         : 0.0;
}

void MaterialPlayer::render (juce::AudioBuffer<float>& output, int numSamples, float gain)
{
    output.clear();

    if (buffer.getNumSamples() == 0 || numSamples <= 0)
        return;

    const int total = buffer.getNumSamples();
    const int outCh = output.getNumChannels();
    const int srcCh = buffer.getNumChannels();

    if (outCh <= 0)
        return;

    // Copy contiguous spans; never repeat the last sample after non-looping EOF.
    const int requested = juce::jlimit (0, output.getNumSamples(), numSamples);
    int written = 0;
    while (written < requested)
    {
        if (position >= total)
        {
            if (! looping)
                break;
            position = 0;
        }
        const int count = juce::jmin (requested - written, total - position);
        for (int ch = 0; ch < outCh; ++ch)
            output.copyFrom (ch, written, buffer.getReadPointer (juce::jmin (ch, srcCh - 1)) + position,
                             count, gain);
        position += count;
        written += count;
    }
}

} // namespace earlab
