#pragma once

#include "PluginHost.h"
#include "PluginProbe.h"

#include <array>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace earlab::plugin
{

/** 题目类型：方向辨识 / 增益量 / 整体电平量。 */
enum class PluginQuestionKind
{
    automatic     = -1,
    bandDirection = 0,
    bandAmount    = 1,
    levelAmount   = 2
};

/**
    一道插件题。

    题目只用效果器自己的参数制造差异，不预设插件类型：
    先测哪个参数真的能改变频谱，再把幅度二分校准到目标难度。
*/
struct PluginTask
{
    juce::String pluginName;
    juce::String pluginFile;
    juce::String parameterName;

    PluginQuestionKind kind = PluginQuestionKind::bandDirection;

    int parameterIndex = -1;
    float parameterValue = 0.0f;                      // 归一化
    std::vector<std::pair<int, float>> helperSwitches; // 需要一起打开的开关

    std::array<juce::String, 4> optionLabels;
    int correctIndex = 0;

    float difficulty = 0.5f;       // 0 易 .. 1 难
    float targetDeltaDb = 6.0f;    // 出题目标幅度
    float measuredDeltaDb = 0.0f;  // 实际测到的幅度
    float amountDb = 0.0f;         // 幅度题的档位
    int direction = 1;             // +1 提升 / -1 衰减
    int bandIndex = 0;
    bool broadband = false;
    std::uint32_t seed = 0;
};

/** 幅度题的固定档位（dB）。 */
[[nodiscard]] const std::array<float, 5>& amountBuckets();

/** 选项构造结果：4 个标签互不相同，correctIndex 指向唯一正确项。 */
struct PluginOptions
{
    std::array<juce::String, 4> labels;
    int correctIndex = 0;
    bool valid = false;
};

/**
    按「实测幅度」生成幅度题选项（纯函数，不需要插件，便于单测）。

    正确项取离实测值最近的档位；四个选项是包含该档位的连续四档。
*/
PluginOptions buildAmountOptions (float measuredAbsDb, juce::Random& random);

/**
    方向题选项（纯函数）。

    宽带题使用「整体变响 / 整体变轻」加两个频段干扰项；
    频段题只放远离正确频段的标签。保证四个选项互不相同且正确项只出现一次。
*/
PluginOptions buildDirectionOptions (bool broadband, int bandIndex, int direction,
                                     juce::Random& random);

/**
    插件出题器。

    生成过程会反复渲染（每个候选参数 2 次、校准约 12 次），
    因此必须在非音频线程调用。
*/
class PluginTaskGenerator
{
public:
    explicit PluginTaskGenerator (std::uint32_t seed = 1);

    /** 在当前插件状态上测一遍，找最能改变声音的参数，并把幅度校准到 targetDeltaDb。
        shouldExit 返回 true 时尽快放弃并返回空任务（parameterIndex < 0）。 */
    PluginTask generate (juce::AudioPluginInstance& instance,
                         const PluginInfo& info,
                         const std::vector<HostParameter>& parameters,
                         float targetDeltaDb,
                         float difficulty,
                         double sampleRate = 48000.0,
                         int preferredParameterIndex = -1,
                         PluginQuestionKind preferredKind = PluginQuestionKind::automatic,
                         std::function<bool()> shouldExit = {});

    /** 把任务描述还原到插件上（渲染前调用）。 */
    static void applyToPlugin (juce::AudioPluginInstance& instance, const PluginTask& task);

private:
    std::uint32_t seed;
    juce::Random random;
};

} // namespace earlab::plugin
