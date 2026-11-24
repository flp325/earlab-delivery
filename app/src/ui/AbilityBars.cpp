#include "AbilityBars.h"

namespace earlab
{

namespace
{
juce::String zh (const char* text)
{
    return juce::String::fromUTF8 (text);
}
} // namespace

void AbilityBars::setStats (const std::array<SkillStats, 3>& current,
                            const std::array<SkillStats, 3>& first,
                            bool hasBaseline)
{
    static const char* names[3] = { "EQ 频段定位", "压缩量判断", "插件辨别" };

    for (size_t i = 0; i < rows.size(); ++i)
    {
        auto& row = rows[i];
        row.name = zh (names[i]);
        row.hasData = current[i].sessions > 0;
        row.elo = current[i].elo;
        row.hasBaseline = hasBaseline && first[i].sessions > 0;
        row.firstElo = first[i].elo;
    }

    repaint();
}

void AbilityBars::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    const int rowHeight = juce::jmax (36, area.getHeight() / 3);

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& row = rows[i];
        auto line = area.removeFromTop (rowHeight);

        auto nameArea = line.removeFromLeft (92);

        g.setColour (theme::text3);
        g.setFont (theme::uiFont (12.0f));
        g.drawText (row.name, nameArea, juce::Justification::centredLeft);

        auto valueArea = line.removeFromRight (66);
        const auto badge = line.removeFromRight (46).withSizeKeepingCentre (42, 20);

        auto track = line.withSizeKeepingCentre (line.getWidth(), 6);
        track = track.withTrimmedLeft (8).withTrimmedRight (8);

        // P2-3：进度条轨道 / 填充 / 增量徽章圆角统一走 token（胶囊 = 高/2）
        const auto trackRadius = (float) track.getHeight() * 0.5f;

        g.setColour (theme::surfaceInset.brighter (0.15f));
        g.fillRoundedRectangle (track.toFloat(), trackRadius);

        if (row.hasData)
        {
            const auto pos = juce::jlimit (0.0, 1.0, (row.elo - 800.0) / 600.0);
            auto filled = track.toFloat().withWidth ((float) track.getWidth() * (float) pos);

            g.setColour (theme::accent);
            g.fillRoundedRectangle (filled, trackRadius);

            if (row.hasBaseline)
            {
                const auto firstPos = juce::jlimit (0.0, 1.0, (row.firstElo - 800.0) / 600.0);
                const float x = (float) track.getX() + (float) track.getWidth() * (float) firstPos;

                g.setColour (theme::text3.withAlpha (0.7f));
                g.fillRect (x - 0.5f, (float) track.getY() - 3.0f, 1.0f, (float) track.getHeight() + 6.0f);
            }
        }

        g.setFont (theme::monoFont (13.0f));
        g.setColour (row.hasData ? theme::text1 : theme::text3);
        g.drawText (row.hasData ? juce::String (juce::roundToInt (row.elo)) : zh ("—"),
                    valueArea, juce::Justification::centredRight);

        if (row.hasData && row.hasBaseline)
        {
            const int delta = juce::roundToInt (row.elo - row.firstElo);

            if (delta != 0)
            {
                g.setColour ((delta > 0 ? theme::success : theme::danger).withAlpha (0.14f));
                g.fillRoundedRectangle (badge.toFloat(), (float) theme::radiusBadge);

                g.setColour (delta > 0 ? theme::success : theme::danger);
                g.setFont (theme::uiFont (11.0f, true));
                g.drawText ((delta > 0 ? "+" : "") + juce::String (delta), badge,
                            juce::Justification::centred);
            }
        }
    }
}

} // namespace earlab
