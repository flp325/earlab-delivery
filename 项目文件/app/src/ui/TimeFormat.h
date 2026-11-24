#pragma once

#include <juce_core/juce_core.h>

namespace earlab
{

/** 统一的中文时间格式：2026-10-24 18:18 */
inline juce::String formatDateTime (juce::int64 milliseconds)
{
    const juce::Time time (milliseconds);

    return juce::String::formatted ("%04d-%02d-%02d %02d:%02d",
                                    time.getYear(),
                                    time.getMonth() + 1,
                                    time.getDayOfMonth(),
                                    time.getHours(),
                                    time.getMinutes());
}

/** P2-6：成绩卡头行 / 页头胶囊用的短时间：10-25 12:13（历史表仍用完整日期） */
inline juce::String formatShortDateTime (juce::int64 milliseconds)
{
    const juce::Time time (milliseconds);

    return juce::String::formatted ("%02d-%02d %02d:%02d",
                                    time.getMonth() + 1,
                                    time.getDayOfMonth(),
                                    time.getHours(),
                                    time.getMinutes());
}

} // namespace earlab
