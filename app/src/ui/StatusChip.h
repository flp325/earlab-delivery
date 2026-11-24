#pragma once

#include "Theme.h"

namespace earlab
{

/** 页头右侧的状态胶囊：surface + 1px border + 左侧状态点（accentWash 不再参与）。 */
class StatusChip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Tone
    {
        neutral,   // 空闲：text3 圆点
        accent,    // 进行中（考试态）：accent 圆点
        success    // 已完成：success 圆点
    };

    void setText (const juce::String& newText)
    {
        if (text == newText)
            return;

        text = newText;
        setTooltip (text);
        repaint();
    }

    void setTone (Tone newTone)
    {
        if (tone == newTone)
            return;

        tone = newTone;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto font = theme::uiFont (theme::typeMeta);
        const int width = juce::jmin (getWidth(), 40 + juce::GlyphArrangement::getStringWidthInt (font, text));
        const auto chip = getLocalBounds().removeFromRight (width);
        auto bounds = chip.toFloat().reduced (0.5f);
        // P2-3：胶囊圆角 = 高 / 2（token 只提供上限，实际永远由高度决定）
        const auto radius = juce::jmin ((float) theme::radiusPill,
                                        bounds.getHeight() * 0.5f);

        g.setColour (theme::surface);
        g.fillRoundedRectangle (bounds, radius);

        g.setColour (theme::border);
        g.drawRoundedRectangle (bounds, radius, 1.0f);

        constexpr float dotSize = 6.0f;
        const auto dotColour = tone == Tone::accent ? theme::accent
                             : tone == Tone::success ? theme::success
                                                     : theme::text3;

        g.setColour (dotColour);
        g.fillEllipse (bounds.getX() + 12.0f, bounds.getCentreY() - dotSize * 0.5f, dotSize, dotSize);

        g.setColour (theme::text2);
        g.setFont (font);
        g.drawText (text, chip.withTrimmedLeft (26).withTrimmedRight (12),
                    juce::Justification::centredLeft, true);
    }

private:
    juce::String text;
    Tone tone = Tone::neutral;
};

} // namespace earlab
