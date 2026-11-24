#include "PluginTask.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace earlab::plugin
{

namespace
{
constexpr float minUsableDeltaDb = 2.0f;   // 小于这个幅度不算「听得出来」
constexpr int calibrationSteps = 8;

juce::String directionLabel (float hz, int direction)
{
    const auto band = PluginProbe::formatBandLabel (hz);
    return band + (direction > 0 ? utf8 (" 附近被提升") : utf8 (" 附近被衰减"));
}

juce::String amountLabel (float db)
{
    return utf8 ("约 ") + juce::String (juce::roundToInt (db)) + " dB";
}

const std::array<float, 5>& bucketTable()
{
    static const std::array<float, 5> buckets { 2.0f, 4.0f, 6.0f, 8.0f, 10.0f };
    return buckets;
}

int nearestBucketIndex (float value)
{
    int best = 0;
    float bestError = 1.0e9f;

    for (int i = 0; i < static_cast<int> (bucketTable().size()); ++i)
    {
        const float error = std::abs (bucketTable()[static_cast<size_t> (i)] - value);

        if (error < bestError)
        {
            bestError = error;
            best = i;
        }
    }

    return best;
}

std::vector<int> shuffledBands (juce::Random& random)
{
    std::vector<int> order;

    for (int i = 0; i < static_cast<int> (PluginProbe::bandCentres().size()); ++i)
        order.push_back (i);

    for (int i = static_cast<int> (order.size()) - 1; i > 0; --i)
        std::swap (order[static_cast<size_t> (i)],
                   order[static_cast<size_t> (random.nextInt (i + 1))]);

    return order;
}
} // namespace

PluginTaskGenerator::PluginTaskGenerator (std::uint32_t newSeed)
    : seed (newSeed), random (newSeed)
{
}

void PluginTaskGenerator::applyToPlugin (juce::AudioPluginInstance& instance, const PluginTask& task)
{
    PluginHost::setAllToDefault (instance);

    for (const auto& [index, value] : task.helperSwitches)
        PluginHost::setParameter (instance, index, value);

    if (task.parameterIndex >= 0)
        PluginHost::setParameter (instance, task.parameterIndex, task.parameterValue);
}

const std::array<float, 5>& amountBuckets()
{
    return bucketTable();
}

PluginOptions buildAmountOptions (float measuredAbsDb, juce::Random& random)
{
    PluginOptions result;

    const auto& buckets = bucketTable();
    const int bucket = nearestBucketIndex (measuredAbsDb);
    const int start = juce::jlimit (0, static_cast<int> (buckets.size()) - 4, bucket - 1);

    juce::Array<juce::String> labels;

    for (int i = 0; i < 4; ++i)
        labels.add (amountLabel (buckets[static_cast<size_t> (start + i)]));

    for (int i = labels.size() - 1; i > 0; --i)
        labels.swap (i, random.nextInt (i + 1));

    const auto correctLabel = amountLabel (buckets[static_cast<size_t> (bucket)]);
    result.correctIndex = labels.indexOf (correctLabel);
    result.valid = result.correctIndex >= 0 && labels.size() == 4;

    for (int i = 0; i < 4; ++i)
        result.labels[static_cast<size_t> (i)] = labels[i];

    return result;
}

PluginOptions buildDirectionOptions (bool broadband, int bandIndex, int direction, juce::Random& random)
{
    PluginOptions result;

    const auto& centres = PluginProbe::bandCentres();
    const int safeBand = juce::jlimit (0, static_cast<int> (centres.size()) - 1, bandIndex);
    const juce::String correct = broadband
        ? (direction >= 0 ? utf8 ("整体变响") : utf8 ("整体变轻"))
        : directionLabel (centres[static_cast<size_t> (safeBand)], direction >= 0 ? 1 : -1);

    juce::Array<juce::String> labels;

    if (broadband)
    {
        labels.add (utf8 ("整体变响"));
        labels.add (utf8 ("整体变轻"));
    }

    const auto order = shuffledBands (random);

    for (int band : order)
    {
        if (labels.size() >= (broadband ? 4 : 3))
            break;

        if (! broadband && std::abs (band - safeBand) < 2)
            continue;

        const int bandDirection = random.nextBool() ? 1 : -1;
        const auto label = directionLabel (centres[static_cast<size_t> (band)], bandDirection);

        if (label == correct || labels.contains (label))
            continue;

        labels.add (label);
    }

    // 兜底：极端随机下也可能凑不满，按顺序补
    for (int band = 0; labels.size() < (broadband ? 4 : 3)
                            && band < static_cast<int> (centres.size()); ++band)
    {
        const auto label = directionLabel (centres[static_cast<size_t> (band)], 1);

        if (label != correct && ! labels.contains (label))
            labels.add (label);
    }

    if (! broadband)
        labels.insert (random.nextInt (labels.size() + 1), correct);

    // 洗牌
    for (int i = labels.size() - 1; i > 0; --i)
        labels.swap (i, random.nextInt (i + 1));

    result.correctIndex = labels.indexOf (correct);
    result.valid = result.correctIndex >= 0 && labels.size() == 4;

    for (int i = 0; i < 4; ++i)
        result.labels[static_cast<size_t> (i)] = labels[i];

    return result;
}

PluginTask PluginTaskGenerator::generate (juce::AudioPluginInstance& instance,
                                          const PluginInfo& info,
                                          const std::vector<HostParameter>& parameters,
                                          float targetDeltaDb,
                                          float difficulty,
                                          double sampleRate,
                                          int preferredParameterIndex,
                                          PluginQuestionKind preferredKind,
                                          std::function<bool()> shouldExit)
{
    PluginTask task;
    task.pluginName = info.name;
    task.pluginFile = info.fileOrIdentifier;
    task.seed = seed;
    task.targetDeltaDb = targetDeltaDb;
    task.difficulty = difficulty;

    const auto candidates = PluginHost::trainingCandidates (parameters, 24);

    // ---- 1. 找真正能改变声音的参数
    struct Candidate
    {
        const HostParameter* parameter = nullptr;
        float bestDelta = 0.0f;
        float bestValue = 1.0f;
        float score = 0.0f;
        ProbeResult result;
        int band = 0;
        std::vector<std::pair<int, float>> switches;
    };

    std::vector<Candidate> usable;

    for (const auto& candidate : candidates)
    {
        // 用户点了取消就尽快退出，不要继续跑几十次离线渲染
        if (shouldExit && shouldExit())
            return task;

        // 已知哪个参数有效时直接用它，跳过整轮探测（每题省几秒）
        if (preferredParameterIndex >= 0 && candidate.index != preferredParameterIndex)
            continue;

        PluginHost::setAllToDefault (instance);
        auto switches = PluginHost::activateRelatedSwitches (instance, parameters, candidate.index);

        const auto low = PluginProbe::measureWithParameter (instance, sampleRate, candidate.index, 0.0f);
        const auto high = PluginProbe::measureWithParameter (instance, sampleRate, candidate.index, 1.0f);

        if (shouldExit && shouldExit())
            return task;

        if (! low.ok || ! high.ok)
            continue;

        const bool lowStronger = std::abs (low.dominantDeltaDb) >= std::abs (high.dominantDeltaDb);
        const auto& best = lowStronger ? low : high;
        const float bestValue = lowStronger ? 0.0f : 1.0f;
        const float bestDelta = best.dominantDeltaDb;

        if (std::abs (bestDelta) < minUsableDeltaDb)
            continue;

        // 局部频段变化优先：宽带变化往往只是音量，题目没意思
        const bool localized = ! PluginProbe::isBroadband (best);
        const float score = std::abs (bestDelta) * (localized ? 1.0f : 0.35f);

        Candidate entry;
        entry.parameter = &candidate;
        entry.bestDelta = bestDelta;
        entry.bestValue = bestValue;
        entry.score = score;
        entry.result = best;
        entry.band = best.dominantBand;
        entry.switches = std::move (switches);
        usable.push_back (std::move (entry));
    }

    if (usable.empty())
        return task;   // parameterIndex 仍是 -1，调用方据此判定失败

    std::stable_sort (usable.begin(), usable.end(), [] (const Candidate& a, const Candidate& b)
    {
        return a.score > b.score;
    });

    // 从前几名里随机选，避免每次都是同一个参数
    const int poolSize = juce::jmin (3, static_cast<int> (usable.size()));
    const auto& chosen = usable[static_cast<size_t> (random.nextInt (poolSize))];

    task.parameterIndex = chosen.parameter->index;
    task.parameterName  = chosen.parameter->name;
    task.helperSwitches = chosen.switches;

    // ---- 2. 题型与校准目标
    const bool measuredBroadband = PluginProbe::isBroadband (chosen.result);

    PluginQuestionKind kind = preferredKind;

    if (kind == PluginQuestionKind::automatic)
        kind = measuredBroadband ? PluginQuestionKind::levelAmount
                                 : (random.nextBool() ? PluginQuestionKind::bandDirection
                                                      : PluginQuestionKind::bandAmount);

    if (kind == PluginQuestionKind::levelAmount && ! measuredBroadband)
        kind = PluginQuestionKind::bandAmount;   // 没有宽带效果就退成频段题

    task.kind = kind;

    const bool isAmount = kind != PluginQuestionKind::bandDirection;
    const int bucketIndex = nearestBucketIndex (targetDeltaDb);
    const float calibrationTarget = isAmount ? bucketTable()[static_cast<size_t> (bucketIndex)]
                                             : targetDeltaDb;

    const float defaultValue = chosen.parameter->defaultValue;
    const bool searchDown = chosen.bestValue < 0.5f;
    float lo = searchDown ? 0.0f : juce::jlimit (0.0f, 1.0f, defaultValue);
    float hi = searchDown ? juce::jlimit (0.0f, 1.0f, defaultValue) : 1.0f;

    const auto magnitudeOf = [kind] (const ProbeResult& probe)
    {
        return kind == PluginQuestionKind::levelAmount ? std::abs (probe.broadbandDeltaDb)
                                                       : std::abs (probe.dominantDeltaDb);
    };

    float bestValue = chosen.bestValue;
    ProbeResult bestResult = chosen.result;
    float bestError = std::abs (magnitudeOf (chosen.result) - calibrationTarget);

    // Pro-Q 这类插件的 DSP 只在整批参数同步后才跟随控制器状态，
    // 所以每次测量前都重放「默认值 + 开关」再写目标参数。
    const auto resetToBase = [&instance, &task]()
    {
        PluginHost::setAllToDefault (instance);

        for (const auto& [index, value] : task.helperSwitches)
            PluginHost::setParameter (instance, index, value);
    };

    for (int step = 0; step < calibrationSteps; ++step)
    {
        if (shouldExit && shouldExit())
            return task;

        const float mid = 0.5f * (lo + hi);
        resetToBase();
        const auto probe = PluginProbe::measureWithParameter (instance, sampleRate, task.parameterIndex, mid);

        if (! probe.ok)
            break;

        const float magnitude = magnitudeOf (probe);
        const float error = std::abs (magnitude - calibrationTarget);

        if (error < bestError)
        {
            bestError = error;
            bestValue = mid;
            bestResult = probe;
        }

        if (magnitude < calibrationTarget)
        {
            // 幅度不够：往「效果更强」的一端推
            if (searchDown) hi = mid; else lo = mid;
        }
        else
        {
            // 幅度过大：往默认值退
            if (searchDown) lo = mid; else hi = mid;
        }
    }

    task.parameterValue = bestValue;
    task.bandIndex = bestResult.dominantBand;
    task.broadband = PluginProbe::isBroadband (bestResult);

    const float signedMagnitude = kind == PluginQuestionKind::levelAmount
                                      ? bestResult.broadbandDeltaDb
                                      : bestResult.dominantDeltaDb;

    const float measuredAbs = std::abs (signedMagnitude);
    task.measuredDeltaDb = signedMagnitude;
    task.direction = signedMagnitude >= 0.0f ? 1 : -1;

    // 校准没落到档位上时不要假装：能听出来就降级成方向题，否则判失败
    if (task.kind != PluginQuestionKind::bandDirection)
    {
        const float measuredBucket = bucketTable()[static_cast<size_t> (nearestBucketIndex (measuredAbs))];

        if (std::abs (measuredAbs - measuredBucket) > 1.5f)
        {
            if (measuredAbs < minUsableDeltaDb)
            {
                task.parameterIndex = -1;
                return task;
            }

            task.kind = PluginQuestionKind::bandDirection;
        }
    }

    // ---- 3. 选项：正确项由「实际测到的幅度」决定
    const auto options = task.kind == PluginQuestionKind::bandDirection
                             ? buildDirectionOptions (task.broadband, task.bandIndex,
                                                      task.direction, random)
                             : buildAmountOptions (measuredAbs, random);

    if (! options.valid)
    {
        task.parameterIndex = -1;
        return task;
    }

    task.optionLabels = options.labels;
    task.correctIndex = options.correctIndex;
    task.amountDb = task.kind == PluginQuestionKind::bandDirection
                        ? 0.0f
                        : bucketTable()[static_cast<size_t> (nearestBucketIndex (measuredAbs))];
    return task;
}

} // namespace earlab::plugin
