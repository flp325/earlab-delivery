#pragma once

#include "../engine/AudioEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace earlab
{

/**
    EQ 曲线面板。

    曲线来自多段 EQ 的幅频响应（解析计算）。
    拖节点改频率/增益，滚轮改 Q，双击开关该段；训练时只显示不可编辑。
*/
class EqCurvePanel final : public juce::Component,
                           private juce::Timer
{
public:
    explicit EqCurvePanel (AudioEngine& engineToUse);

    void paint (juce::Graphics& g) override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    /** 训练时禁止编辑，只显示。 */
    void setEditable (bool shouldBeEditable);

private:
    void timerCallback() override;

    [[nodiscard]] juce::Rectangle<float> getPlot() const;
    [[nodiscard]] float xForFrequency (float hz) const;
    [[nodiscard]] float frequencyForX (float x) const;
    [[nodiscard]] float yForDb (float db) const;
    [[nodiscard]] float dbForY (float y) const;
    [[nodiscard]] int bandAt (juce::Point<float> point) const;
    [[nodiscard]] int edgeAt (juce::Point<float> point) const;

    AudioEngine& engine;

    std::vector<float> responseFrequencies;

    bool editable = true;
    int draggingBand = -1;
    int draggingEdge = 0;   // 1 = 高通, -1 = 低通

    // 编辑后等手停下来再重算等响，避免拖动时每个事件都离线渲染 20 秒素材
    bool trimDirty = false;
    juce::uint32 lastEditMs = 0;

    // 素材/噪声载入或卸载后，空态提示需要立即消失/出现
    bool lastHasMaterial = false;

    static constexpr float minFreq = 20.0f;
    static constexpr float maxFreq = 20000.0f;
    static constexpr float maxDb = 18.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqCurvePanel)
};

} // namespace earlab
