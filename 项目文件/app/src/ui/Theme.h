#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace earlab::theme
{

// ---- 配色（石墨监听台）
inline const juce::Colour canvas       { 0xff10191f };
inline const juce::Colour surface      { 0xff19252d };
inline const juce::Colour surfaceInset { 0xff101c23 };
inline const juce::Colour surfaceHover { 0xff354952 };
inline const juce::Colour surfaceRaised{ 0xff26363f };
inline const juce::Colour border       { 0xff34454e };
inline const juce::Colour borderInput  { 0xff657d87 };

inline const juce::Colour text1        { 0xffedf0f4 };
inline const juce::Colour text2        { 0xffc3cfd3 };
inline const juce::Colour text3        { 0xff98acb5 };

inline const juce::Colour accent       { 0xff6be6cc };
inline const juce::Colour accentStrong { 0xff086c60 };
inline const juce::Colour accentWash   { 0xff173e3e };
// 进度点「未开始」态：仅作装饰，不承担唯一表意（卡内题号文字是等价信息）
inline const juce::Colour dotTodo      { 0xff3a4450 };

inline const juce::Colour success      { 0xff46c98b };
inline const juce::Colour warn         { 0xffe3a94f };
inline const juce::Colour danger       { 0xffe4615e };

// ---- 字体（全站单一家族；数字用等宽）
inline juce::Font uiFont (float height, bool bold = false)
{
   #if JUCE_MAC
    constexpr auto family = "PingFang SC";
   #else
    constexpr auto family = "Microsoft YaHei UI";
   #endif
    return juce::Font (juce::FontOptions (family, height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font monoFont (float height, bool bold = false)
{
   #if JUCE_MAC
    constexpr auto family = "Menlo";
   #else
    constexpr auto family = "Consolas";
   #endif
    return juce::Font (juce::FontOptions (family, height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

// ---- 8pt 栅格
inline constexpr int margin          = 16;
inline constexpr int sidebarWidth    = 184;
inline constexpr int contentPadding  = 24;
inline constexpr int gap             = 8;
inline constexpr int groupGap        = 16;
inline constexpr int cardRadius      = 12;
inline constexpr int controlHeight   = 40;
inline constexpr int compactHeight   = 32;

// ---- P1 设计 token（间距 / 高度 / 圆角；评估页布局只引用这些值）
inline constexpr int space8              = 8;
inline constexpr int space12             = 12;
inline constexpr int space16             = 16;
inline constexpr int space24             = 24;
inline constexpr int space32             = 32;

inline constexpr int cardPaddingX        = 20;
inline constexpr int cardPaddingY        = 16;

inline constexpr int answerHeight        = 48;
inline constexpr int rowHeight           = 40;
inline constexpr int tableHeaderHeight   = 32;

inline constexpr int radiusCard          = cardRadius;
inline constexpr int radiusControl       = 8;
inline constexpr int radiusBadge         = 6;
// P2-3：胶囊圆角 = 高/2（绘制时与高度取 jmin，token 本身给一个足够大的上限）
inline constexpr int radiusPill          = 999;

// P2-4：考试态卡描边 = accent 以 30% 叠在 surface 上（等价 accent@0.30）
inline constexpr float examOutlineAlpha  = 0.30f;

// ---- P1 字号刻度（JUCE Font height = ascent + descent，不是 CSS px）
inline constexpr float typeEyebrow       = 15.0f;
inline constexpr float typeMeta          = 15.0f;
inline constexpr float typeBody          = 16.0f;
inline constexpr float typeH2            = 18.0f;
inline constexpr float typeH1            = 30.0f;
inline constexpr float typeHero          = 40.0f;
inline constexpr float typeButton        = 16.0f;

// ---- 评估页内容列最大宽度（P1-8：≥ 该宽度时水平居中，不随窗口继续拉伸）
inline constexpr int assessmentContentWidth = 960;

} // namespace earlab::theme
