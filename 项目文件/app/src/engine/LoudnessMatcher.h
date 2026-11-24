#pragma once

#include "TrainableProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace earlab
{

struct LoudnessMatch
{
    float dryTrimDb = 0.0f;
    float wetTrimDb = 0.0f;
    double dryLufs  = -100.0;
    double wetLufs  = -100.0;
    bool valid      = false;
};

/**
    离线渲染干湿两路，各自测积分响度，算出把两路拉到同一目标响度所需的 trim。

    这是 AC-01 的核心：不做这一步，用户比较的是响度而不是音色。
*/
LoudnessMatch computeLoudnessMatch (ITrainableProcessor& processor,
                                    const juce::AudioBuffer<float>& source,
                                    double sampleRate,
                                    float targetLufs = -23.0f);

} // namespace earlab
