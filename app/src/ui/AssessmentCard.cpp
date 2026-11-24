#include "AssessmentCard.h"

#include "GradeStyle.h"
#include "TimeFormat.h"

namespace earlab
{

namespace
{
juce::String zh (const char* text)
{
    return juce::String::fromUTF8 (text);
}

int textWidth (const juce::Font& font, const juce::String& text)
{
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, text)) + 1;
}

// P1-3 内容栈（卡高 176 = 上下内边距 16×2 + 20 + 8 + 48 + 12 + 28×2）
constexpr int kHeaderRow     = 20;
constexpr int kScoreRow      = 48;
constexpr int kBreakdownRow  = 28;
constexpr int kScoreColumn   = 76;   // 数字列固定宽，分数 87/100 都不影响后缀 x
constexpr int kLabelColumn   = 120;
constexpr int kResultColumn  = 120;
constexpr int kBadgeHeight   = 28;
} // namespace

void AssessmentCard::setResult (const AssessmentResult& result)
{
    data = result;
    hasResult = true;
    repaint();
}

void AssessmentCard::clearResult()
{
    hasResult = false;
    repaint();
}

void AssessmentCard::paint (juce::Graphics& g)
{
    if (! hasResult)
    {
        // P1-7：无记录时 MainComponent 直接隐藏整张卡片；这里只保留兜底文案
        g.setColour (theme::text2);
        g.setFont (theme::uiFont (theme::typeEyebrow));
        g.drawText (zh ("还没有测评记录"), getLocalBounds(), juce::Justification::centred);
        return;
    }

    auto area = getLocalBounds().reduced (theme::cardPaddingX, theme::cardPaddingY);

    // ---- 头行 20：左 eyebrow、右时间戳（mono），同一条基线
    auto headerRow = area.removeFromTop (kHeaderRow);

    g.setColour (theme::text3);
    g.setFont (theme::uiFont (theme::typeEyebrow));
    g.drawText (zh ("最近一次测评"), headerRow, juce::Justification::centredLeft);

    g.setFont (theme::monoFont (theme::typeMeta));
    // P2-6：成绩卡头行时间戳缩为 MM-dd HH:mm（历史表保留完整日期）
    g.drawText (formatShortDateTime (data.timestamp), headerRow, juce::Justification::centredRight);

    area.removeFromTop (theme::space8);

    // ---- 分数行 48：固定 76 宽数字列右对齐；后缀 +6 与数字基线对齐
    auto scoreRow = area.removeFromTop (kScoreRow);
    auto numberColumn = scoreRow.removeFromLeft (kScoreColumn);

    const auto scoreFont = theme::uiFont (theme::typeHero, true);
    g.setColour (theme::text1);
    g.setFont (scoreFont);
    g.drawText (juce::String (data.score), numberColumn, juce::Justification::centredRight);

    const auto suffixFont = theme::uiFont (theme::typeMeta);
    const auto suffixText = juce::String ("/ 100");
    const float numberBaseline = (float) scoreRow.getY()
                                 + ((float) kScoreRow - scoreFont.getHeight()) * 0.5f
                                 + scoreFont.getAscent();
    // 后缀基线与数字基线对齐（视觉下沉 2px）；区域高度 = 字体 height 时基线 = top + ascent
    const int suffixTop = juce::roundToInt (numberBaseline + 2.0f - suffixFont.getAscent());
    const int suffixX   = scoreRow.getX() + 6;
    const int suffixWidth = textWidth (suffixFont, suffixText);

    g.setColour (theme::text3);
    g.setFont (suffixFont);
    g.drawText (suffixText,
                juce::Rectangle<int> (suffixX, suffixTop, suffixWidth,
                                      juce::roundToInt (suffixFont.getHeight())),
                juce::Justification::centredLeft);

    // ---- 等级徽章：距后缀 16，高 28、圆角 6、14% 等级色底 + 等级色文字（15/600）
    const auto badgeFont = theme::uiFont (theme::typeEyebrow, true);
    const auto colour = gradeColour (data.grade);
    const int badgeWidth = textWidth (badgeFont, data.grade) + 2 * theme::space12;
    const auto badge = juce::Rectangle<float> ((float) (suffixX + suffixWidth + theme::space16),
                                               (float) (scoreRow.getY()
                                                        + (kScoreRow - kBadgeHeight) / 2),
                                               (float) badgeWidth,
                                               (float) kBadgeHeight);

    g.setColour (colour.withAlpha (0.14f));
    g.fillRoundedRectangle (badge, (float) theme::radiusBadge);

    g.setColour (colour);
    g.setFont (badgeFont);
    g.drawText (data.grade, badge.toNearestInt(), juce::Justification::centred);

    area.removeFromTop (theme::space12);

    // ---- 分项 28×2：标签 120 / 结果 120 / JND 右对齐到卡内右缘
    const auto labelFont = theme::uiFont (theme::typeMeta);
    const auto valueFont = theme::monoFont (theme::typeMeta);
    const auto jndLabelFont = theme::uiFont (14.0f);
    const auto jndValueFont = theme::monoFont (14.0f);

    const auto drawRow = [&] (const juce::String& name, int correct, int total, float jnd)
    {
        auto row = area.removeFromTop (kBreakdownRow);

        g.setColour (theme::text2);
        g.setFont (labelFont);
        g.drawText (name, row.removeFromLeft (kLabelColumn), juce::Justification::centredLeft);

        const int pct = total > 0 ? juce::roundToInt (100.0 * correct / total) : 0;

        // P2-1：mono + 中点分隔（原「6 / 6（100%）」全角括号与半角斜杠混用）
        g.setColour (theme::text1);
        g.setFont (valueFont);
        g.drawText (juce::String (correct) + " / " + juce::String (total)
                        + " · " + juce::String (pct) + "%",
                    row.removeFromLeft (kResultColumn), juce::Justification::centredLeft);

        // P2-1：JND 数值用 mono，单位与数字之间恰好一个空格；整段右对齐到卡内右缘
        g.setColour (theme::text3);

        if (jnd > 0.0f)
        {
            const auto label = zh ("估计 JND ");
            const auto value = juce::String (jnd, 0);
            const auto unit  = juce::String (" dB");

            const int labelWidth = textWidth (jndLabelFont, label);
            const int valueWidth = textWidth (jndValueFont, value);
            const int unitWidth  = textWidth (jndValueFont, unit);
            const int x = row.getRight() - labelWidth - valueWidth - unitWidth;
            const int h = juce::roundToInt (jndValueFont.getHeight());
            const int top = row.getCentreY() - h / 2;

            g.setFont (jndLabelFont);
            g.drawText (label, juce::Rectangle<int> (x, top, labelWidth, h),
                        juce::Justification::centredLeft);

            g.setFont (jndValueFont);
            g.drawText (value, juce::Rectangle<int> (x + labelWidth, top, valueWidth, h),
                        juce::Justification::centredLeft);
            g.drawText (unit, juce::Rectangle<int> (x + labelWidth + valueWidth, top, unitWidth, h),
                        juce::Justification::centredLeft);
        }
        else
        {
            g.setFont (jndLabelFont);
            g.drawText (zh ("JND 未测出"), row, juce::Justification::centredRight);
        }
    };

    drawRow (zh ("EQ 频段定位"), data.eqCorrect, data.eqTotal, data.eqJndDb);
    drawRow (zh ("压缩量判断"), data.compCorrect, data.compTotal, data.compJndDb);
}

} // namespace earlab
