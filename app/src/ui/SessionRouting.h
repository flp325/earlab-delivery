#pragma once

#include "../engine/TrainingTask.h"

namespace earlab::ui
{

/**
    会话归属的纯逻辑（U-P1-1 / U-P1-3）。

    MainComponent 的控件可见性与键盘守卫都走这里，避免再次出现
    「测评控件泄漏到训练页」「首页 Enter 重开一轮」这类状态机错误。
    这里是 header-only 纯函数，便于在无 GUI 的引擎测试里断言。
*/
enum class SessionKind
{
    none,
    training,
    assessment
};

inline SessionKind sessionKindOf (bool trainingActive, bool assessmentActive) noexcept
{
    if (! trainingActive)
        return SessionKind::none;

    return assessmentActive ? SessionKind::assessment : SessionKind::training;
}

/** 会话是否属于该页：训练会话属于训练页（插件训练属于 VST 页），测评只属于测评页。 */
inline bool sessionHostedOnPage (bool trainingPage, bool pluginPage, bool assessmentPage,
                                 SessionKind kind, Skill activeSkill) noexcept
{
    if (kind == SessionKind::none)
        return false;

    if (kind == SessionKind::assessment)
        return assessmentPage;

    return activeSkill == Skill::pluginBand ? pluginPage : trainingPage;
}

/** 本轮小结只在训练会话的宿主页显示（结束后 trainingActive 为 false，仍按技能判断）。 */
inline bool summaryHostedOnPage (bool trainingPage, bool pluginPage,
                                 bool summaryVisible, bool assessmentActive, Skill activeSkill) noexcept
{
    if (! summaryVisible || assessmentActive)
        return false;

    return activeSkill == Skill::pluginBand ? pluginPage : trainingPage;
}

/** 1–4 作答键：必须有进行中的会话、未作答、无小结，且当前页是会话宿主页。 */
inline bool canAcceptAnswerKey (bool trainingPage, bool pluginPage, bool assessmentPage,
                                SessionKind kind, Skill activeSkill,
                                bool answered, bool summaryVisible) noexcept
{
    return kind != SessionKind::none && ! answered && ! summaryVisible
        && sessionHostedOnPage (trainingPage, pluginPage, assessmentPage, kind, activeSkill);
}

/** Enter 触发小结「再练一轮」：小结按钮必须真实可见，且当前页是宿主页。 */
inline bool canActivateSummaryWithEnter (bool summaryVisible, bool summaryButtonsVisible,
                                         bool trainingPage, bool pluginPage,
                                         bool assessmentActive, Skill activeSkill) noexcept
{
    return summaryVisible && summaryButtonsVisible
        && summaryHostedOnPage (trainingPage, pluginPage, summaryVisible, assessmentActive, activeSkill);
}

} // namespace earlab::ui
