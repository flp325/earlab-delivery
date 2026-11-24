#include "PluginMemory.h"

#include "../util/AtomicFile.h"

#include <algorithm>

namespace earlab::plugin
{

int PluginMemoryEntry::getKindCount (PluginQuestionKind kind) const noexcept
{
    switch (kind)
    {
        case PluginQuestionKind::bandAmount:  return amountTasks;
        case PluginQuestionKind::levelAmount: return levelTasks;
        case PluginQuestionKind::bandDirection:
        default:                              return directionTasks;
    }
}

void PluginMemoryEntry::addKind (PluginQuestionKind kind) noexcept
{
    switch (kind)
    {
        case PluginQuestionKind::bandAmount:  ++amountTasks; break;
        case PluginQuestionKind::levelAmount: ++levelTasks; break;
        case PluginQuestionKind::bandDirection:
        default:                              ++directionTasks; break;
    }
}

juce::File PluginMemoryStore::defaultPathFor (const juce::File& profileFile)
{
    return profileFile.getSiblingFile ("plugin-memory.json");
}

namespace
{
bool parseMemory (const juce::File& file, std::vector<PluginMemoryEntry>& loaded)
{
    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    auto* root = parsed.getDynamicObject();

    // 坏文件不清空：保留内存里的记忆，避免下一次保存把「已练 N 题」抹掉
    if (root == nullptr || ! root->getProperty ("plugins").isArray())
        return false;


    if (auto* items = root->getProperty ("plugins").getArray())
    {
        for (const auto& item : *items)
        {
            auto* object = item.getDynamicObject();

            if (object == nullptr)
                return false;

            PluginMemoryEntry entry;
            entry.identifier         = object->getProperty ("identifier").toString();
            entry.displayName        = object->getProperty ("name").toString();
            entry.preferredParameter = static_cast<int> (object->getProperty ("preferredParameter"));
            entry.tasks              = static_cast<int> (object->getProperty ("tasks"));
            entry.directionTasks     = static_cast<int> (object->getProperty ("directionTasks"));
            entry.amountTasks        = static_cast<int> (object->getProperty ("amountTasks"));
            entry.levelTasks         = static_cast<int> (object->getProperty ("levelTasks"));
            entry.lastUsed           = static_cast<juce::int64> (object->getProperty ("lastUsed"));

            loaded.push_back (std::move (entry));
        }
    }

    return true;
}
} // namespace

bool PluginMemoryStore::load (const juce::File& file)
{
    const auto backup = file.getSiblingFile (file.getFileName() + ".bak");
    skipBackupOnNextSave = false;
    loadProblem = false;
    if (! file.existsAsFile() && ! backup.existsAsFile())
    {
        clear();
        return false;
    }
    std::vector<PluginMemoryEntry> loaded;
    if (parseMemory (file, loaded))
    {
        entries = std::move (loaded);
        return true;
    }
    loadProblem = true;
    skipBackupOnNextSave = true;
    loaded.clear();
    if (! parseMemory (backup, loaded))
        return false;
    entries = std::move (loaded);
    return true;
}

bool PluginMemoryStore::save (const juce::File& file) const
{
    juce::Array<juce::var> items;

    for (const auto& entry : entries)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("identifier", entry.identifier);
        object->setProperty ("name", entry.displayName);
        object->setProperty ("preferredParameter", entry.preferredParameter);
        object->setProperty ("tasks", entry.tasks);
        object->setProperty ("directionTasks", entry.directionTasks);
        object->setProperty ("amountTasks", entry.amountTasks);
        object->setProperty ("levelTasks", entry.levelTasks);
        object->setProperty ("lastUsed", entry.lastUsed);
        items.add (juce::var (object));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("plugins", items);

    file.getParentDirectory().createDirectory();
    const bool saved = writeTextAtomically (file, juce::JSON::toString (juce::var (root), false),
                                            ! skipBackupOnNextSave);
    if (saved)
        skipBackupOnNextSave = false;
    return saved;
}

void PluginMemoryStore::clear()
{
    entries.clear();
    skipBackupOnNextSave = false;
    loadProblem = false;
}

const PluginMemoryEntry* PluginMemoryStore::find (const juce::String& identifier) const
{
    for (const auto& entry : entries)
        if (entry.identifier == identifier)
            return &entry;

    return nullptr;
}

void PluginMemoryStore::record (const juce::String& identifier,
                                const juce::String& displayName,
                                const PluginTask& task)
{
    PluginMemoryEntry* entry = nullptr;

    for (auto& candidate : entries)
        if (candidate.identifier == identifier)
            entry = &candidate;

    if (entry == nullptr)
    {
        entries.push_back ({});
        entry = &entries.back();
    }

    entry->identifier = identifier;
    entry->displayName = displayName;
    entry->preferredParameter = task.parameterIndex;
    entry->lastUsed = juce::Time::currentTimeMillis();
    ++entry->tasks;
    entry->addKind (task.kind);
}

PluginQuestionKind PluginMemoryStore::suggestKind (const juce::String& identifier) const
{
    const auto* entry = find (identifier);

    if (entry == nullptr || entry->tasks < 3)
        return PluginQuestionKind::automatic;

    // 优先练得少的那一类；level 只在插件出现过宽带效果时才有意义
    const int direction = entry->directionTasks;
    const int amount = entry->amountTasks;

    return direction <= amount ? PluginQuestionKind::bandDirection
                               : PluginQuestionKind::bandAmount;
}

} // namespace earlab::plugin
