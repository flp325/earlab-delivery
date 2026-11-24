#pragma once

#include "BuiltinChain.h"
#include "DualChain.h"
#include "LoudnessMatcher.h"
#include "MaterialPlayer.h"
#include "MultiBandEq.h"
#include "plugin/PluginTrainableProcessor.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <memory>

namespace earlab
{

/**
    音频引擎：设备回调 + 素材播放 + 双链 + 内置处理链 + 等响 trim。

    线程约定：
    - 音频线程只做 DSP，不分配、不加锁、不碰 UI；
    - 所有会改处理器状态的消息线程操作（换素材、换插件、离线测响度）
      一律先 AudioSuspension 摘掉音频回调，等音频线程退出后再改；
    - 纯参数写入（EQ 频段）走 MultiBandEq 的不可变快照，可以在播放中安全进行。
*/
class AudioEngine final : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    [[nodiscard]] juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    bool loadFile (const juce::File& file);

    /** 没有素材时用内置噪声训练（白噪 / 粉噪）。 */
    bool loadNoise (MaterialPlayer::NoiseType type);

    void setPlaying (bool shouldPlay) noexcept { playing.store (shouldPlay); }
    [[nodiscard]] bool isPlaying() const noexcept { return playing.load(); }

    void setSource (DualChain::Source source) { chain.setSource (source); }
    [[nodiscard]] DualChain::Source getSource() const noexcept { return chain.getSource(); }

    void setEqParameters (float frequencyHz, float gainDb, float q);

    /**
        开/关高低切。

        P1-3：压缩题开始前必须关闭——calibrateThresholdForTargetGr 用原始素材校准，
        而实际湿链经过 EQ，保留 HP/LP 会让正确答案与听到的压缩量不一致。
    */
    void setHighLowPassEnabled (bool shouldBeEnabled);

    void setCompressorNeutral();
    float configureCompressorForTarget (float targetGrDb);

    /** 多段 EQ：读取/应用完整设置（消息线程）。应用是快照发布，播放中安全。 */
    [[nodiscard]] MultiBandEqProcessor::Settings getEqSettings() const;
    void applyEqSettings (const MultiBandEqProcessor::Settings& settings);
    [[nodiscard]] const MultiBandEqProcessor& getEq() const noexcept { return builtin.getEq(); }

    /**
        用第三方插件替换湿链处理器。

        切换时摘掉音频回调，避免音频线程拿到正在析构的处理器；
        播放本身会被停止（调用方负责重新开始）。
    */
    bool loadPluginTask (const plugin::PluginInfo& info,
                         const plugin::PluginTask& task,
                         juce::String& error);
    bool updatePluginTask (const plugin::PluginTask& task);
    void unloadPlugin();
    [[nodiscard]] bool hasPlugin() const noexcept { return pluginProcessor != nullptr; }

    void refreshLoudnessMatch();

    /** 采样率变了、素材还是旧速率时置位；由 UI 定时器回调到消息线程处理。 */
    [[nodiscard]] bool hasPendingMaterialReload() const noexcept
    {
        return pendingMaterialReload.load (std::memory_order_relaxed);
    }

    /** 按当前设备采样率重新载入素材并重算等响（消息线程）。 */
    bool reloadMaterialForSampleRate();

    [[nodiscard]] LoudnessMatch getMatch() const;

    /** 当前实际作用在干/湿两路上的 trim（dB），用于回归测试与状态核对。 */
    [[nodiscard]] std::pair<float, float> getAppliedTrimsDb() const;
    [[nodiscard]] float getCompressorGainReductionDb() const { return builtin.getComp().getGainReductionDb(); }
    [[nodiscard]] juce::String getMaterialName() const { return player.getFileName(); }
    [[nodiscard]] double getSampleRate() const noexcept { return currentSampleRate; }
    [[nodiscard]] bool hasMaterial() const noexcept { return player.isLoaded(); }

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    /** 摘掉音频回调并在作用域结束时恢复；可嵌套，内层不重复操作。 */
    class AudioSuspension
    {
    public:
        explicit AudioSuspension (AudioEngine& engineToUse) : engine (engineToUse)
        {
            engine.suspendAudio();
        }

        ~AudioSuspension()
        {
            engine.resumeAudio();
        }

        AudioSuspension (const AudioSuspension&) = delete;
        AudioSuspension& operator= (const AudioSuspension&) = delete;

    private:
        AudioEngine& engine;
    };

    void suspendAudio();
    void resumeAudio();

    /** 必须在音频回调已摘除时调用。 */
    void refreshLoudnessMatchLocked();

    juce::AudioDeviceManager deviceManager;
    MaterialPlayer player;
    BuiltinChain builtin;
    DualChain chain;

    std::atomic<bool> playing { false };
    std::atomic<bool> pendingMaterialReload { false };
    int suspendDepth = 0;
    bool playingBeforeSuspend = false;

    double currentSampleRate = 48000.0;
    int currentBlockSize = 512;

    // 记录上次 prepare 的参数：恢复回调时不应重建处理器（会清空滤波/插件状态）
    double preparedSampleRate = 0.0;
    int preparedBlockSize = 0;

    std::unique_ptr<plugin::PluginTrainableProcessor> pluginProcessor;

    juce::AudioBuffer<float> scratch;
    mutable juce::CriticalSection matchLock;
    LoudnessMatch match;
};

} // namespace earlab
