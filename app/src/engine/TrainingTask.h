#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <vector>

namespace earlab
{

enum class Skill
{
    eqBandCenter = 0,
    compAmount   = 1,
    pluginBand   = 2
};

/** EQ 题里的一段变化；第 0 段是主变化（决定答案）。 */
struct EqBandMove
{
    float frequencyHz = 1000.0f;
    float gainDb = 0.0f;
    float q = 1.0f;
};

struct TrainingTask
{
    Skill skill = Skill::eqBandCenter;
    std::uint32_t seed = 0;

    std::array<juce::String, 4> optionLabels;
    int correctIndex = 0;
    float difficulty = 0.5f;   // 0 易 .. 1 难

    // EQ 题
    float frequencyHz = 1000.0f;
    float gainDb = 6.0f;
    float q = 1.0f;
    std::array<float, 4> optionsHz { 250.0f, 500.0f, 1000.0f, 2000.0f };
    std::array<EqBandMove, 3> bandMoves {};
    int numBandMoves = 1;

    // 压缩题
    float targetGrDb = 6.0f;
    float compThresholdDb = -30.0f;
    float compRatio = 4.0f;
    float compAttackMs = 10.0f;
    float compReleaseMs = 100.0f;
    float compKneeDb = 6.0f;
};

/** 确定性出题器：同一 seed + 同一难度，必须生成同一道题。 */
class TaskGenerator
{
public:
    explicit TaskGenerator (std::uint32_t seed = 1);

    TrainingTask makeEqBandTask (float gainDb, float q);
    TrainingTask makeCompAmountTask (float targetGrDb);

    static const std::array<float, 20>& bandCentres();
    static juce::String formatHzLabel (float hz);
    static juce::String formatDbLabel (float db);

private:
    std::uint32_t seed;
    juce::Random random;
};

/**
    2-down-1-up 阶梯法：连续两次答对降难度，答错一次升难度。
    方向反转时步长减半，阈值取最后几次反转的均值（约 70.7% 正确率）。
*/
class Staircase
{
public:
    Staircase (float startDb, float minDb, float maxDb);

    void registerAnswer (bool correct);
    void reset();

    [[nodiscard]] float getCurrentDb() const noexcept { return current; }
    [[nodiscard]] float getThresholdDb() const;
    [[nodiscard]] int getReversals() const noexcept { return static_cast<int> (reversals.size()); }

private:
    float minDb, maxDb, current, step;
    int correctRun = 0;
    int lastDirection = 0;
    std::vector<float> reversals;
};

/** 简化 Elo：题目难度映射成分值，答对涨、答错跌。 */
class EloRating
{
public:
    explicit EloRating (double initial = 1000.0);

    void update (bool correct, float difficulty01);
    void reset (double initial = 1000.0);

    [[nodiscard]] double getRating() const noexcept { return rating; }
    [[nodiscard]] int getTrials() const noexcept { return trials; }

private:
    double rating = 1000.0;
    int trials = 0;
};

} // namespace earlab
