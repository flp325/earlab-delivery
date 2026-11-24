#pragma once

#include "TrainingTask.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace earlab
{

/** 一次测评的最终结果（永久保存，可在测评页回看）。 */
struct AssessmentResult
{
    juce::int64 timestamp = 0;

    int score = 0;                    // 0–100
    juce::String grade;               // 入门级 / 进阶级 / 熟练级 / 专业级

    int eqCorrect = 0, eqTotal = 0;
    int compCorrect = 0, compTotal = 0;

    float eqJndDb = 0.0f;             // 答对的最小增益，作为 JND 估计
    float compJndDb = 0.0f;
};

/**
    测评结果仓库：JSON 落盘（临时文件 + rename），历史按时间倒序保存。

    文件按 profile 文件名派生（D-P1-1）：`profile.json` → `assessments-profile.json`，
    `alice.json` → `assessments-alice.json`，同目录多档案不再共用一份历史。
    兼容旧版本：新文件不存在时回退读取同目录的 `assessments.json`；
    新写入只写新文件，旧文件保持原样（不会悄悄改写或删除别人的历史）。
*/
class AssessmentStore
{
public:
    enum class LoadState
    {
        ok,
        missing,               // 主文件与旧版文件都不存在
        recoveredFromBackup,   // 主文件损坏，已从 .bak 恢复
        corrupted              // 主文件损坏且 .bak 也不可用
    };

    static juce::File defaultPathFor (const juce::File& profileFile);
    /** 旧版本共用路径（只读兼容，不会作为写入目标）。 */
    static juce::File legacyPathFor (const juce::File& profileFile);

    bool load (const juce::File& file);
    /** 按 profile 载入：优先新文件，不存在时回退旧版同目录 assessments.json。 */
    bool loadForProfile (const juce::File& profileFile);
    bool save (const juce::File& file);

    void add (const AssessmentResult& result);
    void clear();

    [[nodiscard]] const std::vector<AssessmentResult>& getResults() const noexcept { return results; }
    [[nodiscard]] int getCount() const noexcept { return static_cast<int> (results.size()); }

    [[nodiscard]] LoadState getLoadState() const noexcept { return loadState; }
    [[nodiscard]] bool didRecoverFromBackup() const noexcept
    {
        return loadState == LoadState::recoveredFromBackup;
    }
    [[nodiscard]] bool didUseLegacyFile() const noexcept { return usedLegacyFile; }
    /** 实际载入的文件（不存在时为主文件候选路径）。 */
    [[nodiscard]] const juce::File& getLoadedFile() const noexcept { return loadedFile; }

    /** 根据分数给等级（供 UI 与结果共用）。 */
    static juce::String gradeForScore (int score);

private:
    std::vector<AssessmentResult> results;   // 最新的在前

    LoadState loadState = LoadState::missing;
    bool usedLegacyFile = false;
    // 本次载入来自 .bak 或彻底损坏：下一次 save 必须跳过「旧主文件 → .bak」备份步骤，
    // 否则会用损坏字节覆盖唯一一份好备份（D-P0-1）。保存成功后自动复位。
    bool skipBackupOnNextSave = false;
    juce::File loadedFile;
};

} // namespace earlab
