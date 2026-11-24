#include "AudioEngine.h"

#include "CompressorCalibration.h"

#include <algorithm>

namespace
{
} // namespace

namespace earlab
{

AudioEngine::AudioEngine()
{
    deviceManager.initialise (0, 2, nullptr, true);

    builtin.getEq().setSingleBand (1000.0f, 0.0f, 1.0f);
    builtin.getComp().setNeutral();

    chain.setProcessor (&builtin);
    chain.prepare (48000.0, 512, 2);

    deviceManager.addAudioCallback (this);

    // 训练引擎固定 48 kHz、256 采样；设备不支持时会自动回退
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.sampleRate = 48000.0;
    setup.bufferSize = 256;
    deviceManager.setAudioDeviceSetup (setup, true);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    currentSampleRate = device != nullptr ? device->getCurrentSampleRate() : 48000.0;

    if (currentSampleRate <= 0.0)
        currentSampleRate = 48000.0;

    const int block = juce::jmax (64, device != nullptr ? device->getCurrentBufferSizeSamples() : 512);
    currentBlockSize = block;

    scratch.setSize (2, block);

    // 恢复回调（remove/addAudioCallback）也会走到这里：只有设备参数真的变了才重建，
    // 否则每次 EQ 微调都会清空滤波状态、重跑插件 prepareToPlay。
    if (currentSampleRate != preparedSampleRate || block != preparedBlockSize)
    {
        preparedSampleRate = currentSampleRate;
        preparedBlockSize = block;
        chain.prepare (currentSampleRate, block, 2);
    }

    // 关键：恢复回调不能把已测好的等响 trim 清零
    {
        const juce::ScopedLock sl (matchLock);
        chain.setTrims (match.valid ? match.dryTrimDb : 0.0f,
                        match.valid ? match.wetTrimDb : 0.0f);
    }

    // 采样率变了但素材还是旧速率：交给消息线程重新载入（这里不能摘回调，会死锁）
    if (player.isLoaded() && std::abs (player.getEngineSampleRate() - currentSampleRate) > 0.5)
        pendingMaterialReload.store (true, std::memory_order_relaxed);
}

void AudioEngine::suspendAudio()
{
    if (suspendDepth++ > 0)
        return;

    playingBeforeSuspend = playing.load();
    playing.store (false, std::memory_order_relaxed);
    deviceManager.removeAudioCallback (this);
}

void AudioEngine::resumeAudio()
{
    if (suspendDepth <= 0)
        return;

    if (--suspendDepth > 0)
        return;

    deviceManager.addAudioCallback (this);
    playing.store (playingBeforeSuspend, std::memory_order_relaxed);
}


void AudioEngine::audioDeviceStopped()
{
    playing.store (false);
}

bool AudioEngine::loadFile (const juce::File& file)
{
    playing.store (false);

    // 解码/重采样会重分配 material buffer，必须等音频线程退出后再做
    AudioSuspension suspend (*this);

    if (! player.load (file, currentSampleRate))
        return false;

    pendingMaterialReload.store (false, std::memory_order_relaxed);
    refreshLoudnessMatchLocked();
    return true;
}

bool AudioEngine::loadNoise (MaterialPlayer::NoiseType type)
{
    playing.store (false);

    AudioSuspension suspend (*this);

    if (! player.startNoise (type, currentSampleRate))
        return false;

    pendingMaterialReload.store (false, std::memory_order_relaxed);
    refreshLoudnessMatchLocked();
    return true;
}

bool AudioEngine::reloadMaterialForSampleRate()
{
    if (! player.isLoaded())
    {
        pendingMaterialReload.store (false, std::memory_order_relaxed);
        return true;
    }

    AudioSuspension suspend (*this);

    pendingMaterialReload.store (false, std::memory_order_relaxed);
    if (player.reloadForSampleRate (currentSampleRate))
    {
        refreshLoudnessMatchLocked();
        return true;
    }

    // A stale-rate buffer would trigger another reload as soon as the callback resumes.
    player = MaterialPlayer {};
    playingBeforeSuspend = false;
    const juce::ScopedLock lock (matchLock);
    match = {};
    chain.setTrims (0.0f, 0.0f);
    return false;
}

void AudioEngine::setEqParameters (float frequencyHz, float gainDb, float q)
{
    builtin.getEq().setSingleBand (frequencyHz, gainDb, q);
}

void AudioEngine::setHighLowPassEnabled (bool shouldBeEnabled)
{
    builtin.getEq().setHighLowPassEnabled (shouldBeEnabled);
}

MultiBandEqProcessor::Settings AudioEngine::getEqSettings() const
{
    return builtin.getEq().getSettings();
}

void AudioEngine::applyEqSettings (const MultiBandEqProcessor::Settings& settings)
{
    builtin.getEq().applySettings (settings);
}

void AudioEngine::setCompressorNeutral()
{
    builtin.getComp().setNeutral();
}

float AudioEngine::configureCompressorForTarget (float targetGrDb)
{
    if (! player.isLoaded())
        return 0.0f;

    // 校准要反复 reset/process 压缩器，必须独占
    AudioSuspension suspend (*this);

    auto& comp = builtin.getComp();

    CompressorProcessor::Params params;
    params.ratio      = 4.0f;
    params.attackMs   = 10.0f;
    params.releaseMs  = 100.0f;
    params.kneeDb     = 6.0f;
    params.makeupDb   = 0.0f;

    params.thresholdDb = calibrateThresholdForTargetGr (comp, player.getBuffer(),
                                                        currentSampleRate, params, targetGrDb);

    comp.setParameters (params);

    // 复测真实 GR，用于反馈文案
    const int maxSamples = juce::jmin (player.getBuffer().getNumSamples(),
                                       static_cast<int> (currentSampleRate * 5.0));
    juce::AudioBuffer<float> working (player.getBuffer().getNumChannels(), maxSamples);

    for (int ch = 0; ch < player.getBuffer().getNumChannels(); ++ch)
        working.copyFrom (ch, 0, player.getBuffer(), ch, 0, maxSamples);

    comp.reset();
    comp.resetMetering();
    comp.process (working);

    return comp.getAverageGainReductionDb();
}

void AudioEngine::refreshLoudnessMatch()
{
    AudioSuspension suspend (*this);
    refreshLoudnessMatchLocked();
}

void AudioEngine::refreshLoudnessMatchLocked()
{
    if (! player.isLoaded())
        return;

    // 等响必须按当前真正在处理声音的处理器测，而不是永远测内置链
    ITrainableProcessor& active = pluginProcessor != nullptr
                                      ? static_cast<ITrainableProcessor&> (*pluginProcessor)
                                      : static_cast<ITrainableProcessor&> (builtin);

    auto result = computeLoudnessMatch (active, player.getBuffer(), currentSampleRate);

    {
        const juce::ScopedLock sl (matchLock);
        match = result;
    }

    // 测量无效时必须归零：否则换新素材后会沿用上一份素材的 ±30 dB trim
    chain.setTrims (result.valid ? result.dryTrimDb : 0.0f,
                    result.valid ? result.wetTrimDb : 0.0f);
}

bool AudioEngine::loadPluginTask (const plugin::PluginInfo& info,
                                  const plugin::PluginTask& task,
                                  juce::String& error)
{
    playing.store (false);

    // 实例化、prepareToPlay、等响测量全程独占音频回调
    AudioSuspension suspend (*this);

    auto instance = plugin::PluginHost::create (info, currentSampleRate, currentBlockSize, error);

    if (instance == nullptr)
        return false;

    pluginProcessor = std::make_unique<plugin::PluginTrainableProcessor> (std::move (instance), task);

    chain.setProcessor (pluginProcessor.get());
    chain.prepare (currentSampleRate, currentBlockSize, 2);
    chain.setTrims (0.0f, 0.0f);

    refreshLoudnessMatchLocked();
    return true;
}

bool AudioEngine::updatePluginTask (const plugin::PluginTask& task)
{
    if (pluginProcessor == nullptr || playing.load())
        return false;

    AudioSuspension suspend (*this);

    pluginProcessor->setTask (task);
    chain.prepare (currentSampleRate, currentBlockSize, 2);
    refreshLoudnessMatchLocked();
    return true;
}

void AudioEngine::unloadPlugin()
{
    if (pluginProcessor == nullptr)
        return;

    playing.store (false);

    AudioSuspension suspend (*this);

    pluginProcessor.reset();
    chain.setProcessor (&builtin);
    chain.prepare (currentSampleRate, currentBlockSize, 2);
    chain.setTrims (0.0f, 0.0f);
    refreshLoudnessMatchLocked();
}

LoudnessMatch AudioEngine::getMatch() const
{
    const juce::ScopedLock sl (matchLock);
    return match;
}

std::pair<float, float> AudioEngine::getAppliedTrimsDb() const
{
    constexpr float floorDb = -100.0f;

    return { juce::Decibels::gainToDecibels (chain.getDryGain(), floorDb),
             juce::Decibels::gainToDecibels (chain.getWetGain(), floorDb) };
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused (inputChannelData, numInputChannels, context);

    const bool silence = (! playing.load())
                      || numSamples <= 0
                      || numOutputChannels <= 0
                      || scratch.getNumSamples() < numSamples;

    if (silence)
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

        return;
    }

    juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(),
                                   scratch.getNumChannels(), 0, numSamples);

    player.render (view, numSamples, 1.0f);
    chain.process (view);

    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        if (outputChannelData[ch] == nullptr)
            continue;

        const int sourceChannel = juce::jmin (ch, view.getNumChannels() - 1);
        juce::FloatVectorOperations::copy (outputChannelData[ch],
                                           view.getReadPointer (sourceChannel),
                                           numSamples);
    }
}

} // namespace earlab
