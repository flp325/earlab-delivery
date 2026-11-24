#pragma once

#include "../engine/ProfileStore.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace earlab
{

/** 极简 Elo 历史曲线：两条线分别为 EQ 与压缩，横轴是各自的训练轮次。 */
class HistoryChart final : public juce::Component
{
public:
    void setHistory (const std::vector<SessionRecord>& records);

    void paint (juce::Graphics& g) override;

private:
    std::vector<SessionRecord> history;
};

} // namespace earlab
