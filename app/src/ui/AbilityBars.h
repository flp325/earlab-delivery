#pragma once

#include "../engine/ProfileStore.h"
#include "Theme.h"

#include <array>

namespace earlab
{

/** 三项能力条：Elo 在 800–1400 之间映射，附首次基线标记与增量。 */
class AbilityBars final : public juce::Component
{
public:
    void setStats (const std::array<SkillStats, 3>& current,
                   const std::array<SkillStats, 3>& first,
                   bool hasBaseline);

    void paint (juce::Graphics& g) override;

private:
    struct Row
    {
        juce::String name;
        double elo = 1000.0;
        double firstElo = 0.0;
        bool hasData = false;
        bool hasBaseline = false;
    };

    std::array<Row, 3> rows;
};

} // namespace earlab
