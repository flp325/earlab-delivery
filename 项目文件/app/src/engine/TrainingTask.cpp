#include "TrainingTask.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace earlab
{

namespace
{
const std::array<float, 20> kBandCentres {
    100.0f, 125.0f, 160.0f, 200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f, 800.0f,
    1000.0f, 1250.0f, 1600.0f, 2000.0f, 2500.0f, 3150.0f, 4000.0f, 5000.0f, 6300.0f, 8000.0f
};

float roundToHalf (float value)
{
    return std::round (value * 2.0f) * 0.5f;
}
} // namespace

const std::array<float, 20>& TaskGenerator::bandCentres()
{
    return kBandCentres;
}

juce::String TaskGenerator::formatHzLabel (float hz)
{
    if (hz >= 1000.0f)
        return juce::String (hz / 1000.0f, 1) + " kHz";

    return juce::String (juce::roundToInt (hz)) + " Hz";
}

juce::String TaskGenerator::formatDbLabel (float db)
{
    return juce::String::fromUTF8 ("\xE7\xBA\xA6 ") + juce::String (db, 1) + " dB";
}

TaskGenerator::TaskGenerator (std::uint32_t seedToUse)
    : seed (seedToUse), random (seedToUse)
{
}

TrainingTask TaskGenerator::makeEqBandTask (float gainDb, float q)
{
    TrainingTask task;
    task.skill      = Skill::eqBandCenter;
    task.seed       = seed;
    task.gainDb     = juce::jlimit (1.0f, 12.0f, gainDb);
    task.q          = juce::jlimit (0.5f, 6.0f, q);
    task.difficulty = (12.0f - task.gainDb) / 11.0f;

    const auto& centres = kBandCentres;
    const int count = static_cast<int> (centres.size());

    const int correct = random.nextInt (count);
    task.frequencyHz  = centres[static_cast<size_t> (correct)];

    // 干扰项与正确答案至少隔 3 个 1/3 倍频程，避免语义上模棱两可
    std::array<int, 3> distractors {};
    int found = 0;

    while (found < 3)
    {
        const int candidate = random.nextInt (count);

        if (std::abs (candidate - correct) < 3)
            continue;

        bool duplicate = false;

        for (int i = 0; i < found; ++i)
            if (distractors[static_cast<size_t> (i)] == candidate)
                duplicate = true;

        if (duplicate)
            continue;

        distractors[static_cast<size_t> (found++)] = candidate;
    }

    task.correctIndex = random.nextInt (4);

    int next = 0;

    for (int i = 0; i < 4; ++i)
    {
        if (i == task.correctIndex)
            task.optionsHz[static_cast<size_t> (i)] = task.frequencyHz;
        else
            task.optionsHz[static_cast<size_t> (i)] = centres[static_cast<size_t> (distractors[static_cast<size_t> (next++)])];
    }

    // P2-1：选项按频率升序排列（100 / 200 / 630 / 4000 Hz），不再随随机顺序跳动。
    // 只调整「value + 是否正确答案」对的顺序，题量、干扰项规则与正确项集合都不变。
    std::array<std::pair<float, bool>, 4> ascending {};

    for (int i = 0; i < 4; ++i)
        ascending[static_cast<size_t> (i)] = { task.optionsHz[static_cast<size_t> (i)],
                                               i == task.correctIndex };

    std::sort (ascending.begin(), ascending.end(),
               [] (const auto& a, const auto& b) { return a.first < b.first; });

    for (int i = 0; i < 4; ++i)
    {
        task.optionsHz[static_cast<size_t> (i)] = ascending[static_cast<size_t> (i)].first;
        task.optionLabels[static_cast<size_t> (i)] = formatHzLabel (task.optionsHz[static_cast<size_t> (i)]);

        if (ascending[static_cast<size_t> (i)].second)
            task.correctIndex = i;
    }

    // 主变化
    task.bandMoves[0] = { task.frequencyHz, task.gainDb, task.q };
    task.numBandMoves = 1;

    // 较响的题里加入一段更弱的背景变化：更接近真实 EQ，
    // 但频率远离正确项和所有选项，避免语义歧义。
    if (task.gainDb > 4.0f && random.nextFloat() < 0.65f)
    {
        for (int attempt = 0; attempt < 24; ++attempt)
        {
            const int index = random.nextInt (count);

            if (std::abs (index - correct) < 4)
                continue;

            bool clashes = false;

            for (const auto hz : task.optionsHz)
                if (hz == centres[static_cast<size_t> (index)])
                    clashes = true;

            if (clashes)
                continue;

            const float magnitude = juce::jmin (3.0f, task.gainDb * 0.45f);

            task.bandMoves[1] = { centres[static_cast<size_t> (index)],
                                  random.nextBool() ? magnitude : -magnitude,
                                  0.8f + random.nextFloat() * 1.4f };
            task.numBandMoves = 2;
            break;
        }
    }

    return task;
}

TrainingTask TaskGenerator::makeCompAmountTask (float targetGrDb)
{
    TrainingTask task;
    task.skill          = Skill::compAmount;
    task.seed           = seed;

    // 正确答案就是「实际测到的 GR」，避免出现标称 12 dB、实际只有 6 dB 的错题
    const float correct = juce::jlimit (0.5f, 24.0f, roundToHalf (targetGrDb));
    task.targetGrDb     = correct;
    task.difficulty     = juce::jlimit (0.0f, 1.0f, (12.0f - correct) / 10.5f);
    task.compRatio      = 4.0f;
    task.compAttackMs   = 10.0f;
    task.compReleaseMs  = 100.0f;
    task.compKneeDb     = 6.0f;
    task.compThresholdDb = -30.0f;   // 实际阈值由引擎按素材校准

    std::array<float, 4> values { correct, 0.0f, 0.0f, 0.0f };
    int count = 1;

    // 干扰项：围绕正确答案取 ±1..6 dB，限制在 0.5–24，且与已有项至少差 0.5 dB。
    // 旧实现用 0.5×/2×/4× 倍率再 +0.5 补重复，会在 12 dB 时造出 24 与 24.5 这种听不出差别的选项。
    static const float offsets[] = { -6.0f, -4.0f, -2.0f, -1.0f, 1.0f, 2.0f, 4.0f, 6.0f };
    std::vector<float> pool;

    for (const float offset : offsets)
    {
        const float value = juce::jlimit (0.5f, 24.0f, roundToHalf (correct + offset));

        if (std::abs (value - correct) >= 1.0f)
            pool.push_back (value);
    }

    for (int i = static_cast<int> (pool.size()) - 1; i > 0; --i)
        std::swap (pool[static_cast<size_t> (i)],
                   pool[static_cast<size_t> (random.nextInt (i + 1))]);

    for (const float value : pool)
    {
        if (count >= 4)
            break;

        bool duplicate = false;

        for (int j = 0; j < count; ++j)
            if (std::abs (values[static_cast<size_t> (j)] - value) < 1.0f)
                duplicate = true;

        if (! duplicate)
            values[static_cast<size_t> (count++)] = value;
    }

    // 极端值兜底（正常情况下不会走到）
    for (float value = 0.5f; count < 4 && value <= 24.0f; value += 1.0f)
    {
        bool duplicate = false;

        for (int j = 0; j < count; ++j)
            if (std::abs (values[static_cast<size_t> (j)] - value) < 1.0f)
                duplicate = true;

        if (! duplicate)
            values[static_cast<size_t> (count++)] = value;
    }

    for (int i = 3; i > 0; --i)
    {
        const int j = random.nextInt (i + 1);
        std::swap (values[static_cast<size_t> (i)], values[static_cast<size_t> (j)]);
    }

    for (int i = 0; i < 4; ++i)
    {
        task.optionLabels[static_cast<size_t> (i)] = formatDbLabel (values[static_cast<size_t> (i)]);

        if (std::abs (values[static_cast<size_t> (i)] - correct) < 0.01f)
            task.correctIndex = i;
    }

    return task;
}

//==============================================================================
Staircase::Staircase (float startDb, float minDbToUse, float maxDbToUse)
    : minDb (minDbToUse),
      maxDb (maxDbToUse),
      current (juce::jlimit (minDbToUse, maxDbToUse, startDb)),
      step (juce::jmax (0.25f, (maxDbToUse - minDbToUse) / 8.0f))
{
}

void Staircase::reset()
{
    current = juce::jlimit (minDb, maxDb, (minDb + maxDb) * 0.5f);
    step = juce::jmax (0.25f, (maxDb - minDb) / 8.0f);
    correctRun = 0;
    lastDirection = 0;
    reversals.clear();
}

void Staircase::registerAnswer (bool correct)
{
    int direction = 0;

    if (correct)
    {
        if (++correctRun >= 2)
        {
            correctRun = 0;
            current -= step;
            direction = -1;
        }
    }
    else
    {
        correctRun = 0;
        current += step;
        direction = +1;
    }

    current = juce::jlimit (minDb, maxDb, current);

    if (direction != 0)
    {
        if (lastDirection != 0 && direction != lastDirection)
        {
            reversals.push_back (current);
            step = juce::jmax (0.25f, step * 0.5f);
        }

        lastDirection = direction;
    }
}

float Staircase::getThresholdDb() const
{
    if (reversals.empty())
        return current;

    const int count = juce::jmin (8, static_cast<int> (reversals.size()));
    float sum = 0.0f;

    for (int i = 0; i < count; ++i)
        sum += reversals[reversals.size() - static_cast<size_t> (count) + static_cast<size_t> (i)];

    return sum / static_cast<float> (count);
}

//==============================================================================
EloRating::EloRating (double initial) : rating (initial)
{
}

void EloRating::reset (double initial)
{
    rating = initial;
    trials = 0;
}

void EloRating::update (bool correct, float difficulty01)
{
    const double taskRating = 800.0 + 400.0 * static_cast<double> (juce::jlimit (0.0f, 1.0f, difficulty01));
    const double expected   = 1.0 / (1.0 + std::pow (10.0, (taskRating - rating) / 400.0));

    rating += 24.0 * ((correct ? 1.0 : 0.0) - expected);
    ++trials;
}

} // namespace earlab
