#include "PluginTrainableProcessor.h"

namespace earlab::plugin
{

PluginTrainableProcessor::PluginTrainableProcessor (std::unique_ptr<juce::AudioPluginInstance> instance,
                                                    PluginTask taskToApply)
    : plugin (std::move (instance)), task (std::move (taskToApply))
{
    if (plugin != nullptr)
        PluginTaskGenerator::applyToPlugin (*plugin, task);
}

PluginTrainableProcessor::~PluginTrainableProcessor()
{
    if (plugin != nullptr)
        plugin->releaseResources();
}

juce::String PluginTrainableProcessor::getName() const
{
    return task.pluginName.isNotEmpty() ? task.pluginName
                                        : (plugin != nullptr ? plugin->getName() : juce::String ("VST3"));
}

void PluginTrainableProcessor::setTask (const PluginTask& newTask)
{
    task = newTask;

    if (plugin != nullptr)
        PluginTaskGenerator::applyToPlugin (*plugin, task);
}

void PluginTrainableProcessor::prepare (double sampleRate, int maximumBlockSize, int numChannels)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock = juce::jmax (16, maximumBlockSize);

    juce::ignoreUnused (numChannels);

    if (plugin == nullptr)
        return;

    // 按插件自己的声道数准备：单声道插件不能被强行喂成立体声，
    // 否则部分 VST3 只处理 ch0，湿声右声道其实是未处理信号。
    const int pluginChannels = juce::jmax (1, juce::jmax (plugin->getTotalNumInputChannels(),
                                                          plugin->getTotalNumOutputChannels()));

    scratch.setSize (pluginChannels, maxBlock, false, true, true);
    plugin->prepareToPlay (sr, maxBlock);
    plugin->reset();

    maxObservedBlock = 0;
    lastRequestedSamples = 0;
}

void PluginTrainableProcessor::reset()
{
    if (plugin != nullptr)
        plugin->reset();
}

void PluginTrainableProcessor::process (juce::AudioBuffer<float>& buffer)
{
    if (plugin == nullptr)
        return;

    const int numSamples = buffer.getNumSamples();
    const int hostChannels = buffer.getNumChannels();
    const int pluginChannels = juce::jmax (1, scratch.getNumChannels());
    const int blockSize = juce::jmax (1, maxBlock);

    lastRequestedSamples = numSamples;

    // prepare() 没跑过就没有插件块大小契约可言；不要在音频线程上扩容。
    if (scratch.getNumSamples() < blockSize)
        return;

    midi.clear();

    // 立体声插件：一次处理整块。
    // 单声道插件：逐声道处理（L、R 各跑一次），避免右声道被原样放过去。
    for (int base = 0; base < hostChannels; base += pluginChannels)
    {
        // P1-1：调用方可能给远超 prepare 的块（等响测量最多 10 s），
        // 必须按 prepareToPlay 的 maxBlock 切块，插件内部缓冲只按 maxBlock 分配。
        for (int offset = 0; offset < numSamples; offset += blockSize)
        {
            const int chunk = juce::jmin (blockSize, numSamples - offset);
            maxObservedBlock = juce::jmax (maxObservedBlock, chunk);

            scratch.clear();

            for (int ch = 0; ch < pluginChannels; ++ch)
                if (base + ch < hostChannels)
                    scratch.copyFrom (ch, 0, buffer, base + ch, offset, chunk);

            juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(),
                                           pluginChannels, 0, chunk);
            plugin->processBlock (view, midi);

            for (int ch = 0; ch < pluginChannels; ++ch)
                if (base + ch < hostChannels)
                    buffer.copyFrom (base + ch, offset, scratch, ch, 0, chunk);
        }
    }
}

int PluginTrainableProcessor::getLatencySamples() const
{
    return plugin != nullptr ? plugin->getLatencySamples() : 0;
}

} // namespace earlab::plugin
