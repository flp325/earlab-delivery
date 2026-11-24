#pragma once

#include "Theme.h"

namespace earlab
{

/** 首页技能统计卡：技能名 / 主值 / 附加信息。 */
class StatCard final : public juce::Component
{
public:
    void setData (const juce::String& titleText, const juce::String& valueText,
                  const juce::String& subText, bool hasData)
    {
        title = titleText;
        value = valueText;
        sub = subText;
        active = hasData;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (theme::surface);
        g.fillRoundedRectangle (bounds, (float) theme::cardRadius);

        g.setColour (active ? theme::accent.withAlpha (0.35f) : theme::border);
        g.drawRoundedRectangle (bounds, (float) theme::cardRadius, 1.0f);

        auto area = getLocalBounds().reduced (16, 14);

        g.setColour (theme::text2);
        g.setFont (theme::uiFont (15.0f, true));
        g.drawText (title, area.removeFromTop (18), juce::Justification::centredLeft);

        area.removeFromTop (6);

        g.setColour (active ? theme::accent : theme::text3);
        g.setFont (theme::monoFont (30.0f, true));
        g.drawText (value, area.removeFromTop (32), juce::Justification::centredLeft);

        area.removeFromTop (2);

        g.setColour (theme::text3);
        g.setFont (theme::uiFont (13.0f));
        g.drawText (sub, area.removeFromTop (16), juce::Justification::centredLeft);
    }

private:
    juce::String title, value, sub;
    bool active = false;
};

} // namespace earlab
