// AC-01 的地基：K 加权响度计的正确性。
// 通过标准：
//   1 kHz 正弦在 -20 dBFS RMS 时读数 -20.0 LUFS（±0.2）
//   低频被 K 加权衰减
//   静音低于绝对门限
//   双声道等功率叠加 +3.01 dB

#include "dsp/LoudnessMeter.h"
#include "engine/MultiBandEq.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
int failures = 0;

void check (const char* name, bool ok, const std::string& detail = {})
{
    std::printf ("[%s] %-42s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    if (!ok)
        ++failures;
}

void expectNear (const char* name, double actual, double expected, double tolerance)
{
    char detail[192];
    std::snprintf (detail, sizeof (detail),
                   "actual=%.3f expected=%.3f tol=%.3f", actual, expected, tolerance);
    check (name, std::abs (actual - expected) <= tolerance, detail);
}

double measureSine (double dbfsRms, double frequency, double seconds, double sampleRate = 48000.0)
{
    const int numSamples = static_cast<int> (std::lround (sampleRate * seconds));

    juce::AudioBuffer<float> buffer (1, numSamples);
    const double amplitude = std::sqrt (2.0) * std::pow (10.0, dbfsRms / 20.0);

    for (int i = 0; i < numSamples; ++i)
        buffer.setSample (0, i, static_cast<float> (
            amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * frequency * i / sampleRate)));

    return earlab::dsp::LoudnessMeter::measureIntegrated (buffer, sampleRate, 1);
}
} // namespace

int main()
{
    std::printf ("EarLab loudness tests\n------------------------------\n");

    expectNear ("1 kHz -20 dBFS RMS -> -20 LUFS", measureSine (-20.0, 1000.0, 5.0), -20.0, 0.2);
    expectNear ("1 kHz -40 dBFS RMS -> -40 LUFS", measureSine (-40.0, 1000.0, 5.0), -40.0, 0.2);

    const double low = measureSine (-20.0, 60.0, 5.0);
    check ("K-weighting 衰减 60 Hz", low < -21.5, "60 Hz LUFS=" + std::to_string (low));

    juce::AudioBuffer<float> silence (1, 48000);
    silence.clear();
    const double silenceLufs = earlab::dsp::LoudnessMeter::measureIntegrated (silence, 48000.0, 1);
    check ("静音低于绝对门限", std::isinf (silenceLufs) && silenceLufs < 0.0);

    {
        const int numSamples = 48000 * 5;
        juce::AudioBuffer<float> stereo (2, numSamples);
        const double amplitude = std::sqrt (2.0) * std::pow (10.0, -20.0 / 20.0);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto v = static_cast<float> (
                amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / 48000.0));
            stereo.setSample (0, i, v);
            stereo.setSample (1, i, v);
        }

        // 两路相同信号：能量相加，读数比单声道高 3.01 dB。
        expectNear ("双声道等功率叠加 +3.01 dB",
                    earlab::dsp::LoudnessMeter::measureIntegrated (stereo, 48000.0, 2),
                    -16.99, 0.25);
    }

    // ---- 多段 EQ：幅频与处理
    {
        earlab::MultiBandEqProcessor eq;
        eq.prepare (48000.0, 512, 2);

        const auto flat = eq.getMagnitudeDb ({ 1000.0f }, 48000.0);
        check ("EQ 默认平坦", std::abs (flat.at (0)) < 0.01f,
               "mag=" + std::to_string (flat.at (0)));

        eq.setSingleBand (1000.0f, 6.0f, 1.0f);
        const auto peak = eq.getMagnitudeDb ({ 1000.0f }, 48000.0);
        expectNear ("EQ 峰值 +6 dB", peak.at (0), 6.0, 0.2);

        auto settings = eq.getSettings();
        settings.bands[0].enabled = false;
        settings.hpEnabled = true;
        settings.hpHz = 1000.0f;
        eq.applySettings (settings);

        const auto low = eq.getMagnitudeDb ({ 100.0f }, 48000.0).at (0);
        const auto high = eq.getMagnitudeDb ({ 5000.0f }, 48000.0).at (0);
        check ("EQ 高通衰减低频", low < -12.0f, "low=" + std::to_string (low));
        check ("EQ 高通保留高频", high > -1.0f, "high=" + std::to_string (high));

        eq.setSingleBand (2000.0f, -12.0f, 0.8f);

        juce::AudioBuffer<float> buffer (2, 256);
        juce::Random rng (7);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                buffer.setSample (ch, i, rng.nextFloat() * 0.2f - 0.1f);

        eq.process (buffer);

        bool finite = true;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
                finite = finite && std::isfinite (buffer.getSample (ch, i));

        check ("EQ 处理输出有限", finite);
    }

    std::printf ("------------------------------\n%s: %d 项失败\n",
                 failures == 0 ? "GREEN" : "RED", failures);
    return failures == 0 ? 0 : 1;
}
