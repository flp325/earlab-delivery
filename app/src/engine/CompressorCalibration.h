#pragma once

#include "CompressorProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace earlab
{

/**
    二分搜索阈值，使压缩器在该素材上的平均增益衰减贴近目标值。

    出题时用：题目问“压缩量约多少 dB”，先保证实际 GR 真的是这个数。
*/
float calibrateThresholdForTargetGr (CompressorProcessor& compressor,
                                     const juce::AudioBuffer<float>& source,
                                     double sampleRate,
                                     const CompressorProcessor::Params& baseParams,
                                     float targetGrDb);

} // namespace earlab
