#include "Assessment.h"

#include "../util/AtomicFile.h"

namespace earlab
{

namespace
{
/** 解析测评 JSON；返回 false 表示文件缺失或内容不可解析。 */
bool parseResults (const juce::File& file, std::vector<AssessmentResult>& loaded)
{
    if (! file.existsAsFile())
        return false;

    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    auto* root = parsed.getDynamicObject();

    if (root == nullptr || ! root->getProperty ("results").isArray())
        return false;

    std::vector<AssessmentResult> items;

    if (auto* array = root->getProperty ("results").getArray())
    {
        for (const auto& item : *array)
        {
            auto* object = item.getDynamicObject();

            if (object == nullptr || ! object->getProperty ("score").isInt())
                return false;

            AssessmentResult result;
            result.timestamp   = static_cast<juce::int64> (object->getProperty ("time"));
            result.score       = static_cast<int> (object->getProperty ("score"));
            result.grade       = object->getProperty ("grade").toString();
            result.eqCorrect   = static_cast<int> (object->getProperty ("eqCorrect"));
            result.eqTotal     = static_cast<int> (object->getProperty ("eqTotal"));
            result.compCorrect = static_cast<int> (object->getProperty ("compCorrect"));
            result.compTotal   = static_cast<int> (object->getProperty ("compTotal"));
            result.eqJndDb     = static_cast<float> (static_cast<double> (object->getProperty ("eqJnd")));
            result.compJndDb   = static_cast<float> (static_cast<double> (object->getProperty ("compJnd")));

            if (result.score < 0 || result.score > 100
                || result.eqCorrect < 0 || result.eqCorrect > result.eqTotal
                || result.compCorrect < 0 || result.compCorrect > result.compTotal)
                return false;

            items.push_back (std::move (result));
        }
    }

    loaded = std::move (items);
    return true;
}
} // namespace

juce::File AssessmentStore::defaultPathFor (const juce::File& profileFile)
{
    const auto stem = profileFile.getFileNameWithoutExtension();

    // 按 profile 文件名派生，避免同目录多档案共用一份测评历史
    if (stem.isEmpty())
        return legacyPathFor (profileFile);

    return profileFile.getSiblingFile ("assessments-" + stem + ".json");
}

juce::File AssessmentStore::legacyPathFor (const juce::File& profileFile)
{
    return profileFile.getSiblingFile ("assessments.json");
}

juce::String AssessmentStore::gradeForScore (int score)
{
    if (score >= 85) return juce::String::fromUTF8 ("专业级");
    if (score >= 70) return juce::String::fromUTF8 ("熟练级");
    if (score >= 50) return juce::String::fromUTF8 ("进阶级");

    return juce::String::fromUTF8 ("入门级");
}

bool AssessmentStore::load (const juce::File& file)
{
    usedLegacyFile = false;
    skipBackupOnNextSave = false;
    loadedFile = file;

    if (! file.existsAsFile()
        && ! file.getSiblingFile (file.getFileName() + ".bak").existsAsFile())
    {
        clear();
        loadState = LoadState::missing;
        return false;
    }

    std::vector<AssessmentResult> parsed;

    if (parseResults (file, parsed))
    {
        results = std::move (parsed);
        loadState = LoadState::ok;
        return true;
    }

    // 主文件损坏：尝试唯一备份，成功则本次保存必须跳过备份步骤（D-P0-1）
    const auto backup = file.getSiblingFile (file.getFileName() + ".bak");
    std::vector<AssessmentResult> parsedBackup;

    if (parseResults (backup, parsedBackup))
    {
        results = std::move (parsedBackup);
        loadState = LoadState::recoveredFromBackup;
        skipBackupOnNextSave = true;
        loadedFile = backup;
        return true;
    }

    // 主文件与备份都不可用：保留内存里的历史，提示 UI；下一次保存也不拿坏档覆盖 .bak
    loadState = LoadState::corrupted;
    skipBackupOnNextSave = true;
    return false;
}

bool AssessmentStore::loadForProfile (const juce::File& profileFile)
{
    const auto primary = defaultPathFor (profileFile);

    if (primary.existsAsFile() || primary.getSiblingFile (primary.getFileName() + ".bak").existsAsFile())
        return load (primary);

    const auto legacy = legacyPathFor (profileFile);

    if (legacy != primary && (legacy.existsAsFile() || legacy.getSiblingFile (legacy.getFileName() + ".bak").existsAsFile()))
    {
        const bool ok = load (legacy);
        usedLegacyFile = true;
        return ok;
    }

    usedLegacyFile = false;
    return load (primary);
}

bool AssessmentStore::save (const juce::File& file)
{
    juce::Array<juce::var> items;

    for (const auto& result : results)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("time", result.timestamp);
        object->setProperty ("score", result.score);
        object->setProperty ("grade", result.grade);
        object->setProperty ("eqCorrect", result.eqCorrect);
        object->setProperty ("eqTotal", result.eqTotal);
        object->setProperty ("compCorrect", result.compCorrect);
        object->setProperty ("compTotal", result.compTotal);
        object->setProperty ("eqJnd", result.eqJndDb);
        object->setProperty ("compJnd", result.compJndDb);
        items.add (juce::var (object));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("results", items);

    const bool ok = writeTextAtomically (file, juce::JSON::toString (juce::var (root), false),
                                         ! skipBackupOnNextSave);

    if (ok)
    {
        // 写成功 = 主文件重新可信；下一次保存恢复正常备份语义
        skipBackupOnNextSave = false;
        loadState = LoadState::ok;
        loadedFile = file;
        usedLegacyFile = false;
    }

    return ok;
}

void AssessmentStore::add (const AssessmentResult& result)
{
    results.insert (results.begin(), result);

    // 只留最近 200 次，避免文件无限增长
    if (results.size() > 200)
        results.resize (200);
}

void AssessmentStore::clear()
{
    results.clear();
}

} // namespace earlab
