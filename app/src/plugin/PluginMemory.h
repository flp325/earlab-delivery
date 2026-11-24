#pragma once

#include "PluginTask.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace earlab::plugin
{

/** 单个插件的训练记忆：上次有效的参数、各题型做过的数量。 */
struct PluginMemoryEntry
{
    juce::String identifier;
    juce::String displayName;
    int preferredParameter = -1;
    int tasks = 0;
    int directionTasks = 0;
    int amountTasks = 0;
    int levelTasks = 0;
    juce::int64 lastUsed = 0;

    [[nodiscard]] int getKindCount (PluginQuestionKind kind) const noexcept;
    void addKind (PluginQuestionKind kind) noexcept;
};

/** 插件记忆存储（JSON，与档案同目录）。 */
class PluginMemoryStore
{
public:
    static juce::File defaultPathFor (const juce::File& profileFile);

    bool load (const juce::File& file);
    bool save (const juce::File& file) const;
    void clear();
    [[nodiscard]] bool hadLoadProblem() const noexcept { return loadProblem; }

    [[nodiscard]] const PluginMemoryEntry* find (const juce::String& identifier) const;

    /** 记录一道题：更新首选参数与题型计数。 */
    void record (const juce::String& identifier, const juce::String& displayName, const PluginTask& task);

    /** 按做过的数量挑最少的题型（只有方向/幅度两种，level 视插件能力而定）。 */
    [[nodiscard]] PluginQuestionKind suggestKind (const juce::String& identifier) const;

private:
    std::vector<PluginMemoryEntry> entries;
    mutable bool skipBackupOnNextSave = false;
    bool loadProblem = false;
};

} // namespace earlab::plugin
