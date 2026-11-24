#include "ProfileStore.h"

#include "../util/AtomicFile.h"
#include <filesystem>

namespace earlab
{

namespace
{
size_t skillIndex (Skill skill)
{
    switch (skill)
    {
        case Skill::compAmount: return 1u;
        case Skill::pluginBand: return 2u;
        case Skill::eqBandCenter:
        default:                return 0u;
    }
}

/** 解析训练档案 JSON；返回 false 表示文件缺失或内容不可解析。 */
bool parseSessions (const juce::File& file, std::vector<SessionRecord>& loaded)
{
    if (! file.existsAsFile())
        return false;

    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    auto* root = parsed.getDynamicObject();

    if (root == nullptr || ! root->getProperty ("sessions").isArray())
        return false;

    std::vector<SessionRecord> records;

    if (auto* sessions = root->getProperty ("sessions").getArray())
    {
        for (const auto& entry : *sessions)
        {
            auto* object = entry.getDynamicObject();

            if (object == nullptr || ! object->getProperty ("skill").isInt()
                || ! object->getProperty ("trials").isInt() || ! object->getProperty ("correct").isInt())
                return false;

            SessionRecord record;
            record.skill       = static_cast<Skill> (static_cast<int> (object->getProperty ("skill")));
            record.timestamp   = static_cast<juce::int64> (object->getProperty ("time"));
            record.trials      = static_cast<int> (object->getProperty ("trials"));
            record.correct     = static_cast<int> (object->getProperty ("correct"));
            record.thresholdDb = static_cast<float> (static_cast<double> (object->getProperty ("threshold")));
            record.elo         = static_cast<double> (object->getProperty ("elo"));

            if ((record.skill != Skill::eqBandCenter && record.skill != Skill::compAmount && record.skill != Skill::pluginBand)
                || record.trials < 0 || record.correct < 0 || record.correct > record.trials)
                return false;

            records.push_back (record);
        }
    }

    loaded = std::move (records);
    return true;
}
} // namespace

juce::File ProfileStore::defaultPath()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("EarLab")
        .getChildFile ("profile.json");
}

bool ProfileStore::load (const juce::File& file)
{
    skipBackupOnNextSave = false;

    if (! file.existsAsFile()
        && ! file.getSiblingFile (file.getFileName() + ".bak").existsAsFile())
    {
        history.clear();
        rebuildStats();
        loadState = LoadState::missing;
        return false;
    }

    std::vector<SessionRecord> parsed;

    if (parseSessions (file, parsed))
    {
        history = std::move (parsed);
        rebuildStats();
        loadState = LoadState::ok;
        return true;
    }

    // 主文件损坏：尝试 .bak。恢复成功时下一次保存跳过备份步骤，保留好备份（D-P0-1）
    const auto backup = file.getSiblingFile (file.getFileName() + ".bak");
    std::vector<SessionRecord> parsedBackup;

    if (parseSessions (backup, parsedBackup))
    {
        history = std::move (parsedBackup);
        rebuildStats();
        loadState = LoadState::recoveredFromBackup;
        skipBackupOnNextSave = true;
        return true;
    }

    // 主文件与备份都不可用：保留内存里的历史，绝不清空；下一次保存也不覆盖 .bak
    loadState = LoadState::corrupted;
    skipBackupOnNextSave = true;
    return false;
}

bool ProfileStore::save (const juce::File& file)
{
    juce::Array<juce::var> sessions;

    for (const auto& record : history)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("skill", static_cast<int> (record.skill));
        object->setProperty ("time", record.timestamp);
        object->setProperty ("trials", record.trials);
        object->setProperty ("correct", record.correct);
        object->setProperty ("threshold", record.thresholdDb);
        object->setProperty ("elo", record.elo);
        sessions.add (juce::var (object));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("sessions", sessions);

    const bool ok = writeTextAtomically (file, juce::JSON::toString (juce::var (root), false),
                                         ! skipBackupOnNextSave);

    if (ok)
    {
        // 主文件重新可信，下一次保存恢复正常备份语义
        skipBackupOnNextSave = false;
        loadState = LoadState::ok;
    }

    return ok;
}

bool ProfileStore::exportReport (const juce::File& destination, const juce::File& profileFile) const
{
    if (loadState == LoadState::corrupted || destination == profileFile || ! destination.hasFileExtension ("csv;html"))
        return false;

    // Two spellings can name the same document (a link, or a case-insensitive volume).
    if (destination.existsAsFile() && profileFile.existsAsFile())
    {
        std::error_code error;
        const auto path = [] (const juce::File& f) { return std::filesystem::u8path (f.getFullPathName().toRawUTF8()); };
        if (std::filesystem::equivalent (path (profileFile), path (destination), error) || error)
            return false;
    }

    return destination.hasFileExtension ("csv") ? saveCsv (destination) : writeHtmlReport (destination);
}

bool ProfileStore::saveCsv (const juce::File& file) const
{
    juce::String text;

    // Excel 需要 BOM 才按 UTF-8 打开中文表头
    text << "\xEF\xBB\xBF";
    text << juce::String::fromUTF8 ("时间,技能,题数,正确,准确率%,阈值dB,Elo\n");

    for (const auto& record : history)
    {
        const auto* skillName = record.skill == Skill::compAmount ? "压缩量判断"
                              : record.skill == Skill::pluginBand ? "插件辨别"
                                                                  : "EQ 频段定位";

        const double accuracy = record.trials > 0
                                    ? static_cast<double> (record.correct) / record.trials * 100.0
                                    : 0.0;

        text << juce::Time (record.timestamp).toString (true, true) << ","
             << juce::String::fromUTF8 (skillName) << ","
             << record.trials << ","
             << record.correct << ","
             << juce::String (accuracy, 1) << ","
             << juce::String (record.thresholdDb, 2) << ","
             << juce::String (record.elo, 1) << "\n";
    }

    file.getParentDirectory().createDirectory();
    return file.replaceWithText (text);
}

bool ProfileStore::writeHtmlReport (const juce::File& file) const
{
    const auto zh = [] (const char* text) { return juce::String::fromUTF8 (text); };

    const auto skillName = [&zh] (Skill skill)
    {
        switch (skill)
        {
            case Skill::compAmount: return zh ("压缩量判断");
            case Skill::pluginBand: return zh ("插件辨别");
            case Skill::eqBandCenter:
            default:                return zh ("EQ 频段定位");
        }
    };

    const auto skillColour = [] (Skill skill)
    {
        switch (skill)
        {
            case Skill::compAmount: return "#e0a050";
            case Skill::pluginBand: return "#5fd0a8";
            case Skill::eqBandCenter:
            default:                return "#4f8cff";
        }
    };

    juce::String html;
    html << "<!doctype html><html lang=\"zh\"><head><meta charset=\"utf-8\">";
    html << "<title>" << zh ("EarLab 训练报告") << "</title>";
    html << "<style>"
            "body{background:#0e1116;color:#e6ebf2;font-family:'Microsoft YaHei UI',sans-serif;"
            "margin:32px auto;max-width:900px;padding:0 20px}"
            "h1{font-size:24px;margin:0 0 6px}.meta{color:#8b93a1;font-size:12px;margin:0 0 24px}"
            ".cards{display:flex;gap:14px;flex-wrap:wrap;margin-bottom:26px}"
            ".card{flex:1 1 220px;background:#191f27;border-radius:10px;padding:14px 16px}"
            ".card h2{font-size:14px;margin:0 0 8px}.card .big{font-size:26px;margin:2px 0}"
            ".card .sub{color:#8b93a1;font-size:12px;line-height:1.7}"
            "table{width:100%;border-collapse:collapse;font-size:13px;margin-top:10px}"
            "th,td{padding:7px 8px;text-align:left;border-bottom:1px solid #2a313c}"
            "th{color:#8b93a1;font-weight:normal}"
            "h3{margin:28px 0 6px;font-size:15px}"
            ".note{color:#8b93a1;font-size:12px;margin-top:18px}"
            "</style></head><body>";

    html << "<h1>" << zh ("EarLab 训练报告") << "</h1>";
    html << "<p class=\"meta\">" << zh ("生成时间：")
         << juce::Time::getCurrentTime().toString (true, true)
         << zh (" · 共 ") << juce::String (getSessionCount()) << zh (" 轮训练") << "</p>";

    // ---- 汇总卡片
    html << "<div class=\"cards\">";

    for (const auto skill : { Skill::eqBandCenter, Skill::compAmount, Skill::pluginBand })
    {
        const auto& stat = getStats (skill);

        html << "<div class=\"card\"><h2 style=\"color:" << skillColour (skill) << "\">"
             << skillName (skill) << "</h2>";

        if (stat.sessions == 0)
        {
            html << "<div class=\"sub\">" << zh ("还没有记录") << "</div></div>";
            continue;
        }

        html << "<div class=\"big\">" << juce::String (stat.elo, 0) << "</div>";
        html << "<div class=\"sub\">" << zh ("JND ") << juce::String (stat.thresholdDb, 1)
             << " dB · " << juce::String (stat.sessions) << zh (" 轮 / ")
             << juce::String (stat.trials) << zh (" 题 · ")
             << zh ("准确率 ") << juce::String (juce::roundToInt (stat.getAccuracy() * 100.0))
             << "%</div></div>";
    }

    html << "</div>";

    // ---- JND 趋势（每条技能一条折线）
    const double width = 820.0, height = 180.0, pad = 34.0;

    html << "<h3>" << zh ("JND 趋势（越低越好，0–12 dB）") << "</h3>";
    html << "<svg viewBox=\"0 0 " << width << " " << height
         << "\" width=\"100%\" style=\"background:#191f27;border-radius:10px\">";

    for (int db = 0; db <= 12; db += 6)
    {
        const double y = pad + (1.0 - static_cast<double> (db) / 12.0) * (height - 2.0 * pad);
        html << "<line x1=\"" << pad << "\" y1=\"" << y << "\" x2=\"" << width - pad
             << "\" y2=\"" << y << "\" stroke=\"#2a313c\" stroke-width=\"1\"/>";
        html << "<text x=\"6\" y=\"" << y + 4 << "\" fill=\"#8b93a1\" font-size=\"11\">"
             << db << " dB</text>";
    }

    for (const auto skill : { Skill::eqBandCenter, Skill::compAmount, Skill::pluginBand })
    {
        std::vector<double> values;

        for (const auto& record : history)
            if (record.skill == skill)
                values.push_back (juce::jlimit (0.0, 12.0, static_cast<double> (record.thresholdDb)));

        if (values.empty())
            continue;

        html << "<polyline fill=\"none\" stroke=\"" << skillColour (skill)
             << "\" stroke-width=\"2\" points=\"";

        for (size_t i = 0; i < values.size(); ++i)
        {
            const double x = pad + (width - 2.0 * pad)
                                   * (values.size() == 1 ? 0.5 : static_cast<double> (i)
                                                                  / static_cast<double> (values.size() - 1));
            const double y = pad + (1.0 - values[i] / 12.0) * (height - 2.0 * pad);
            html << juce::String (x, 1) << "," << juce::String (y, 1) << " ";
        }

        html << "\"/>";

        // 数据点：只有一轮时折线画不出来，必须画点
        for (size_t i = 0; i < values.size(); ++i)
        {
            const double x = pad + (width - 2.0 * pad)
                                   * (values.size() == 1 ? 0.5 : static_cast<double> (i)
                                                                  / static_cast<double> (values.size() - 1));
            const double y = pad + (1.0 - values[i] / 12.0) * (height - 2.0 * pad);

            html << "<circle cx=\"" << juce::String (x, 1) << "\" cy=\"" << juce::String (y, 1)
                 << "\" r=\"3.5\" fill=\"" << skillColour (skill) << "\"/>";
        }
    }

    html << "</svg>";

    // ---- 明细
    html << "<h3>" << zh ("训练明细") << "</h3>";
    html << "<table><tr><th>" << zh ("时间") << "</th><th>" << zh ("技能") << "</th><th>"
         << zh ("题数") << "</th><th>" << zh ("正确") << "</th><th>" << zh ("准确率")
         << "</th><th>" << zh ("阈值 dB") << "</th><th>Elo</th></tr>";

    for (const auto& record : history)
    {
        const double accuracy = record.trials > 0
                                    ? static_cast<double> (record.correct) / record.trials * 100.0
                                    : 0.0;

        html << "<tr><td>" << juce::Time (record.timestamp).toString (true, true)
             << "</td><td>" << skillName (record.skill)
             << "</td><td>" << record.trials
             << "</td><td>" << record.correct
             << "</td><td>" << juce::String (accuracy, 1) << "%"
             << "</td><td>" << juce::String (record.thresholdDb, 1)
             << "</td><td>" << juce::String (record.elo, 0) << "</td></tr>";
    }

    html << "</table>";
    html << "<p class=\"note\">" << zh ("EarLab · 浏览器里 Ctrl+P 可直接打印成 PDF") << "</p>";
    html << "</body></html>";

    file.getParentDirectory().createDirectory();
    return file.replaceWithText (html);
}

void ProfileStore::addSession (const SessionRecord& record)
{
    history.push_back (record);
    rebuildStats();
}

void ProfileStore::clear()
{
    history.clear();
    rebuildStats();
}

const SkillStats& ProfileStore::getStats (Skill skill) const
{
    return stats[skillIndex (skill)];
}
double ProfileStore::getFirstElo (Skill skill) const
{
    for (const auto& record : history)
        if (record.skill == skill)
            return record.elo;

    return 0.0;
}

void ProfileStore::rebuildStats()
{
    for (auto& stat : stats)
        stat = SkillStats {};

    for (const auto& record : history)
    {
        auto& stat = stats[skillIndex (record.skill)];

        ++stat.sessions;
        stat.trials  += record.trials;
        stat.correct += record.correct;
        stat.elo     = record.elo;
        stat.thresholdDb = record.thresholdDb;
    }
}

} // namespace earlab
