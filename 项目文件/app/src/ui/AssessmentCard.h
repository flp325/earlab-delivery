#pragma once

#include "../engine/Assessment.h"
#include "Theme.h"

namespace earlab
{

/** 最近一次测评的成绩卡：总分、等级、分项正确率与 JND。 */
class AssessmentCard final : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    void setResult (const AssessmentResult& result);
    void clearResult();

    void paint (juce::Graphics& g) override;

private:
    AssessmentResult data;
    bool hasResult = false;
};

} // namespace earlab
