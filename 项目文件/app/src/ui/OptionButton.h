#pragma once

#include "FadeBase.h"

namespace earlab
{

/** 答题选项：左侧键位徽标 + 选项文字，支持正确/错误状态；hover/按下 120ms 过渡（P2-5）。 */
class OptionButton final : public FadeBase<juce::Button>
{
public:
    enum class Result { none, correct, wrong };

    OptionButton() : FadeBase<juce::Button> (juce::String())
    {
        setClickingTogglesState (false);
        setWantsKeyboardFocus (true);
    }

    void setResult (Result newResult)
    {
        if (result == newResult)
            return;

        result = newResult;
        repaint();
    }

protected:
    void paintFaded (juce::Graphics& g, float hoverAmount, float pressAmount) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        auto fill = theme::surface;
        auto outline = theme::border;

        if (result == Result::correct)
        {
            fill = theme::success.withAlpha (0.14f);
            outline = theme::success;
        }
        else if (result == Result::wrong)
        {
            fill = theme::danger.withAlpha (0.14f);
            outline = theme::danger;
        }
        else if (isEnabled())
        {
            // P2-5：surface → surfaceHover（hover）、border → borderInput，按下再压暗
            fill = fill.interpolatedWith (theme::surfaceHover, hoverAmount);
            fill = fill.interpolatedWith (theme::surfaceInset, pressAmount);
            outline = outline.interpolatedWith (theme::borderInput,
                                                juce::jmax (hoverAmount, pressAmount));
        }

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, (float) theme::radiusControl);

        g.setColour (outline);
        g.drawRoundedRectangle (bounds, (float) theme::radiusControl, 1.0f);

        // 键位徽标（左对齐）
        auto badge = juce::Rectangle<int> (10, (getHeight() - 24) / 2, 24, 24);

        g.setColour (result == Result::none ? theme::surfaceRaised : theme::surfaceRaised.brighter (0.25f));
        g.fillRoundedRectangle (badge.toFloat(), (float) theme::radiusBadge);

        g.setColour (result == Result::correct ? theme::success
                                               : (result == Result::wrong ? theme::danger : theme::text3));
        if (result == Result::none)
        {
            g.setFont (theme::monoFont (14.0f));
            g.drawText (letter, badge, juce::Justification::centred);
        }
        else
        {
            const auto c = badge.getCentre().toFloat();
            juce::Path mark;
            if (result == Result::correct)
            {
                mark.startNewSubPath (c.translated (-5, 0));
                mark.lineTo (c.translated (-1, 4));
                mark.lineTo (c.translated (6, -4));
            }
            else
            {
                mark.startNewSubPath (c.translated (-4, -4));
                mark.lineTo (c.translated (4, 4));
                mark.startNewSubPath (c.translated (4, -4));
                mark.lineTo (c.translated (-4, 4));
            }
            g.strokePath (mark, juce::PathStrokeType (2, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // P1：判定后答案文字不再被禁用态置灰（错误用 text2，对危险底 6.98:1）
        const auto textColour = result == Result::wrong ? theme::text2
                              : result == Result::correct ? theme::text1
                              : (isEnabled() ? theme::text1 : theme::text3.withAlpha (0.5f));

        // P2-1：选项标签是纯数字 + 单位（100 Hz / 4.0 kHz），改用等宽字体
        g.setColour (textColour);
        g.setFont (theme::monoFont (theme::typeBody));

        g.drawText (getButtonText(),
                    getLocalBounds().withTrimmedLeft (44)
                                    .translated (0, juce::roundToInt (pressAmount)),
                    juce::Justification::centredLeft, true);

        // P1-9：统一焦点环（2px accent，内缩 2）
        if (hasKeyboardFocus (true))
        {
            g.setColour (theme::accent);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f),
                                    (float) theme::radiusControl - 2.0f, 2.0f);
        }
    }

public:
    void setLetter (const juce::String& newLetter)
    {
        letter = newLetter;
        repaint();
    }

private:
    Result result = Result::none;
    juce::String letter;
};

} // namespace earlab
