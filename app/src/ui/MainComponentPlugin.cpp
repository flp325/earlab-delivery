#include "MainComponent.h"
#include "MainComponentSupport.h"
#include "PluginBrowser.h"
#include "SessionRouting.h"
#include "TimeFormat.h"

namespace earlab
{
using namespace ui::detail;

void MainComponent::choosePlugin()
{
    const auto locations = plugin::PluginCatalog::defaultVST3Directories();
    const auto startDir = locations.isEmpty() ? juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                              : juce::File (locations[0]);

    chooser = std::make_unique<juce::FileChooser> (zh ("选择 .vst3 插件"),
                                                   startDir, "");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories
                              | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (safe && file != juce::File())
            safe->loadPluginFile (file);
    });
}

void MainComponent::loadPluginFile (const juce::File& file)
{
    if (! file.exists())
    {
        reportVisibleError (zh ("无法加载插件"), zh ("插件路径不存在：") + file.getFullPathName());
        return;
    }
    beginPluginLoad (file.getFullPathName());
}

void MainComponent::loadPluginInfo (const plugin::PluginInfo& info)
{
    beginPluginLoad (info.fileOrIdentifier, info);
}

void MainComponent::beginPluginLoad (const juce::String& path, std::optional<plugin::PluginInfo> info)
{
    if (trainingActive || assessmentActive)
    {
        reportVisibleError (zh ("无法切换插件"), zh ("请先结束本轮训练或测评。"));
        return;
    }
    cancelPluginLoad();
    engine.setPlaying (false);
    pluginLoadJob = std::make_unique<plugin::PluginLoadJob> (
        juce::File::getSpecialLocation (juce::File::currentExecutableFile), path, info);
    pluginLoadButton.setButtonText (zh ("取消加载"));
    pluginBrowserButton.setEnabled (false);
    trainStartButton.setEnabled (false);
    assessStartButton.setEnabled (false);
    pollPluginLoad();
}

void MainComponent::cancelPluginLoad()
{
    pluginLoadJob.reset();
    pendingQaSkill = 0;
    pluginLoadButton.setButtonText (zh ("加载 VST3"));
    pluginBrowserButton.setEnabled (! trainingActive);
    trainStartButton.setEnabled (! trainingActive);
    assessStartButton.setEnabled (! trainingActive);
    pluginLabel.setText (pluginGenerator ? zh ("插件：") + pluginInfo.getDisplayName()
                                         : zh ("未加载插件"), juce::dontSendNotification);
    updatePageStatus();
}

void MainComponent::pollPluginLoad()
{
    if (! pluginLoadJob)
        return;
    if (! pluginLoadJob->isFinished())
    {
        const auto text = pluginLoadJob->getPhase() == plugin::PluginLoadJob::Phase::scanning
            ? zh ("正在读取插件信息… 可取消") : zh ("正在隔离环境预检… 可取消");
        pluginLabel.setText (text, juce::dontSendNotification);
        if (currentPage == Page::plugin)
        {
            pageStatus.setText (text);
            pageStatus.setTone (StatusChip::Tone::accent);
        }
        return;
    }
    auto result = pluginLoadJob->takeResult();
    const int queuedSkill = pendingQaSkill;
    cancelPluginLoad();
    if (! result.ok)
    {
        reportVisibleError (zh ("无法加载插件"),
                            zh ("隔离检查未通过：") + result.error + zh ("。已拒绝加载。"));
        return;
    }
    const bool loaded = finishPluginLoad (result.info);
    updatePageStatus();
    if (queuedSkill < 0 && loaded)
        beginAssessment();
    else if (queuedSkill > 0 && loaded)
        beginTrainingForQa (queuedSkill);
}

bool MainComponent::finishPluginLoad (const plugin::PluginInfo& info)
{
    juce::String error;
    auto instance = plugin::PluginHost::create (info,
                                                plugin::PluginHost::probeSampleRate,
                                                plugin::PluginHost::probeBlockSize,
                                                error);

    if (instance == nullptr)
    {
        reportVisibleError (zh ("无法加载插件"), zh ("插件加载失败：") + error);
        return false;
    }

    pluginInfo = info;
    pluginGenerator = std::move (instance);
    pluginParams = plugin::PluginHost::listParameters (*pluginGenerator);
    preferredPluginParameter = -1;

    juce::String memoryNote;

    if (const auto* memory = pluginMemory.find (info.identifier))
    {
        preferredPluginParameter = memory->preferredParameter;

        if (memory->tasks > 0)
            memoryNote = zh (" · 已练 ") + juce::String (memory->tasks) + zh (" 题");
    }

    engine.unloadPlugin();

    pluginLabel.setText (zh ("插件：") + pluginInfo.getDisplayName()
                             + zh (" · ") + juce::String (static_cast<int> (pluginParams.size()))
                             + zh (" 个参数") + memoryNote,
                         juce::dontSendNotification);

    feedbackLabel.setText (zh ("插件已就绪，选「VST3 插件辨别」开始训练。"),
                           juce::dontSendNotification);
    skillBox.setSelectedId (3, juce::dontSendNotification);
    return true;
}

void MainComponent::showPluginBrowser()
{
    if (pluginBrowserWindow != nullptr) { pluginBrowserWindow->toFront (true); return; }
    auto content = std::make_unique<plugin::PluginBrowser> (
        pluginCatalog,
        profileFile.getSiblingFile ("plugins.json"),
        [safe = juce::Component::SafePointer<MainComponent> (this)] (const plugin::PluginInfo& info)
        { if (safe != nullptr) safe->loadPluginInfo (info); });

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (content.release());
    options.dialogTitle = zh ("插件库");
    options.dialogBackgroundColour = kPanel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = true;
    pluginBrowserWindow = options.launchAsync();
}

void MainComponent::setOptionsEnabled (bool shouldBeEnabled)
{
    for (auto& b : optionButtons)
        b.setEnabled (shouldBeEnabled);
}

void MainComponent::startPluginTrial()
{
    if (pluginGenerator == nullptr)
        return;

    engine.setPlaying (false);

    const float target = pluginStaircase.getCurrentDb();
    const float difficulty = juce::jlimit (0.0f, 1.0f, (12.0f - target) / 10.5f);
    const std::uint32_t seed = ++pluginSeed;
    const int preferred = preferredPluginParameter;

    auto preferredKind = qaKind;

    if (preferredKind == plugin::PluginQuestionKind::automatic)
        preferredKind = pluginMemory.suggestKind (pluginInfo.identifier);

    struct GenerationThread final : juce::ThreadWithProgressWindow
    {
        GenerationThread (juce::AudioPluginInstance& instanceToUse,
                          const plugin::PluginInfo& infoToUse,
                          const std::vector<plugin::HostParameter>& parametersToUse,
                          float targetDbToUse, float difficultyToUse,
                          std::uint32_t seedToUse, int preferredIndex,
                          plugin::PluginQuestionKind kindToUse)
            : juce::ThreadWithProgressWindow (zh ("正在分析插件并出题…"), false, true, 3000),
              instance (instanceToUse), info (infoToUse), parameters (parametersToUse),
              targetDb (targetDbToUse), difficulty (difficultyToUse),
              seed (seedToUse), preferred (preferredIndex), kind (kindToUse)
        {
        }

        void run() override
        {
            plugin::PluginTaskGenerator taskGenerator (seed);
            task = taskGenerator.generate (instance, info, parameters, targetDb, difficulty,
                                           48000.0, preferred, kind,
                                           [this] { return threadShouldExit(); });
            valid = task.parameterIndex >= 0;
        }

        juce::AudioPluginInstance& instance;
        const plugin::PluginInfo& info;
        const std::vector<plugin::HostParameter>& parameters;
        float targetDb, difficulty;
        std::uint32_t seed;
        int preferred;
        plugin::PluginQuestionKind kind;

        plugin::PluginTask task;
        bool valid = false;
    };

    setOptionsEnabled (false);
    nextButton.setEnabled (false);
    feedbackLabel.setText (zh ("正在分析插件并生成题目，第一次会慢几秒…"),
                           juce::dontSendNotification);
    repaint();

    GenerationThread thread (*pluginGenerator, pluginInfo, pluginParams,
                             target, difficulty, seed, preferred, preferredKind);

    if (! thread.runThread())
    {
        abortTraining (zh ("已取消出题，本轮训练中止。"));
        return;
    }

    if (! thread.valid)
    {
        abortTraining (zh ("没能出题：这个插件可能没有能测出频谱变化的参数。请换一个效果器。"));
        return;
    }

    applyPluginTask (thread.task);
}


void MainComponent::applyPluginTask (const plugin::PluginTask& task)
{
    pluginTask = task;
    preferredPluginParameter = task.parameterIndex;

    juce::String error;

    if (! engine.hasPlugin())
    {
        if (! engine.loadPluginTask (pluginInfo, task, error))
        {
            abortTraining (zh ("插件加载失败：") + error);
            return;
        }
    }
    else if (! engine.updatePluginTask (task))
    {
        abortTraining (zh ("插件参数更新失败（插件未就绪或仍在播放），本轮已中止。"));
        return;
    }

    pluginMemory.record (pluginInfo.identifier, pluginInfo.name, task);
    if (! pluginMemory.save (plugin::PluginMemoryStore::defaultPathFor (profileFile)))
        reportVisibleError (zh ("插件记忆保存失败"), zh ("本题仍可继续，但退出后可能丢失题型统计。"));

    engine.setSource (DualChain::Source::dry);
    engine.setPlaying (true);
    updateTransportText();

    answered = false;

    for (int i = 0; i < 4; ++i)
    {
        auto& b = optionButtons[static_cast<size_t> (i)];
        b.setButtonText (task.optionLabels[static_cast<size_t> (i)]);
        b.setResult (OptionButton::Result::none);
        b.setEnabled (true);
    }

    displayedTrialIndex = trialIndex;
    progressTotal = kTrialsPerSession;

    nextButton.setEnabled (false);

    // 幅度题不能把目标值显示出来：那个值就是正确选项
    juce::String infoText = zh ("第 ") + juce::String (trialIndex + 1) + " / "
                                + juce::String (kTrialsPerSession) + zh (" 题");

    if (task.kind == plugin::PluginQuestionKind::bandDirection)
        infoText << zh (" · 目标 ") << juce::String (task.targetDeltaDb, 1) << " dB";

    infoText << zh (" · 正确 ") << juce::String (correctCount);

    trainInfoLabel.setText (infoText, juce::dontSendNotification);

    feedbackLabel.setText (zh ("A 是干声，B 是 ") + pluginInfo.name
                               + zh (" 处理后的声音。判断 B 改变了什么。"),
                           juce::dontSendNotification);

    if (task.kind == plugin::PluginQuestionKind::bandAmount)
    {
        const auto bandName = plugin::PluginProbe::formatBandLabel (
            plugin::PluginProbe::bandCentres()[static_cast<size_t> (task.bandIndex)]);

        feedbackLabel.setText (zh ("B 在 ") + bandName + zh (" 附近的")
                                   + (task.direction > 0 ? zh ("提升") : zh ("衰减"))
                                   + zh ("量约为多少 dB？"),
                               juce::dontSendNotification);
    }
    else if (task.kind == plugin::PluginQuestionKind::levelAmount)
    {
        feedbackLabel.setText (zh ("B 的整体电平变化量约为多少 dB？"),
                               juce::dontSendNotification);
    }

    feedbackLabel.setColour (juce::Label::textColourId, kTextBright);

    if (qaAutoPlay)
        juce::Timer::callAfterDelay (400, [safe = juce::Component::SafePointer<MainComponent> (this), epoch = sessionEpoch]
        {
            if (safe && safe->sessionEpoch == epoch && safe->trainingActive && safe->activeSkill == Skill::pluginBand)
                safe->submitTrainingAnswer (safe->pluginTask.correctIndex);
        });
}

} // namespace earlab
