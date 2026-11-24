#include "PluginHost.h"

#include <algorithm>
#include <cmath>

namespace earlab::plugin
{

namespace
{
/** 插件实例报告的声道数为 0 时用扫描元数据兜底。 */
int reportedOrMetadataCount (int reported, int metadata)
{
    return reported > 0 ? reported : metadata;
}

bool looksBypassLike (const juce::String& name)
{
    const auto n = name.toLowerCase();
    return n.contains ("bypass") || n.contains ("mix") || n.contains ("dry/wet")
        || n.contains ("dry wet") || n.contains ("wet/dry") || n.contains (juce::String (juce::CharPointer_UTF8 ("旁通")))
        || n.contains ("mix ");
}

/** 名字像增益/频率/Q/阈值这些真正会改变声音的参数。 */
bool looksAudible (const juce::String& name)
{
    const auto n = name.toLowerCase();
    static const char* keywords[] = { "gain", "freq", "q ", "q", "threshold", "ratio",
                                      "attack", "release", "knee", "tilt", "level",
                                      "drive", "amount", "depth", "width", "trim",
                                      "low", "high", "mid", "band", "boost", "cut" };

    for (const auto* k : keywords)
        if (n.contains (k))
            return true;

    return false;
}
} // namespace

int PluginHost::preheatChannelCount (int numInputs, int numOutputs) noexcept
{
    // 输出总线也会被 JUCE 映射到 AudioBuffer 的通道指针上；只按输入分配会让
    // 0-in/64-out 这类插件写到宿主 buffer 之外（P0-1）。
    return juce::jmax (2, juce::jmax (numInputs, numOutputs));
}

int PluginHost::resolvedPreheatChannelCount (int instanceInputs,
                                             int instanceOutputs,
                                             int metadataInputs,
                                             int metadataOutputs) noexcept
{
    // 部分插件在 create 之后、prepare 之前仍报告 0 声道；用扫描元数据兜底，
    // 避免预热缓冲退回 2 声道后再次被多输出插件越界写。
    return preheatChannelCount (reportedOrMetadataCount (instanceInputs, metadataInputs),
                                reportedOrMetadataCount (instanceOutputs, metadataOutputs));
}

int PluginHost::chooseLoadableIndex (const std::vector<PluginInfo>& plugins) noexcept
{
    if (plugins.empty())
        return -1;

    for (int index = 0; index < static_cast<int> (plugins.size()); ++index)
        if (! plugins[static_cast<size_t> (index)].isInstrument)
            return index;

    return 0;
}

bool PluginHost::prepareAndWarmUp (juce::AudioPluginInstance& instance,
                                   double sampleRate,
                                   int blockSize,
                                   int metadataInputs,
                                   int metadataOutputs,
                                   int warmUpSamples)
{
    const int channels = resolvedPreheatChannelCount (instance.getTotalNumInputChannels(),
                                                      instance.getTotalNumOutputChannels(),
                                                      metadataInputs,
                                                      metadataOutputs);

    const int samples = juce::jlimit (1, juce::jmax (1, blockSize), warmUpSamples);

    instance.prepareToPlay (sampleRate, blockSize);
    instance.reset();

    // 让插件把参数定义完整建起来（部分 VST3 延迟到第一次 process 才创建）
    juce::AudioBuffer<float> silence (channels, samples);
    silence.clear();
    juce::MidiBuffer midi;
    instance.processBlock (silence, midi);

    for (int ch = 0; ch < silence.getNumChannels(); ++ch)
        for (int i = 0; i < silence.getNumSamples(); ++i)
            if (! std::isfinite (silence.getSample (ch, i)))
                return false;

    return true;
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::create (const PluginInfo& info,
                                                               double sampleRate,
                                                               int blockSize,
                                                               juce::String& error)
{
    juce::PluginDescription description;
    description.name             = info.name;
    description.descriptiveName  = info.name;
    description.manufacturerName = info.manufacturer;
    description.category         = info.category;
    description.version          = info.version;
    description.pluginFormatName = info.format.isEmpty() ? "VST3" : info.format;
    description.fileOrIdentifier = info.fileOrIdentifier;
    description.uniqueId         = info.uniqueId;
    description.isInstrument     = info.isInstrument;
    description.numInputChannels = info.numInputs;
    description.numOutputChannels = info.numOutputs;

    juce::AudioPluginFormatManager manager;
    juce::addHeadlessDefaultFormatsToManager (manager);

    auto instance = manager.createPluginInstance (description, sampleRate, blockSize, error);

    if (instance != nullptr)
    {
        // prepareToPlay + 静音首帧与预检共用，缓冲声道数覆盖插件输出（P0-1）。
        if (! prepareAndWarmUp (*instance, sampleRate, blockSize, info.numInputs, info.numOutputs))
        {
            error = utf8 ("插件预热产生非有限音频，已拒绝加载");
            return nullptr;
        }
    }

    return instance;
}

std::vector<HostParameter> PluginHost::listParameters (juce::AudioPluginInstance& instance)
{
    std::vector<HostParameter> result;

    const auto& parameters = instance.getParameters();
    result.reserve (static_cast<size_t> (parameters.size()));

    for (int i = 0; i < parameters.size(); ++i)
    {
        auto* parameter = parameters[i];

        if (parameter == nullptr)
            continue;

        HostParameter info;
        info.index        = i;
        info.name         = parameter->getName (128);
        info.label        = parameter->getLabel();
        info.defaultValue = parameter->getDefaultValue();
        info.currentValue = parameter->getValue();
        info.numSteps     = parameter->getNumSteps();
        info.isDiscrete   = info.numSteps > 0 && info.numSteps < 200;
        info.isMetaParameter = parameter->isMetaParameter();

        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            info.id = withId->paramID;

        info.isBypassLike = looksBypassLike (info.name);

        result.push_back (std::move (info));
    }

    return result;
}

bool PluginHost::setParameter (juce::AudioPluginInstance& instance, int index, float normalisedValue)
{
    const auto& parameters = instance.getParameters();

    if (! juce::isPositiveAndBelow (index, parameters.size()))
        return false;

    if (auto* parameter = parameters[index])
    {
        parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalisedValue));
        return true;
    }

    return false;
}

void PluginHost::setAllToDefault (juce::AudioPluginInstance& instance)
{
    for (auto* parameter : instance.getParameters())
        if (parameter != nullptr)
            parameter->setValueNotifyingHost (parameter->getDefaultValue());
}

void PluginHost::refreshState (juce::AudioPluginInstance& instance)
{
    juce::MemoryBlock state;
    instance.getStateInformation (state);
}

std::vector<std::pair<int, float>> PluginHost::activateRelatedSwitches (
    juce::AudioPluginInstance& instance,
    const std::vector<HostParameter>& all,
    int parameterIndex)
{
    std::vector<std::pair<int, float>> applied;
    juce::String prefix;

    for (const auto& parameter : all)
    {
        if (parameter.index == parameterIndex)
        {
            prefix = parameter.name.upToLastOccurrenceOf (" ", false, false);
            break;
        }
    }

    if (prefix.isEmpty())
        return applied;

    for (const auto& parameter : all)
    {
        if (parameter.index == parameterIndex || ! parameter.name.startsWith (prefix + " "))
            continue;

        const auto tail = parameter.name.substring (prefix.length() + 1).toLowerCase();
        const bool isSwitch = tail.contains ("used") || tail.contains ("enabled")
                           || tail.contains ("active") || tail == "on";

        if (isSwitch && parameter.currentValue < 0.5f)
        {
            if (setParameter (instance, parameter.index, 1.0f))
                applied.emplace_back (parameter.index, 1.0f);
        }
    }

    return applied;
}

std::vector<HostParameter> PluginHost::trainingCandidates (const std::vector<HostParameter>& all, int maxCount)
{
    std::vector<HostParameter> preferred, rest;

    for (const auto& parameter : all)
    {
        if (parameter.isBypassLike || parameter.isMetaParameter)
            continue;

        if (parameter.isDiscrete)
            continue;

        if (looksAudible (parameter.name))
            preferred.push_back (parameter);
        else
            rest.push_back (parameter);
    }

    std::vector<HostParameter> result = preferred;

    for (const auto& parameter : rest)
    {
        if (static_cast<int> (result.size()) >= maxCount)
            break;

        result.push_back (parameter);
    }

    if (static_cast<int> (result.size()) > maxCount)
        result.resize (static_cast<size_t> (maxCount));

    return result;
}

} // namespace earlab::plugin
