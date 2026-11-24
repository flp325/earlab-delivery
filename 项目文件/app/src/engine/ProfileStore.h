#pragma once

#include "TrainingTask.h"

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace earlab
{

struct SessionRecord
{
    Skill skill = Skill::eqBandCenter;
    juce::int64 timestamp = 0;
    int trials = 0;
    int correct = 0;
    float thresholdDb = 0.0f;
    double elo = 1000.0;
};

struct SkillStats
{
    double elo = 1000.0;
    float thresholdDb = 0.0f;
    int sessions = 0;
    int trials = 0;
    int correct = 0;

    [[nodiscard]] double getAccuracy() const noexcept
    {
        return trials > 0 ? static_cast<double> (correct) / static_cast<double> (trials) : 0.0;
    }
};

/**
    本地训练档案：会话历史 + 每技能汇总。

    用 JSON 落盘（AppData），零外部依赖；记录量大到需要查询时再迁 SQLite。
*/
class ProfileStore
{
public:
    enum class LoadState
    {
        ok,
        missing,               // 文件不存在（首次启动）
        recoveredFromBackup,   // 主文件损坏，已从 .bak 恢复（D-P0-1）
        corrupted              // 主文件损坏且 .bak 也不可用
    };

    static juce::File defaultPath();

    bool load (const juce::File& file);
    bool save (const juce::File& file);

    bool exportReport (const juce::File& destination, const juce::File& profileFile) const;

    /** 导出 CSV（带 UTF-8 BOM，Excel 直接打开不乱码）。 */
    bool saveCsv (const juce::File& file) const;

    /** 导出 HTML 报告：汇总卡片 + JND 趋势图 + 明细表，可打印成 PDF。 */
    bool writeHtmlReport (const juce::File& file) const;

    void addSession (const SessionRecord& record);
    void clear();

    [[nodiscard]] const SkillStats& getStats (Skill skill) const;
    /** 该技能第一次训练的 Elo；没有记录返回 0。 */
    [[nodiscard]] double getFirstElo (Skill skill) const;
    [[nodiscard]] const std::vector<SessionRecord>& getHistory() const noexcept { return history; }
    [[nodiscard]] int getSessionCount() const noexcept { return static_cast<int> (history.size()); }

    [[nodiscard]] LoadState getLoadState() const noexcept { return loadState; }
    [[nodiscard]] bool didRecoverFromBackup() const noexcept
    {
        return loadState == LoadState::recoveredFromBackup;
    }

private:
    void rebuildStats();

    std::array<SkillStats, 3> stats;
    std::vector<SessionRecord> history;

    LoadState loadState = LoadState::missing;
    // 载入来自 .bak 或彻底损坏：下一次 save 跳过「旧主文件 → .bak」备份，
    // 避免用损坏字节覆盖唯一一份好备份（D-P0-1）。保存成功后自动复位。
    bool skipBackupOnNextSave = false;
};

} // namespace earlab
