#pragma once

#include "../engine/Assessment.h"
#include "Theme.h"
#include "HistoryPagination.h"
#include "LabButton.h"

#include <vector>

namespace earlab
{

/** 测评历史表：时间 / 等级 / 分数，最新在上。 */
class AssessmentHistory final : public juce::Component,
                                public juce::SettableTooltipClient
{
public:
    AssessmentHistory();
    void setResults (const std::vector<AssessmentResult>& results);

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void movePage (int delta);
    void updatePagingControls();
    std::vector<AssessmentResult> items;
    ui::HistoryPagination pagination;
    LabButton previousButton, nextButton;
    juce::Label pageLabel;
};

} // namespace earlab
