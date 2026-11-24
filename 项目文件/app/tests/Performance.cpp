#include "engine/MultiBandEq.h"
#include "engine/MaterialPlayer.h"
#include "engine/CompressorProcessor.h"
#include "BaselineMultiBandEq.h"
#include "BaselineMaterialPlayer.h"
#include "BaselineCompressorProcessor.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace
{
volatile double sink = 0;
template <typename Work> double medianMs (Work work)
{
    std::array<double, 5> times {};
    work(); // Warm caches before timing.
    for (auto& time : times)
    {
        const auto start = std::chrono::steady_clock::now();
        work();
        time = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
    }
    std::sort (times.begin(), times.end());
    return times[2];
}
template <typename Eq> double curve()
{
    Eq eq;
    eq.prepare (48000, 512, 2);
    auto settings = eq.getSettings();
    for (auto& band : settings.bands) { band.enabled = true; band.gainDb = 6.0f; }
    eq.applySettings (settings);
    std::vector<float> frequencies (180);
    for (size_t i = 0; i < frequencies.size(); ++i)
        frequencies[i] = 20.0f * std::pow (1000.0f, static_cast<float> (i) / 179.0f);
    return medianMs ([&]
    {
        double total = 0;
        for (int i = 0; i < 3000; ++i)
            total += eq.getMagnitudeDb (frequencies, 48000).back();
        sink = total;
    });
}
template <typename Player> double playback()
{
    Player player;
    player.startNoise (Player::NoiseType::white, 48000, 1.0);
    juce::AudioBuffer<float> buffer (2, 512);
    return medianMs ([&]
    {
        double total = 0;
        for (int i = 0; i < 20000; ++i)
        {
            player.render (buffer, 512, 0.75f);
            total += buffer.getSample (0, 0);
        }
        sink = total;
    });
}
template <typename Comp> double neutral()
{
    Comp comp;
    comp.prepare (48000, 512, 2);
    comp.setNeutral();
    juce::AudioBuffer<float> buffer (2, 512);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample (ch, i, 0.1f);
    return medianMs ([&]
    {
        for (int i = 0; i < 10000; ++i)
            comp.process (buffer);
        sink = buffer.getSample (0, 0);
    });
}
void report (const char* label, double baseline, double current)
{
    std::printf ("%s: baseline=%.3f ms current=%.3f ms speedup=%.2fx\n",
                 label, baseline, current, baseline / current);
}
}
int main()
{
    report ("8-band EQ curve (3000 x 180 points)", curve<earlab::BaselineMultiBandEqProcessor>(), curve<earlab::MultiBandEqProcessor>());
    report ("Stereo playback (20000 x 512 samples)", playback<earlab::BaselineMaterialPlayer>(), playback<earlab::MaterialPlayer>());
    report ("Neutral compressor (10000 x 512 samples)", neutral<earlab::BaselineCompressorProcessor>(), neutral<earlab::CompressorProcessor>());
}
