#pragma once

#include "FadeBase.h"

namespace earlab
{

/** 左侧导航项：选中时左侧 3px 强调条 + 加粗文字；hover/按下走 120ms 过渡（P2-5）。 */
class NavButton final : public FadeBase<juce::Button>
{
public:
    enum class Icon { overview, audio, training, plugin, assessment, settings };
    void setIcon (Icon value) { icon = value; repaint(); }
    NavButton() : FadeBase<juce::Button> (juce::String()) {}

    explicit NavButton (const juce::String& text) : FadeBase<juce::Button> (text)
    {
        setClickingTogglesState (false);
    }

protected:
    void paintFaded (juce::Graphics& g, float hoverAmount, float pressAmount) override
    {
        auto bounds = getLocalBounds().toFloat();

        const bool selected = getToggleState();

        if (selected)
        {
            g.setColour (theme::accentWash);
            g.fillRoundedRectangle (bounds, (float) theme::radiusControl);

            g.setColour (theme::accent);
            // 3px 强调条：胶囊（半径取 token，绘制时会被夹到宽度的一半）
            g.fillRoundedRectangle (3.0f, bounds.getCentreY() - 8.0f, 3.0f, 16.0f, 1.5f);
        }
        else
        {
            // surface → surfaceHover → surfaceInset，按 hover / 按下量连续插值
            auto fill = theme::surface.interpolatedWith (theme::surfaceHover, hoverAmount);
            fill = fill.interpolatedWith (theme::surfaceInset, pressAmount);

            g.setColour (fill);
            g.fillRoundedRectangle (bounds, (float) theme::radiusControl);
        }

        // 按下时文字下移 1px（P2-5；子像素位移让过渡连续）
        const auto textArea = getLocalBounds()
                                  .withTrimmedLeft (45)
                                  .translated (0, juce::roundToInt (pressAmount));

        const auto base = selected ? theme::text1
                                   : theme::text2.interpolatedWith (theme::text1, hoverAmount);
        drawIcon (g, selected ? theme::accent : base);

        g.setColour (base);
        g.setFont (theme::uiFont (16.0f, selected));
        g.drawText (getButtonText(), textArea, juce::Justification::centredLeft);

        // D2：导航项此前可被 Tab 聚焦但没有任何视觉反馈；补 2px accent 焦点环
        if (hasKeyboardFocus (true))
        {
            g.setColour (theme::accent);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f),
                                    (float) theme::radiusControl - 2.0f, 2.0f);
        }
    }

private:
    Icon icon = Icon::overview;

    void drawIcon (juce::Graphics& g, juce::Colour colour) const
    {
        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::translation (16.0f, getHeight() * 0.5f - 9.0f));
        g.setColour (colour);
        juce::Path p;
        switch (icon)
        {
            case Icon::overview:
                for (int x : { 2, 10 })
                    for (int y : { 2, 10 })
                        p.addRoundedRectangle ((float) x, (float) y, 5.0f, 5.0f, 1.0f);
                break;
            case Icon::audio:
                for (int i = 0; i < 5; ++i)
                {
                    constexpr float heights[] = { 5, 10, 16, 8, 4 };
                    const auto x = 2.0f + 3.5f * i;
                    p.startNewSubPath (x, 9.0f - heights[i] / 2);
                    p.lineTo (x, 9.0f + heights[i] / 2);
                }
                break;
            case Icon::training:
                p.addEllipse (1, 1, 16, 16);
                p.addEllipse (5, 5, 8, 8);
                g.fillEllipse (7.5f, 7.5f, 3.0f, 3.0f);
                break;
            case Icon::plugin:
                for (int i = 0; i < 3; ++i)
                {
                    const float x = 3.0f + i * 6;
                    const float y = i == 1 ? 5.0f : 11.0f;
                    p.startNewSubPath (x, 2); p.lineTo (x, y - 2);
                    p.startNewSubPath (x, y + 2); p.lineTo (x, 16);
                    p.addEllipse (x - 2, y - 2, 4, 4);
                }
                break;
            case Icon::assessment:
                p.addRoundedRectangle (3, 1, 12, 16, 2);
                p.startNewSubPath (6, 9); p.lineTo (8, 11); p.lineTo (12, 6);
                break;
            case Icon::settings:
                p.addEllipse (4, 4, 10, 10);
                p.addEllipse (7, 7, 4, 4);
                for (int i = 0; i < 8; ++i)
                {
                    const auto angle = juce::MathConstants<float>::twoPi * i / 8;
                    const juce::Point<float> centre { 9, 9 };
                    p.startNewSubPath (centre.getPointOnCircumference (5, angle));
                    p.lineTo (centre.getPointOnCircumference (7, angle));
                }
                break;
        }
        g.strokePath (p, juce::PathStrokeType (1.55f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NavButton)
};

} // namespace earlab
