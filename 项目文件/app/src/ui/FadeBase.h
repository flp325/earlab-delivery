#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace earlab
{

/** P2-5：LookAndFeel / 自绘按钮读取过渡值的接口（不看 Timer 内部实现）。 */
class FadeProvider
{
public:
    virtual ~FadeProvider() = default;

    [[nodiscard]] virtual float getHoverFade() const = 0;
    [[nodiscard]] virtual float getPressFade() const = 0;
};

/** P2-5：120ms ease-out 的 0..1 过渡值。

    与 60Hz Timer 配合使用；settle 后 isAnimating() 返回 false，调用方可停表。
*/
class FadeValue
{
public:
    static constexpr double durationMs = 120.0;

    explicit FadeValue (float initial = 0.0f)
        : current (initial), startValue (initial), target (initial)
    {
    }

    /** 改变目标并从头计时；目标未变化时返回 false（不用重启动画）。 */
    bool setTarget (float newTarget)
    {
        newTarget = juce::jlimit (0.0f, 1.0f, newTarget);

        if (newTarget == target)
            return false;

        startValue = current;
        startTimeMs = juce::Time::getMillisecondCounterHiRes();
        target = newTarget;
        return true;
    }

    [[nodiscard]] float get() const noexcept { return current; }
    [[nodiscard]] bool isAnimating() const noexcept { return current != target; }

    /** 推进一帧（内部按真实毫秒计时，Timer 频率抖动不会拖长总时长）。 */
    bool step()
    {
        if (! isAnimating())
            return false;

        const double elapsed = juce::Time::getMillisecondCounterHiRes() - startTimeMs;
        const double progress = juce::jlimit (0.0, 1.0, elapsed / durationMs);
        const double eased = 1.0 - std::pow (1.0 - progress, 3.0);   // ease-out cubic

        current = progress >= 1.0 ? target
                                  : static_cast<float> (startValue + (target - startValue) * eased);

        return isAnimating();
    }

private:
    float current = 0.0f;
    float startValue = 0.0f;
    float target = 0.0f;
    double startTimeMs = 0.0;
};

/**
    P2-5：给任意 juce::Button 基类加 120ms hover / 按下过渡。

    hover 与按下各维护一个 0..1 的 FadeValue；只在其一动起来时开 60Hz Timer，
    两个值都 settle 后停表。派生类实现 paintFaded() 即可；TextButton 子类
    通过 LookAndFeel 读取 FadeProvider 的插值值（LabButton）。
*/
template <typename Base>
class FadeBase : public Base, private juce::Timer, public FadeProvider
{
public:
    using Base::Base;

    [[nodiscard]] float getHoverFade() const override { return hoverFade.get(); }
    [[nodiscard]] float getPressFade() const override { return pressFade.get(); }

protected:
    void paintButton (juce::Graphics& g, bool, bool isButtonDown) override
    {
        // 鼠标进入/离开、按下/抬起都会触发 repaint（Button 自身行为），
        // 因此在绘制时刷新目标即可；动画帧由 Timer 驱动。
        // hover 用真实鼠标位置（键盘按下时 Button 的 isOver() 也会为真，不能拿来当 hover）。
        hoverFade.setTarget (this->isMouseOver (true) && this->isEnabled() ? 1.0f : 0.0f);
        pressFade.setTarget (isButtonDown && this->isEnabled() ? 1.0f : 0.0f);

        if (hoverFade.isAnimating() || pressFade.isAnimating())
        {
            if (! isTimerRunning())
                startTimerHz (60);
        }

        paintFaded (g, hoverFade.get(), pressFade.get());
    }

    /** hoverAmount / pressAmount 均为 ease-out 后的 0..1。 */
    virtual void paintFaded (juce::Graphics& g, float hoverAmount, float pressAmount) = 0;

private:
    void timerCallback() override
    {
        hoverFade.step();
        pressFade.step();
        this->repaint();

        if (! hoverFade.isAnimating() && ! pressFade.isAnimating())
            stopTimer();
    }

    FadeValue hoverFade, pressFade;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FadeBase)
};

} // namespace earlab
