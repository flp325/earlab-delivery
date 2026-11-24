#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace earlab
{

/** 单素材播放器：载入时重采样到引擎采样率，播放时只做内存拷贝，不碰磁盘。 */
class MaterialPlayer
{
public:
    /** 内置噪声源：没有素材时也能直接训练。 */
    enum class NoiseType { none, white, pink };

    bool load (const juce::File& file, double targetSampleRate);

    /**
        生成一段白噪 / 粉噪当素材（默认 10 秒，循环播放）。

        粉噪用 Kellet 三点滤波近似 −3 dB/oct；生成后统一归一到 −20 dBFS RMS，
        这样和普通素材的响度在同一量级，等响 trim 不会顶到 ±30 dB 边界。
    */
    bool startNoise (NoiseType type, double targetSampleRate, double seconds = 10.0);

    /** 采样率变化后按新速率重新载入同一文件（噪声则重新生成）；失败时保持原缓冲。 */
    bool reloadForSampleRate (double newSampleRate);

    void reset();
    void render (juce::AudioBuffer<float>& output, int numSamples, float gain);

    void setLooping (bool shouldLoop) noexcept { looping = shouldLoop; }
    [[nodiscard]] bool isLoaded() const noexcept { return buffer.getNumSamples() > 0; }
    [[nodiscard]] bool isNoise() const noexcept { return noiseType != NoiseType::none; }
    [[nodiscard]] NoiseType getNoiseType() const noexcept { return noiseType; }
    [[nodiscard]] double getDurationSeconds() const noexcept;
    [[nodiscard]] double getEngineSampleRate() const noexcept { return engineSampleRate; }
    [[nodiscard]] const juce::File& getSourceFile() const noexcept { return sourceFile; }

    [[nodiscard]] const juce::AudioBuffer<float>& getBuffer() const noexcept { return buffer; }
    [[nodiscard]] juce::String getFileName() const { return name; }

    static constexpr std::uint32_t noiseSeed = 20261023u;

private:
    juce::AudioBuffer<float> buffer;
    juce::File sourceFile;
    double engineSampleRate = 48000.0;
    double noiseSeconds = 10.0;
    int position = 0;
    bool looping = true;
    NoiseType noiseType = NoiseType::none;
    juce::String name;
};

} // namespace earlab
