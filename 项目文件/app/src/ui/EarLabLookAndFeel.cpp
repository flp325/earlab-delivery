#include "EarLabLookAndFeel.h"

#include "FadeBase.h"
#include "Theme.h"

namespace earlab
{

EarLabLookAndFeel::EarLabLookAndFeel()
{
    setColourScheme (getDarkColourScheme());

    setColour (juce::ResizableWindow::backgroundColourId, theme::canvas);

    setColour (juce::TextButton::buttonColourId, theme::surface);
    setColour (juce::TextButton::buttonOnColourId, theme::accentWash);
    setColour (juce::TextButton::textColourOffId, theme::text1);
    setColour (juce::TextButton::textColourOnId, theme::text1);

    setColour (juce::ComboBox::backgroundColourId, theme::surface);
    setColour (juce::ComboBox::textColourId, theme::text1);
    setColour (juce::ComboBox::outlineColourId, theme::border);
    setColour (juce::ComboBox::arrowColourId, theme::text3);
    setColour (juce::ComboBox::buttonColourId, theme::surface);

    setColour (juce::PopupMenu::backgroundColourId, theme::surfaceRaised);
    setColour (juce::PopupMenu::textColourId, theme::text1);
    setColour (juce::PopupMenu::headerTextColourId, theme::text3);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accentWash);
    setColour (juce::PopupMenu::highlightedTextColourId, theme::text1);

    setColour (juce::TextEditor::backgroundColourId, theme::surfaceInset);
    setColour (juce::TextEditor::textColourId, theme::text1);
    setColour (juce::TextEditor::outlineColourId, theme::border);
    setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
    setColour (juce::TextEditor::highlightColourId, theme::accent.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, theme::text1);

    setColour (juce::ListBox::backgroundColourId, theme::surface);
    setColour (juce::ListBox::outlineColourId, theme::border);

    setColour (juce::ScrollBar::thumbColourId, theme::borderInput);

    setColour (juce::AlertWindow::backgroundColourId, theme::surface);
    setColour (juce::AlertWindow::textColourId, theme::text1);
    setColour (juce::AlertWindow::outlineColourId, theme::border);

    setColour (juce::TooltipWindow::backgroundColourId, theme::surfaceRaised);
    setColour (juce::TooltipWindow::textColourId, theme::text1);
    setColour (juce::TooltipWindow::outlineColourId, theme::border);

    setColour (juce::Label::textColourId, theme::text1);

    setColour (juce::ProgressBar::backgroundColourId, theme::accentWash);
    setColour (juce::ProgressBar::foregroundColourId, theme::accent);

    setColour (juce::ToggleButton::textColourId, theme::text1);
    setColour (juce::ToggleButton::tickColourId, theme::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, theme::borderInput);
}

juce::Font EarLabLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return theme::uiFont (theme::typeButton);
}

juce::Font EarLabLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return theme::uiFont (14.0f);
}

juce::Font EarLabLookAndFeel::getPopupMenuFont()
{
    return theme::uiFont (14.0f);
}

juce::Font EarLabLookAndFeel::getLabelFont (juce::Label& label)
{
    // P1：Label 的 setFont(bold) 之前被这里丢掉字重，导致 H2/600 实际渲染为常规体；
    // 保留 bold，字号仍按 11–32 夹取（hero 由 drawText 直接绘制，不经过这里）。
    const auto font = label.getFont();
    return font.withHeight (juce::jlimit (11.0f, 32.0f, font.getHeight()));
}

void EarLabLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                              const juce::Colour& backgroundColour,
                                              bool isMouseOver, bool isButtonDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto radius = (float) theme::radiusControl;

    // P2-5：LabButton 通过 FadeProvider 给出 120ms 插值后的 hover / 按下量；
    // 其它 Button（如插件库里的原生 TextButton）退回即时 0/1，视觉与旧版一致。
    auto hoverAmount = isMouseOver ? 1.0f : 0.0f;
    auto pressAmount = isButtonDown ? 1.0f : 0.0f;

    if (auto* fade = dynamic_cast<const FadeProvider*> (&button))
    {
        hoverAmount = fade->getHoverFade();
        pressAmount = fade->getPressFade();
    }

    // 禁用态：填充/描边同时降级，主按钮也不得残留强调色（P0-3）
    if (! button.isEnabled())
    {
        g.setColour (theme::surfaceInset);
        g.fillRoundedRectangle (bounds, radius);

        g.setColour (theme::border);
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }
    else if (backgroundColour == theme::accentStrong)
    {
        // 主操作 = accentStrong 填充 + 白字（P0-3）：hover 提亮 10%、按下压暗 12%
        auto colour = theme::accentStrong;
        colour = colour.interpolatedWith (juce::Colours::white, 0.10f * hoverAmount);
        colour = colour.interpolatedWith (juce::Colours::black, 0.12f * pressAmount);

        g.setColour (colour);
        g.fillRoundedRectangle (bounds, radius);
    }
    else
    {
        // surface → surfaceHover（hover）、border → borderInput，按下再压暗（P2-5）
        auto fill = theme::surfaceRaised.interpolatedWith (theme::surfaceHover, hoverAmount);
        fill = fill.interpolatedWith (theme::surfaceInset, pressAmount);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, radius);

        g.setColour (theme::border.interpolatedWith (theme::borderInput,
                                                    juce::jmax (hoverAmount, pressAmount)));
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }

    // P1-9：主/次/禁用三条路径共用同一焦点环（2px accent，内缩 2、半径 -2）
    if (button.hasKeyboardFocus (true))
    {
        g.setColour (theme::accent);
        g.drawRoundedRectangle (button.getLocalBounds().toFloat().reduced (2.0f),
                                radius - 2.0f, 2.0f);
    }
}

void EarLabLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                        bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));

    if (! button.isEnabled())
    {
        g.setColour (theme::text3.withAlpha (0.6f));
    }
    else
    {
        const auto background = button.findColour (juce::TextButton::buttonColourId);
        const bool primary = background == theme::accentStrong;

        g.setColour (primary ? juce::Colours::white
                             : button.findColour (juce::TextButton::textColourOffId));
    }

    // P2-5：按下时文字下移 1px（用连续位移，按下过渡期间不突兀）
    auto pressAmount = 0.0f;

    if (auto* fade = dynamic_cast<const FadeProvider*> (&button))
        pressAmount = fade->getPressFade();

    const juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::translation (0.0f, pressAmount));

    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (10, 0),
                      juce::Justification::centred, 1);
}

void EarLabLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                      int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);

    g.setColour (theme::surface);
    g.fillRoundedRectangle (bounds, (float) theme::radiusControl);

    g.setColour (box.hasKeyboardFocus (true) || box.isMouseOver() ? theme::borderInput : theme::border);
    g.drawRoundedRectangle (bounds, (float) theme::radiusControl, 1.0f);

    juce::Path arrow;
    const auto cx = (float) width - 18.0f;
    const auto cy = (float) height * 0.5f;
    arrow.addTriangle (cx - 5.0f, cy - 2.5f, cx + 5.0f, cy - 2.5f, cx, cy + 3.5f);

    g.setColour (theme::text3);
    g.fillPath (arrow);
}

void EarLabLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (12, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

void EarLabLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height,
                                                  juce::TextEditor&)
{
    g.setColour (theme::surfaceInset);
    g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height),
                            (float) theme::radiusControl);
}

void EarLabLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height,
                                               juce::TextEditor& editor)
{
    g.setColour (editor.hasKeyboardFocus (true) ? theme::accent : theme::border);
    g.drawRoundedRectangle (juce::Rectangle<float> (0.5f, 0.5f, (float) width - 1.0f,
                                                    (float) height - 1.0f),
                            (float) theme::radiusControl, 1.0f);
}

void EarLabLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y,
                                       int width, int height, bool isScrollbarVertical,
                                       int thumbStartPosition, int thumbSize,
                                       bool isMouseOver, bool isMouseDown)
{
    if (thumbSize <= 0)
        return;

    auto thumb = isScrollbarVertical
                     ? juce::Rectangle<int> (x + 2, thumbStartPosition, width - 4, thumbSize)
                     : juce::Rectangle<int> (thumbStartPosition, y + 2, thumbSize, height - 4);

    g.setColour (isMouseDown ? theme::accent
                             : (isMouseOver ? theme::borderInput : theme::border));
    // P2-3：滚动条滑块 = 胶囊（圆角 = 短边 / 2），不再写裸 4px
    const auto thumbRadius = 0.5f * (float) juce::jmin (thumb.getWidth(), thumb.getHeight());
    g.fillRoundedRectangle (thumb.toFloat(), thumbRadius);
}

} // namespace earlab
