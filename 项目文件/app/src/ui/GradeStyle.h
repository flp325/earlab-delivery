#pragma once

#include "Theme.h"

namespace earlab
{

/** P2-2：等级色的唯一来源。

    成绩卡徽章（14% 底 + 等级色文字）与历史行圆点共用本函数；页头胶囊只承载
    页面级状态（进行中 / 已完成 / 题量时长），不再重复等级与分数。
*/
inline juce::Colour gradeColour (const juce::String& grade)
{
    if (grade == juce::String::fromUTF8 ("专业级")) return theme::success;
    if (grade == juce::String::fromUTF8 ("熟练级")) return theme::accent;
    if (grade == juce::String::fromUTF8 ("进阶级")) return theme::warn;

    return theme::danger;
}

} // namespace earlab
