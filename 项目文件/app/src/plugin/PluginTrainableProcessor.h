#pragma once

#include "PluginTask.h"
#include "engine/TrainableProcessor.h"

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

namespace earlab::plugin
{

/**
    把第三方插件包装成训练链里的处理器。

    双链只关心 ITrainableProcessor，因此插件和内置 EQ/压缩走同一条渲染路径，
    延迟补偿、等功率交叉淡化、等响 trim 全部复用。
*/
class PluginTrainableProcessor final : public ITrainableProcessor
{
public:
    PluginTrainableProcessor (std::unique_ptr<juce::AudioPluginInstance> instance,
                              PluginTask taskToApply);
    ~PluginTrainableProcessor() override;

    juce::String getName() const override;

    void prepare (double sampleRate, int maximumBlockSize, int numChannels) override;
    void reset() override;
    void process (juce::AudioBuffer<float>& buffer) override;

    int getLatencySamples() const override;

    /** 回归用：prepareToPlay 时声明的最大块；processBlock 的块绝不应超过它。 */
    [[nodiscard]] int getPreparedMaxBlockSize() const noexcept { return maxBlock; }

    /** P1-1 回归用：实际传给插件 processBlock 的最大块。 */
    [[nodiscard]] int getMaxObservedBlockSize() const noexcept { return maxObservedBlock; }

    /** P1-1 回归用：调用方最近一次传入的 buffer 长度（可能大于 maxBlock）。 */
    [[nodiscard]] int getLastRequestedSamples() const noexcept { return lastRequestedSamples; }

    /** 重新应用一道新题（同一插件实例上换题，避免重复加载）。 */
    void setTask (const PluginTask& newTask);

    [[nodiscard]] const PluginTask& getTask() const noexcept { return task; }

private:
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    PluginTask task;

    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midi;
    double sr = 48000.0;
    int maxBlock = 512;
    int maxObservedBlock = 0;
    int lastRequestedSamples = 0;
};

} // namespace earlab::plugin
