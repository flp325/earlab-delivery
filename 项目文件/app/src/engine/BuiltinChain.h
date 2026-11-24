#pragma once

#include "CompressorProcessor.h"
#include "MultiBandEq.h"
#include "TrainableProcessor.h"

namespace earlab
{

/** 内置湿声链：EQ -> 压缩。两个单元常驻，未参与题型时置为中性（bypass 等价）。 */
class BuiltinChain final : public ITrainableProcessor
{
public:
    MultiBandEqProcessor& getEq() noexcept { return eq; }
    const MultiBandEqProcessor& getEq() const noexcept { return eq; }
    CompressorProcessor& getComp() noexcept { return comp; }
    const CompressorProcessor& getComp() const noexcept { return comp; }

    juce::String getName() const override { return "EarLab Builtin Chain"; }

    void prepare (double sampleRate, int maximumBlockSize, int numChannels) override
    {
        eq.prepare (sampleRate, maximumBlockSize, numChannels);
        comp.prepare (sampleRate, maximumBlockSize, numChannels);
    }

    void reset() override
    {
        eq.reset();
        comp.reset();
    }

    void process (juce::AudioBuffer<float>& buffer) override
    {
        eq.process (buffer);
        comp.process (buffer);
    }

    int getLatencySamples() const override { return 0; }

private:
    MultiBandEqProcessor eq;
    CompressorProcessor comp;
};

} // namespace earlab
