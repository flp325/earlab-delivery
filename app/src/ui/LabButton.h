#pragma once

#include "FadeBase.h"

namespace earlab
{

/** P2-5：MainComponent 的所有 TextButton 成员统一用它，带 120ms hover/按下过渡。

    TextButton 的绘制仍交给 EarLabLookAndFeel；LookAndFeel 通过 FadeProvider
    读取插值后的 hover / 按下量，对填充、描边与文字位移做连续过渡。
*/
class LabButton final : public FadeBase<juce::TextButton>
{
public:
    LabButton() = default;

    explicit LabButton (const juce::String& text)
        : FadeBase<juce::TextButton> (text)
    {
    }

protected:
    void paintFaded (juce::Graphics& g, float hoverAmount, float pressAmount) override
    {
        // 与 juce::TextButton::paintButton 相同：颜色 ID + LookAndFeel 接管绘制。
        // bool 参数只是兜底（别人的 LookAndFeel 会按它绘制），本主题读 FadeProvider。
        auto& lf = getLookAndFeel();

        lf.drawButtonBackground (g, *this,
                                 findColour (getToggleState() ? juce::TextButton::buttonOnColourId
                                                              : juce::TextButton::buttonColourId),
                                 hoverAmount > 0.5f, pressAmount > 0.5f);

        lf.drawButtonText (g, *this, hoverAmount > 0.5f, pressAmount > 0.5f);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabButton)
};

} // namespace earlab
