#pragma once
#include "Theme.h"

namespace earlab::ui::detail
{
inline const juce::Colour kBackground = theme::canvas;
inline const juce::Colour kPanel      = theme::surface;
inline const juce::Colour kTextBright = theme::text1;
inline const juce::Colour kTextDim    = theme::text3;
inline const juce::Colour kAccent     = theme::accent;
inline const juce::Colour kGood       = theme::success;
inline const juce::Colour kBad        = theme::danger;

// JUCE 的 String(const char*) 按单字节解释，中文必须显式走 UTF-8
inline juce::String zh (const char* text)
{
    return juce::String::fromUTF8 (text);
}

inline juce::Font uiFont (float height, bool bold = false)
{
    return theme::uiFont (height, bold);
}

/** 异步提示框：显式传 no-op 回调，避免 JUCE 在无回调时走同步模态循环
    （模态循环会卡住 QA 自动流程；错误提示只需要可见，不需要阻塞调用方）。 */
inline void showAsyncAlert (juce::MessageBoxIconType iconType,
                     const juce::String& title,
                     const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (
        iconType, title, message, {}, nullptr,
        juce::ModalCallbackFunction::create ([] (int) {}));
}
} // namespace earlab::ui::detail
