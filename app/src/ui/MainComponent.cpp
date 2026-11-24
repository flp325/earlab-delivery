#include "MainComponent.h"
#include "EarLabArtwork.h"
#include "MainComponentSupport.h"

#include "../plugin/PluginScanRunner.h"
#include "PluginBrowser.h"
#include "SessionRouting.h"
#include "Theme.h"
#include "TimeFormat.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace earlab
{

using namespace ui::detail;

MainComponent::MainComponent()
{
    setSize (1000, 880);
    setWantsKeyboardFocus (true);

    // 进度窗口用的是全局 LookAndFeel，这里统一成主题色
    juce::LookAndFeel::getDefaultLookAndFeel().setColour (juce::ProgressBar::foregroundColourId, kAccent);

    titleLabel.setText (zh ("耳力训练"), juce::dontSendNotification);
    // P1-1：页头 H1 22 → 26（JUCE height；全站唯一一处页头字号）
    titleLabel.setFont (uiFont (theme::typeH1, true));
    titleLabel.setColour (juce::Label::textColourId, kTextBright);
    addAndMakeVisible (titleLabel);

    auto setupLabel = [this] (juce::Label& label, const juce::String& text, float height)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (uiFont (height));
        label.setColour (juce::Label::textColourId, kTextDim);
        addAndMakeVisible (label);
    };

    setupLabel (deviceLabel, zh ("音频设备"), theme::typeMeta);
    setupLabel (materialLabel, zh ("未载入素材"), theme::typeMeta);
    setupLabel (statusLabel, {}, theme::typeMeta);
    setupLabel (trainInfoLabel, zh ("未开始训练"), 13.0f);
    setupLabel (feedbackLabel, zh ("载入素材后，选择技能并开始训练。"), 13.0f);
    setupLabel (pluginLabel, zh ("未加载插件（支持 VST3）"), theme::typeMeta);

    statusLabel.setColour (juce::Label::textColourId, kTextBright);
    feedbackLabel.setColour (juce::Label::textColourId, kTextBright);

    settingsButton.setButtonText (zh ("音频设置"));
    loadButton.setButtonText (zh ("载入素材"));
    whiteNoiseButton.setButtonText (zh ("白噪"));
    pinkNoiseButton.setButtonText (zh ("粉噪"));
    playButton.setButtonText (zh ("播放"));
    dryButton.setButtonText (zh ("A 干声"));
    wetButton.setButtonText (zh ("B 湿声"));
    pluginLoadButton.setButtonText (zh ("加载 VST3"));
    pluginBrowserButton.setButtonText (zh ("插件库"));
    exportButton.setButtonText (zh ("导出报告"));
    trainStartButton.setButtonText (zh ("开始训练"));
    playAButton.setButtonText (zh ("听 A"));
    playBButton.setButtonText (zh ("听 B"));
    nextButton.setButtonText (zh ("下一题"));
    abortSessionButton.setButtonText (zh ("中止本轮"));

    for (auto* b : { &settingsButton, &loadButton, &whiteNoiseButton, &pinkNoiseButton,
                     &playButton, &dryButton, &wetButton,
                     &pluginLoadButton, &pluginBrowserButton, &exportButton,
                     &trainStartButton, &playAButton, &playBButton, &nextButton,
                     &abortSessionButton })
    {
        b->setColour (juce::TextButton::buttonColourId, kPanel);
        b->setColour (juce::TextButton::textColourOffId, kTextBright);
        addAndMakeVisible (*b);
    }

    skillBox.addItem (zh ("EQ 频段定位"), 1);
    skillBox.addItem (zh ("压缩量判断"), 2);
    skillBox.addItem (zh ("VST3 插件辨别"), 3);
    skillBox.setSelectedId (1, juce::dontSendNotification);
    skillBox.setColour (juce::ComboBox::backgroundColourId, kPanel);
    skillBox.setColour (juce::ComboBox::textColourId, kTextBright);
    skillBox.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff2a313c));
    addAndMakeVisible (skillBox);

    for (int i = 0; i < 4; ++i)
    {
        auto& b = optionButtons[static_cast<size_t> (i)];
        // P0-4：徽标与键盘 1–4 作答保持一致（原 A–D 与按键映射矛盾）
        b.setLetter (juce::String (i + 1));
        b.setEnabled (false);
        b.setMouseClickGrabsKeyboardFocus (false);
        b.setWantsKeyboardFocus (true);   // P1-9：Tab 可聚焦并显示焦点环
        b.onClick = [this, i] { submitTrainingAnswer (i); };
        addAndMakeVisible (b);
    }

    for (auto& card : statCards)
        addAndMakeVisible (card);

    addAndMakeVisible (pageStatus);

    nextButton.setEnabled (false);

    settingsButton.onClick = [this] { showAudioSettings(); };
    loadButton.onClick     = [this] { chooseMaterial(); };
    whiteNoiseButton.onClick = [this] { startNoise (1); };
    pinkNoiseButton.onClick  = [this] { startNoise (2); };
    pluginLoadButton.onClick = [this] { if (pluginLoadJob) cancelPluginLoad(); else choosePlugin(); };
    pluginBrowserButton.onClick = [this] { showPluginBrowser(); };
    exportButton.onClick = [this] { exportReport(); };
    playButton.onClick     = [this] { togglePlayback(); };
    dryButton.onClick      = [this] { playSource (DualChain::Source::dry); };
    wetButton.onClick      = [this] { playSource (DualChain::Source::wet); };
    playAButton.onClick    = [this] { playSource (DualChain::Source::dry); };
    playBButton.onClick    = [this] { playSource (DualChain::Source::wet); };
    trainStartButton.onClick = [this] { startTraining(); };
    nextButton.onClick       = [this] { nextTrainingTask(); };
    abortSessionButton.onClick = [this] { requestAbortSession(); };

    addAndMakeVisible (historyChart);
    addAndMakeVisible (abilityBars);
    addAndMakeVisible (eqCurve);

    profileFile = ProfileStore::defaultPath();
    profile.load (profileFile);

    pluginCatalog.load (profileFile.getSiblingFile ("plugins.json"));
    pluginMemory.load (plugin::PluginMemoryStore::defaultPathFor (profileFile));

    if (! pluginCatalog.getPlugins().empty())
        pluginLabel.setText (zh ("插件库：") + juce::String (pluginCatalog.getNumEffects())
                                 + zh (" 个效果器（缓存）"),
                             juce::dontSendNotification);

    updateProfileUi();

    // ---- 左侧导航
    brandIcon.setImage (juce::ImageCache::getFromMemory (EarLabArtwork::EarLab_png,
                          EarLabArtwork::EarLab_pngSize).rescaled (96, 96));
    brandIcon.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (brandIcon);
    brandLabel.setText ("EarLab", juce::dontSendNotification);
    brandLabel.setFont (uiFont (24.0f, true));
    brandLabel.setColour (juce::Label::textColourId, kTextBright);
    addAndMakeVisible (brandLabel);

    static const char* navNames[6] = { "首页", "素材", "训练", "VST 训练", "测评", "设置" };
    using Icon = NavButton::Icon;
    constexpr Icon icons[] = { Icon::overview, Icon::audio, Icon::training, Icon::plugin, Icon::assessment, Icon::settings };

    for (int i = 0; i < 6; ++i)
    {
        auto& button = navButtons[static_cast<size_t> (i)];
        button.setButtonText (zh (navNames[i]));
        button.setIcon (icons[i]);
        button.onClick = [this, i] { showPage (static_cast<Page> (i)); };
        addAndMakeVisible (button);
    }

    // ---- 首页
    homeWelcomeLabel.setText (zh ("耳机里听差异，从这里开始：选素材 → 训练 → 测评。"),
                              juce::dontSendNotification);
    homeWelcomeLabel.setFont (uiFont (theme::typeBody));
    homeWelcomeLabel.setColour (juce::Label::textColourId, theme::text2);
    addAndMakeVisible (homeWelcomeLabel);

    homeTrendLabel.setText (zh ("能力趋势 · 近 12 轮"), juce::dontSendNotification);
    homeTrendLabel.setFont (uiFont (theme::typeH2, true));
    homeTrendLabel.setColour (juce::Label::textColourId, kTextBright);
    addAndMakeVisible (homeTrendLabel);

    for (auto* b : { &homeMaterialButton, &homeTrainingButton, &homeAssessmentButton })
    {
        b->setColour (juce::TextButton::buttonColourId, kPanel);
        b->setColour (juce::TextButton::textColourOffId, kTextBright);
        addAndMakeVisible (*b);
    }

    for (auto* primary : { &homeTrainingButton, &trainStartButton, &loadButton, &pluginLoadButton })
        primary->setColour (juce::TextButton::buttonColourId, theme::accentStrong);
    homeMaterialButton.setButtonText (zh ("选择素材"));
    homeTrainingButton.setButtonText (zh ("开始训练"));
    homeAssessmentButton.setButtonText (zh ("开始测评"));

    homeMaterialButton.onClick = [this] { showPage (Page::material); };
    homeTrainingButton.onClick = [this] { showPage (Page::training); };
    homeAssessmentButton.onClick = [this] { showPage (Page::assessment); };

    // ---- 测评
    // P1-1：说明文字两行 16/400 text2，固定 20 基距（≥ 1.5em）；Label 无法控行距，拆成两行
    assessIntroLabel.setText (zh ("测评共 12 题：EQ 频段定位 6 题 + 压缩量判断 6 题，"
                                  "难度按专业混音师的平均分辨力设定。"),
                              juce::dontSendNotification);
    assessIntroLabel.setFont (uiFont (theme::typeBody));
    assessIntroLabel.setColour (juce::Label::textColourId, theme::text2);
    addAndMakeVisible (assessIntroLabel);

    assessIntroLabel2.setText (zh ("完成后生成能力报告并永久保存，可在下方回看历史。"),
                               juce::dontSendNotification);
    assessIntroLabel2.setFont (uiFont (theme::typeBody));
    assessIntroLabel2.setColour (juce::Label::textColourId, theme::text2);
    addAndMakeVisible (assessIntroLabel2);

    assessStartButton.setButtonText (zh ("开始测评"));
    // P0-3：主操作 = accentStrong 填充 + 白字（白字由 LookAndFeel 处理）
    assessStartButton.setColour (juce::TextButton::buttonColourId, theme::accentStrong);
    assessStartButton.setColour (juce::TextButton::textColourOffId, kTextBright);
    assessStartButton.onClick = [this] { beginAssessment(); };
    addAndMakeVisible (assessStartButton);

    // P1 结果态：CTA 右侧的时长说明（15/400 text3，与 CTA 同基线）
    assessCtaMetaLabel.setText (zh ("12 题 · 约 3 分钟"), juce::dontSendNotification);
    assessCtaMetaLabel.setFont (uiFont (theme::typeMeta));
    assessCtaMetaLabel.setColour (juce::Label::textColourId, kTextDim);
    assessCtaMetaLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (assessCtaMetaLabel);

    // 考试态专用文字（P0-2）：不复用 trainInfoLabel，训练页保持原样
    assessTrialLabel.setFont (uiFont (theme::typeH2, true));
    assessTrialLabel.setColour (juce::Label::textColourId, kTextBright);
    assessTrialLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (assessTrialLabel);

    assessCorrectLabel.setFont (uiFont (theme::typeMeta));
    assessCorrectLabel.setColour (juce::Label::textColourId, kTextDim);
    assessCorrectLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (assessCorrectLabel);

    // 考试态键位提示（P0-4），右对齐于反馈行，不遮挡左侧反馈文字
    assessKeyHintLabel.setText (zh ("空格 试听 · 1–4 作答 · Enter 下一题"),
                                juce::dontSendNotification);
    assessKeyHintLabel.setFont (uiFont (12.0f));
    assessKeyHintLabel.setColour (juce::Label::textColourId, kTextDim);
    assessKeyHintLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (assessKeyHintLabel);

    addAndMakeVisible (assessCard);
    addAndMakeVisible (assessHistory);

    // P2-6：JND 首次出现没有解释；成绩卡 / 历史表各挂一条 tooltip（不改变卡高）。
    {
        const auto jndTip = zh ("JND（最小可辨差异）：能可靠分辨的最小增益差。");
        assessCard.setTooltip (jndTip);
        assessHistory.setTooltip (jndTip);
    }

    assessPluginTitle.setText (zh ("插件辨别（独立分项）"));
    assessPluginLabel.setFont (uiFont (theme::typeBody));
    assessPluginLabel.setColour (juce::Label::textColourId, kTextDim);
    assessPluginLabel.setJustificationType (juce::Justification::topLeft);

    assessPluginButton.setButtonText (zh ("去 VST 训练"));
    assessPluginButton.setColour (juce::TextButton::buttonColourId, kPanel);
    assessPluginButton.setColour (juce::TextButton::textColourOffId, kTextBright);
    assessPluginButton.onClick = [this] { showPage (Page::plugin); };

    addAndMakeVisible (assessPluginTitle);
    addAndMakeVisible (assessPluginLabel);
    addAndMakeVisible (assessPluginButton);

    assessmentStore.loadForProfile (profileFile);

    // ---- 设置
    auto setupSettingsLabel = [this] (juce::Label& label, const juce::String& text, bool title)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (title ? uiFont (theme::typeH2, true) : uiFont (theme::typeBody));
        label.setColour (juce::Label::textColourId, title ? kTextBright : theme::text2);
        addAndMakeVisible (label);
    };

    setupSettingsLabel (settingsAudioTitle, zh ("音频"), true);
    setupSettingsLabel (settingsDataTitle, zh ("数据"), true);
    setupSettingsLabel (settingsAboutTitle, zh ("关于"), true);
    setupSettingsLabel (settingsAudioLabel, zh ("音频设备"), false);
    setupSettingsLabel (settingsDataLabel, {}, false);
    setupSettingsLabel (settingsAboutLabel, juce::String ("EarLab ") + JUCE_APPLICATION_VERSION_STRING, false);

    settingsDataLabel.setFont (theme::monoFont (theme::typeMeta));

    openDataButton.setButtonText (zh ("打开数据目录"));
    openDataButton.setColour (juce::TextButton::buttonColourId, kPanel);
    openDataButton.setColour (juce::TextButton::textColourOffId, kTextBright);
    openDataButton.onClick = [this] { profileFile.getParentDirectory().revealToUser(); };
    addAndMakeVisible (openDataButton);

    // ---- 本轮小结
    summaryTitle.setFont (uiFont (22.0f, true));
    summaryTitle.setColour (juce::Label::textColourId, kTextBright);
    summaryDetail.setFont (uiFont (13.0f));
    summaryDetail.setColour (juce::Label::textColourId, kTextDim);

    summaryAgainButton.setButtonText (zh ("再练一轮"));
    summaryAgainButton.setColour (juce::TextButton::buttonColourId, theme::accentStrong);
    summaryAgainButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    summaryAgainButton.onClick = [this] { hideSessionSummary(); startTraining(); };

    summaryHomeButton.setButtonText (zh ("回首页"));
    summaryHomeButton.setColour (juce::TextButton::buttonColourId, kPanel);
    summaryHomeButton.setColour (juce::TextButton::textColourOffId, kTextBright);
    summaryHomeButton.onClick = [this] { hideSessionSummary(); showPage (Page::home); };

    addAndMakeVisible (summaryTitle);
    addAndMakeVisible (summaryDetail);

    for (auto* b : { &summaryAgainButton, &summaryHomeButton })
    {
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }

    // 键盘焦点留在主组件上，数字键/空格才能全局生效
    for (auto* b : { &playAButton, &playBButton, &nextButton, &trainStartButton,
                     &playButton, &dryButton, &wetButton })
    {
        b->setMouseClickGrabsKeyboardFocus (false);
    }

    updateAssessmentView();
    updateProfileUi();   // 设置页的档案路径依赖这里刷新

    // P2-4 / P2-5：60Hz 只用于过渡插值；状态文本仍按每 12 帧（5Hz）刷新
    startTimerHz (60);
    updateStatus();
    showPage (Page::home);
    showLoadProblems();
}

MainComponent::~MainComponent()
{
    stopTimer();
    pluginLoadJob.reset();
    delete pluginBrowserWindow.getComponent();
    delete audioSettingsWindow.getComponent();
}

void MainComponent::timerCallback()
{
    pollPluginLoad();
    if (engine.hasPendingMaterialReload() && ! engine.reloadMaterialForSampleRate())
    {
        const auto message = zh ("素材无法转换到新的设备采样率。请重新载入音频或选择内置噪声。");
        materialLabel.setText (zh ("未载入素材"), juce::dontSendNotification);
        if (trainingActive)
            abortTraining (message);
        else
            reportVisibleError (zh ("素材重载失败"), message);
        eqCurve.repaint();
    }

    // 原有 5Hz 语义保留：每 12 个 60Hz 帧刷新一次状态文本
    if (++uiFrameCounter >= 12)
    {
        uiFrameCounter = 0;
        updateStatus();
        updateTransportText();
        updatePageStatus();
    }

    // P2-4：考试卡描边 120ms 过渡（0 = border，1 = accent@0.30）
    const bool targetChanged = examOutlineFade.setTarget (assessmentActive ? 1.0f : 0.0f);
    const bool animating = examOutlineFade.isAnimating();
    examOutlineFade.step();

    if (targetChanged || animating)
        repaint();
}

void MainComponent::chooseMaterial()
{
    chooser = std::make_unique<juce::FileChooser> (zh ("选择训练素材"), juce::File(),
                                                 "*.wav;*.aiff;*.aif;*.flac;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& fc)
        {
            if (safe && fc.getResult().existsAsFile())
                safe->openMaterial (fc.getResult());
        });
}

void MainComponent::abortTraining (const juce::String& message, bool showAlert)
{
    ++sessionEpoch;
    const bool wasAssessment = assessmentActive;
    const int answeredInRound = juce::jlimit (0, kAssessmentTrials, assessmentIndex);

    assessmentActive = false;
    trainingActive = false;
    answered = false;
    updatePageVisibility();

    engine.setPlaying (false);
    engine.setSource (DualChain::Source::dry);
    updateTransportText();

    setOptionsEnabled (false);
    nextButton.setEnabled (false);
    nextButton.setButtonText (zh ("下一题"));
    nextButton.setColour (juce::TextButton::buttonColourId, kPanel);
    skillBox.setEnabled (true);
    setEqControlsEnabled (true);
    setSessionControlsEnabled (true);

    trainInfoLabel.setText (wasAssessment ? zh ("测评已中止") : zh ("训练已中止"),
                            juce::dontSendNotification);
    feedbackLabel.setText (message, juce::dontSendNotification);

    if (wasAssessment)
    {
        updateAssessmentView();
        showPage (Page::assessment);
    }

    // U-P1-2 / D-P1-3：中止原因必须可见（AlertWindow），不能只写进隐藏的 feedbackLabel。
    // 用户主动中止/离开时 showAlert=false（用户刚在确认框里看过原因）。
    if (showAlert)
    {
        auto detail = message;

        if (wasAssessment)
            detail << zh ("\n\n本轮已答 ") << juce::String (answeredInRound)
                   << " / " << juce::String (kAssessmentTrials)
                   << zh (" 题，成绩作废（未保存）。请更换合适的素材后重新开始测评。");

        showAsyncAlert (juce::MessageBoxIconType::WarningIcon,
                        wasAssessment ? zh ("测评已中止") : zh ("训练已中止"),
                        detail);
    }
}

void MainComponent::openMaterial (const juce::File& file)
{
    if (trainingActive || assessmentActive)
        return;
    if (engine.loadFile (file))
    {
        materialLabel.setText (engine.getMaterialName(), juce::dontSendNotification);
        feedbackLabel.setText (zh ("素材已载入，选择技能后开始训练。"), juce::dontSendNotification);
        updateStatus();
        eqCurve.repaint();
    }
    else
        reportVisibleError (zh ("无法读取素材"), zh ("文件损坏、格式不支持或没有读取权限：") + file.getFullPathName());
}

void MainComponent::startNoise (int which)
{
    const auto type = which == 2 ? MaterialPlayer::NoiseType::pink
                                 : MaterialPlayer::NoiseType::white;

    if (! engine.loadNoise (type))
        return;

    materialLabel.setText (engine.getMaterialName(), juce::dontSendNotification);
    feedbackLabel.setText (type == MaterialPlayer::NoiseType::white
                               ? zh ("已切换到白噪声，选择技能后开始训练。")
                               : zh ("已切换到粉噪声，选择技能后开始训练。"),
                           juce::dontSendNotification);
    updateStatus();
    eqCurve.repaint();
}

void MainComponent::setProfilePath (const juce::File& file)
{
    cancelPluginLoad();
    delete pluginBrowserWindow.getComponent();
    pluginCatalog.clear();
    pluginMemory.clear();
    // P2-8：切档案时先结束进行中的会话，避免成绩写进新档案
    if (trainingActive || assessmentActive)
        abortTraining (zh ("切换档案，本轮已中止（未保存）。"), false);

    hideSessionSummary();

    profileFile = file;
    profile.clear();
    profile.load (profileFile);
    pluginCatalog.load (profileFile.getSiblingFile ("plugins.json"));
    pluginMemory.load (plugin::PluginMemoryStore::defaultPathFor (profileFile));
    assessmentStore.clear();
    assessmentStore.loadForProfile (profileFile);
    updateProfileUi();
    updateAssessmentView();
    updatePageVisibility();
    updatePageStatus();
    resized();
    // 载入问题由构造函数排的延迟提示统一上报（见 showLoadProblems）
}

void MainComponent::beginTrainingForQa (int skillId)
{
    if (pluginLoadJob) { pendingQaSkill = juce::jlimit (1, 3, skillId); return; }
    skillBox.setSelectedId (juce::jlimit (1, 3, skillId), juce::dontSendNotification);
    startTraining();
}

void MainComponent::showAudioSettings()
{
    if (audioSettingsWindow != nullptr) { audioSettingsWindow->toFront (true); return; }
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        engine.getDeviceManager(), 0, 2, 1, 2, false, false, true, false);

    selector->setSize (520, 420);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = zh ("音频设置");
    options.dialogBackgroundColour = kPanel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = false;
    audioSettingsWindow = options.launchAsync();
}

void MainComponent::togglePlayback()
{
    if (! engine.hasMaterial())
        return;

    engine.setPlaying (! engine.isPlaying());
    updateTransportText();
}

void MainComponent::playSource (DualChain::Source source)
{
    engine.setSource (source);
    engine.setPlaying (true);
    updateTransportText();
}

void MainComponent::updateTransportText()
{
    const bool playing = engine.isPlaying();
    playButton.setButtonText (playing ? zh ("停止") : zh ("播放"));
    playButton.setColour (juce::TextButton::buttonColourId,
                          playing ? theme::accentStrong : kPanel);

    const bool dry = engine.getSource() == DualChain::Source::dry;
    dryButton.setColour (juce::TextButton::buttonColourId, dry ? theme::accentStrong : kPanel);
    wetButton.setColour (juce::TextButton::buttonColourId, dry ? kPanel : theme::accentStrong);
}

void MainComponent::startTraining()
{
    if (assessmentActive || trainingActive || pluginLoadJob)
        return;

    if (! engine.hasMaterial())
    {
        // U-P1-2：失败原因不能只写进仅会话期可见的 feedbackLabel
        reportVisibleError (zh ("无法开始训练"),
                            zh ("请先在「素材」页载入一段素材，或切换到白噪 / 粉噪，再开始训练。"));
        return;
    }

    // 上一轮的结束小结如果还在，先收起，否则会挡住新一轮的控制条
    summaryVisible = false;

    if (currentPage == Page::plugin)
        skillBox.setSelectedId (3, juce::dontSendNotification);

    const int skillId = skillBox.getSelectedId();
    activeSkill = skillId == 2 ? Skill::compAmount
                : skillId == 3 ? Skill::pluginBand
                               : Skill::eqBandCenter;

    if (activeSkill == Skill::pluginBand && pluginGenerator == nullptr)
    {
        reportVisibleError (zh ("无法开始插件训练"),
                            zh ("请先在 VST 训练页点「加载 VST3」选一个效果器插件，再开始训练。"));
        return;
    }

    // 从首页/测评页直接开训时，切到对应的训练页
    if (assessmentActive == false)
    {
        if (activeSkill == Skill::pluginBand)
        {
            if (currentPage != Page::plugin)
                showPage (Page::plugin);
        }
        else if (currentPage != Page::training)
        {
            showPage (Page::training);
        }
    }

    ++sessionEpoch;
    trainingActive = true;
    answered = false;
    trialIndex = 0;
    correctCount = 0;
    displayedTrialIndex = 0;
    progressTotal = kTrialsPerSession;
    updatePageVisibility();

    // 训练期间禁止换素材/换插件，否则题面与听到的声音会脱节
    setSessionControlsEnabled (false);

    if (activeSkill == Skill::pluginBand)
    {
        pluginStaircase = Staircase (6.0f, 1.5f, 12.0f);
        preferredPluginParameter = -1;
    }
    else if (activeSkill == Skill::compAmount)
    {
        compStaircase = Staircase (9.0f, 1.5f, 12.0f);
        engine.unloadPlugin();
    }
    else
    {
        eqStaircase = Staircase (9.0f, 1.5f, 12.0f);
        engine.unloadPlugin();
    }

    elo.reset (profile.getStats (activeSkill).elo);

    setEqControlsEnabled (false);
    skillBox.setEnabled (false);
    nextButton.setButtonText (zh ("下一题"));
    // 训练页的「下一题」保持次要样式（测评主操作在 beginAssessment 里切换）
    nextButton.setColour (juce::TextButton::buttonColourId, kPanel);

    nextTrainingTask();
}

void MainComponent::setSessionControlsEnabled (bool shouldBeEnabled)
{
    loadButton.setEnabled (shouldBeEnabled);
    whiteNoiseButton.setEnabled (shouldBeEnabled);
    pinkNoiseButton.setEnabled (shouldBeEnabled);
    pluginLoadButton.setEnabled (shouldBeEnabled);
    pluginBrowserButton.setEnabled (shouldBeEnabled);
    settingsButton.setEnabled (shouldBeEnabled);
    exportButton.setEnabled (shouldBeEnabled);
    trainStartButton.setEnabled (shouldBeEnabled);
}

void MainComponent::nextTrainingTask()
{
    if (! trainingActive)
        return;

    if (assessmentActive)
    {
        if (assessmentIndex >= kAssessmentTrials)
        {
            finishAssessment();
            return;
        }

        trialIndex = assessmentIndex;
        activeSkill = assessmentSkillForTrial (assessmentIndex);
        skillBox.setSelectedId (activeSkill == Skill::compAmount ? 2 : 1,
                                juce::dontSendNotification);
    }
    else if (trialIndex >= kTrialsPerSession)
    {
        finishTraining();
        return;
    }

    if (activeSkill == Skill::pluginBand)
    {
        startPluginTrial();
        return;
    }

    engine.setPlaying (false);   // 校准与等响测量期间暂停播放，避免与音频线程竞争

    if (activeSkill == Skill::eqBandCenter)
    {
        const float gainDb = assessmentActive
                                 ? static_cast<float> (assessmentGainDbForTrial (assessmentIndex))
                                 : eqStaircase.getCurrentDb();

        currentTask = generator.makeEqBandTask (gainDb, 1.0f);

        engine.setCompressorNeutral();

        MultiBandEqProcessor::Settings settings;

        for (int band = 0; band < currentTask.numBandMoves; ++band)
        {
            const auto& move = currentTask.bandMoves[static_cast<size_t> (band)];
            settings.bands[band].frequency = move.frequencyHz;
            settings.bands[band].gainDb = move.gainDb;
            settings.bands[band].q = move.q;
            settings.bands[band].enabled = true;
        }

        engine.applyEqSettings (settings);
    }
    else
    {
        const float requestedGr = assessmentActive
                                      ? static_cast<float> (assessmentGainDbForTrial (assessmentIndex))
                                      : compStaircase.getCurrentDb();

        // P1-3：压缩题只考压缩。必须先关掉用户设置的 HP/LP：
        // calibrateThresholdForTargetGr 用的是原始素材，而湿链会先过高通/低通，
        // 保留高低切会让「正确答案」与实际听到的压缩量不一致。
        engine.setHighLowPassEnabled (false);
        engine.setEqParameters (1000.0f, 0.0f, 1.0f);
        lastMeasuredGr = engine.configureCompressorForTarget (requestedGr);

        // 素材太安静时压不出可辨识的量，宁可中止也不出无法作答的题
        if (lastMeasuredGr < 1.0f)
        {
            engine.setCompressorNeutral();
            abortTraining (zh ("素材太安静或过轻，无法校准压缩器（实测压缩量 < 1 dB）。"
                               "请更换更响、更长的素材后重试。"));
            return;
        }

        currentTask = generator.makeCompAmountTask (lastMeasuredGr);
    }

    engine.refreshLoudnessMatch();
    engine.setSource (DualChain::Source::dry);
    engine.setPlaying (true);
    updateTransportText();

    answered = false;

    for (int i = 0; i < 4; ++i)
    {
        auto& b = optionButtons[static_cast<size_t> (i)];
        b.setButtonText (currentTask.optionLabels[static_cast<size_t> (i)]);
        b.setResult (OptionButton::Result::none);
        b.setEnabled (true);
    }

    displayedTrialIndex = trialIndex;
    progressTotal = assessmentActive ? kAssessmentTrials : kTrialsPerSession;
    repaint();   // 进度圆点画在父组件上，必须显式重绘

    nextButton.setEnabled (false);

    if (assessmentActive)
    {
        // 屏上题号 = 当前题；提交答案时不动它，直到下一次「下一题」
        assessTrialLabel.setText (zh ("第 ")
                                      + juce::String (displayedTrialIndex + 1)
                                      + " / " + juce::String (kAssessmentTrials) + zh (" 题"),
                                  juce::dontSendNotification);
        assessCorrectLabel.setText (zh ("正确 ")
                                        + juce::String (assessEqCorrect + assessCompCorrect),
                                    juce::dontSendNotification);
    }
    else
    {
        trainInfoLabel.setText (zh ("第 ") + juce::String (trialIndex + 1) + " / "
                                    + juce::String (kTrialsPerSession) + zh (" 题 · 难度 ")
                                    + juce::String (currentTask.difficulty, 2)
                                    + zh (" · 正确 ") + juce::String (correctCount),
                                juce::dontSendNotification);
    }

    feedbackLabel.setText (activeSkill == Skill::eqBandCenter
                               ? (currentTask.numBandMoves > 1
                                      ? zh ("A 是干声，B 是处理后。判断变化最大的频段（不止一段）。")
                                      : zh ("A 是干声，B 是处理后。判断 B 改变了哪个频段。"))
                               : zh ("A 是干声，B 是处理后。判断 B 的压缩量约为多少 dB。"),
                           juce::dontSendNotification);

    feedbackLabel.setColour (juce::Label::textColourId, kTextBright);

    if (qaAutoPlay)
        juce::Timer::callAfterDelay (450, [safe = juce::Component::SafePointer<MainComponent> (this), epoch = sessionEpoch]
        {
            if (safe && safe->sessionEpoch == epoch && safe->trainingActive)
                safe->submitTrainingAnswer (safe->currentTask.correctIndex);
        });
}

void MainComponent::submitTrainingAnswer (int optionIndex)
{
    if (! trainingActive || answered)
        return;

    answered = true;

    const bool isPlugin = (activeSkill == Skill::pluginBand);
    const int correctIndex = isPlugin ? pluginTask.correctIndex : currentTask.correctIndex;
    const float difficulty = isPlugin ? pluginTask.difficulty : currentTask.difficulty;

    const bool correct = (optionIndex == correctIndex);

    if (correct)
        ++correctCount;

    if (! assessmentActive)
    {
        if (activeSkill == Skill::compAmount)
            compStaircase.registerAnswer (correct);
        else if (isPlugin)
            pluginStaircase.registerAnswer (correct);
        else
            eqStaircase.registerAnswer (correct);

        elo.update (correct, difficulty);
    }

    for (auto& b : optionButtons)
        b.setEnabled (false);

    optionButtons[static_cast<size_t> (optionIndex)].setResult (
        correct ? OptionButton::Result::correct : OptionButton::Result::wrong);

    if (! correct)
        optionButtons[static_cast<size_t> (correctIndex)].setResult (OptionButton::Result::correct);

    juce::String feedback = correct ? zh ("正确：") : zh ("错了：");

    if (activeSkill == Skill::eqBandCenter)
        feedback << zh ("正确答案 ") << TaskGenerator::formatHzLabel (currentTask.frequencyHz)
                 << zh ("，增益 ") << juce::String (currentTask.gainDb, 1) << " dB";
    else if (isPlugin)
        feedback << zh ("正确答案「") << pluginTask.optionLabels[static_cast<size_t> (correctIndex)]
                 << zh ("」，实测 ") << juce::String (pluginTask.measuredDeltaDb, 2) << " dB";
    else
        feedback << zh ("正确答案 ") << TaskGenerator::formatDbLabel (currentTask.targetGrDb)
                 << zh ("（实测 GR ") << juce::String (lastMeasuredGr, 1) << " dB）";

    feedbackLabel.setText (feedback, juce::dontSendNotification);
    feedbackLabel.setColour (juce::Label::textColourId, correct ? kGood : kBad);

    if (assessmentActive)
    {
        const int gainDb = assessmentGainDbForTrial (assessmentIndex);

        if (assessmentSkillForTrial (assessmentIndex) == Skill::eqBandCenter)
        {
            ++assessEqTotal;

            if (correct)
            {
                ++assessEqCorrect;
                assessEqJnd = juce::jmin (assessEqJnd, static_cast<float> (gainDb));
            }
        }
        else
        {
            ++assessCompTotal;

            if (correct)
            {
                ++assessCompCorrect;
                assessCompJnd = juce::jmin (assessCompJnd, static_cast<float> (gainDb));
            }
        }

        ++assessmentIndex;
        trialIndex = assessmentIndex;
    }
    else
    {
        ++trialIndex;
    }

    repaint();

    if (assessmentActive)
    {
        // 只更新「正确 N」；屏上题号与当前圆点保持在这道题（作答后该点转为已完成）
        assessCorrectLabel.setText (zh ("正确 ")
                                        + juce::String (assessEqCorrect + assessCompCorrect),
                                    juce::dontSendNotification);
    }
    else
    {
        trainInfoLabel.setText (zh ("第 ") + juce::String (displayedTrialIndex + 1)
                                    + " / " + juce::String (kTrialsPerSession) + zh (" 题 · 正确 ")
                                    + juce::String (correctCount),
                                juce::dontSendNotification);
    }

    nextButton.setEnabled (true);

    if (assessmentActive && assessmentIndex >= kAssessmentTrials)
        nextButton.setButtonText (zh ("查看报告"));
    else if (! assessmentActive && trialIndex >= kTrialsPerSession)
        nextButton.setButtonText (zh ("查看结果"));

    if (qaAutoPlay)
        juce::Timer::callAfterDelay (350, [safe = juce::Component::SafePointer<MainComponent> (this), epoch = sessionEpoch]
        {
            if (safe && safe->sessionEpoch == epoch && safe->trainingActive)
                safe->nextTrainingTask();
        });
}

Skill MainComponent::assessmentSkillForTrial (int index) const
{
    return index < 6 ? Skill::eqBandCenter : Skill::compAmount;
}

int MainComponent::assessmentGainDbForTrial (int index) const
{
    // 参照专业混音师的平均分辨力：6 / 4 / 3 / 2 dB 各两轮
    static const int gains[6] = { 6, 4, 3, 2, 6, 4 };

    return gains[static_cast<size_t> (index % 6)];
}

void MainComponent::beginAssessment()
{
    if (pluginLoadJob)
        return;
    if (! engine.hasMaterial())
    {
        showAsyncAlert (juce::MessageBoxIconType::InfoIcon,
                        zh ("需要先选素材"),
                        zh ("请先在「素材」页选择音频，或直接用白噪 / 粉噪，再开始测评。"));

        showPage (Page::material);
        return;
    }

    // 训练刚结束的小结可能还开着，测评开始前收起
    summaryVisible = false;

    if (trainingActive)
    {
        abortTraining (zh ("训练已中止（本轮未保存）。"), false);

        showAsyncAlert (juce::MessageBoxIconType::InfoIcon,
                        zh ("已中止当前训练"),
                        zh ("本轮训练没有保存。现在可以重新点「开始测评」。"));
        return;
    }

    assessmentActive = true;
    assessmentIndex = 0;
    assessEqCorrect = assessEqTotal = 0;
    assessCompCorrect = assessCompTotal = 0;
    assessEqJnd = 99.0f;
    assessCompJnd = 99.0f;

    ++sessionEpoch;
    trainingActive = true;
    answered = false;
    trialIndex = 0;
    correctCount = 0;
    displayedTrialIndex = 0;
    progressTotal = kAssessmentTrials;

    // P0-3：考试里「下一题」是主操作，切回普通页面时要恢复次要样式
    nextButton.setColour (juce::TextButton::buttonColourId, theme::accentStrong);

    setSessionControlsEnabled (false);
    setEqControlsEnabled (false);
    engine.unloadPlugin();
    elo.reset (1000.0);

    showPage (Page::assessment);
    updatePageVisibility();
    nextTrainingTask();
}

void MainComponent::finishAssessment()
{
    assessmentActive = false;
    trainingActive = false;
    answered = false;
    updatePageVisibility();

    engine.setPlaying (false);
    engine.setSource (DualChain::Source::dry);
    updateTransportText();

    setOptionsEnabled (false);
    nextButton.setEnabled (false);
    nextButton.setButtonText (zh ("下一题"));
    nextButton.setColour (juce::TextButton::buttonColourId, kPanel);
    setEqControlsEnabled (true);
    setSessionControlsEnabled (true);

    const int total = assessEqTotal + assessCompTotal;
    const int correct = assessEqCorrect + assessCompCorrect;
    const int score = total > 0 ? juce::roundToInt (100.0 * static_cast<double> (correct) / total) : 0;

    AssessmentResult result;
    result.timestamp   = juce::Time::currentTimeMillis();
    result.score       = score;
    result.grade       = AssessmentStore::gradeForScore (score);
    result.eqCorrect   = assessEqCorrect;
    result.eqTotal     = assessEqTotal;
    result.compCorrect = assessCompCorrect;
    result.compTotal   = assessCompTotal;
    result.eqJndDb     = assessEqJnd < 90.0f ? assessEqJnd : 0.0f;
    result.compJndDb   = assessCompJnd < 90.0f ? assessCompJnd : 0.0f;

    assessmentStore.add (result);

    if (! assessmentStore.save (AssessmentStore::defaultPathFor (profileFile)))
    {
        showAsyncAlert (juce::MessageBoxIconType::WarningIcon,
                        zh ("测评结果保存失败"),
                        zh ("成绩已生成，但没能写入磁盘（目录不可写或磁盘已满），退出后会丢失。"));
    }

    skillBox.setEnabled (true);

    updateAssessmentView();
    showPage (Page::assessment);
}

void MainComponent::updateAssessmentView()
{
    const auto& results = assessmentStore.getResults();

    if (results.empty())
        assessCard.clearResult();
    else
        assessCard.setResult (results.front());

    assessHistory.setResults (results);

    if (pluginGenerator != nullptr)
    {
        const auto stats = profile.getStats (Skill::pluginBand);

        assessPluginLabel.setText (zh ("已加载：") + pluginInfo.getDisplayName()
                                       + (stats.sessions > 0
                                              ? zh (" · Elo ") + juce::String (juce::roundToInt (stats.elo))
                                                    + zh (" · JND ") + juce::String (stats.thresholdDb, 1) + " dB"
                                              : zh (" · 还没有记录")),
                                   juce::dontSendNotification);
    }
    else
    {
        assessPluginLabel.setText (zh ("未加载插件。到 VST 训练页加载一个效果器后，即可练插件辨别。"),
                                   juce::dontSendNotification);
    }
}

void MainComponent::finishTraining()
{
    if (assessmentActive)
    {
        finishAssessment();
        return;
    }
    trainingActive = false;
    answered = false;
    updatePageVisibility();

    engine.setPlaying (false);
    engine.setSource (DualChain::Source::dry);
    updateTransportText();

    for (auto& b : optionButtons)
        b.setEnabled (false);

    nextButton.setEnabled (false);
    nextButton.setButtonText (zh ("下一题"));
    nextButton.setColour (juce::TextButton::buttonColourId, kPanel);
    skillBox.setEnabled (true);

    setEqControlsEnabled (true);
    setSessionControlsEnabled (true);

    const auto& stairs = (activeSkill == Skill::compAmount) ? compStaircase
                       : (activeSkill == Skill::pluginBand) ? pluginStaircase
                                                            : eqStaircase;

    if (activeSkill != Skill::pluginBand)
    {
        // 曲线面板自己从引擎读设置，这里只需刷新
        eqCurve.repaint();
    }

    const double accuracy = static_cast<double> (correctCount) / kTrialsPerSession * 100.0;

    feedbackLabel.setText (zh ("训练结束：正确 ") + juce::String (correctCount) + " / "
                               + juce::String (kTrialsPerSession) + zh ("，准确率 ")
                               + juce::String (accuracy, 0) + zh ("%，JND 估计 ")
                               + juce::String (stairs.getThresholdDb(), 1) + " dB",
                           juce::dontSendNotification);

    trainInfoLabel.setText (zh ("Elo ") + juce::String (elo.getRating(), 0)
                                + zh (" · 反转次数 ") + juce::String (stairs.getReversals()),
                            juce::dontSendNotification);

    SessionRecord record;
    record.skill       = activeSkill;
    record.timestamp   = juce::Time::currentTimeMillis();
    record.trials      = kTrialsPerSession;
    record.correct     = correctCount;
    record.thresholdDb = stairs.getThresholdDb();
    record.elo         = elo.getRating();

    profile.addSession (record);
    const bool saved = profile.save (profileFile);
    updateProfileUi();

    if (! saved)
        showAsyncAlert (juce::MessageBoxIconType::WarningIcon, zh ("训练结果保存失败"),
                        zh ("本轮成绩只保存在内存中。请检查档案目录的写入权限和磁盘空间。"));

    showSessionSummary (correctCount, kTrialsPerSession);
}

void MainComponent::setEqControlsEnabled (bool shouldBeEnabled)
{
    eqCurve.setEditable (shouldBeEnabled);
}

void MainComponent::updateProfileUi()
{
    auto fillCard = [] (StatCard& card, const juce::String& name, const SkillStats& stats)
    {
        if (stats.sessions == 0)
        {
            card.setData (name, zh ("—"), zh ("暂无记录 · 开始第一轮"), false);
            return;
        }

        card.setData (name, juce::String (juce::roundToInt (stats.elo)),
                      zh ("JND ") + juce::String (stats.thresholdDb, 1) + " dB"
                          + zh (" · 准确率 ")
                          + juce::String (juce::roundToInt (stats.getAccuracy() * 100.0)) + "%",
                      true);
    };

    fillCard (statCards[0], zh ("EQ 频段定位"), profile.getStats (Skill::eqBandCenter));
    fillCard (statCards[1], zh ("压缩量判断"), profile.getStats (Skill::compAmount));
    fillCard (statCards[2], zh ("插件辨别"), profile.getStats (Skill::pluginBand));

    historyChart.setHistory (profile.getHistory());

    const std::array<Skill, 3> skills { Skill::eqBandCenter, Skill::compAmount, Skill::pluginBand };
    std::array<SkillStats, 3> currentStats {};
    std::array<SkillStats, 3> firstStats {};
    bool hasBaseline = false;

    for (size_t i = 0; i < skills.size(); ++i)
    {
        currentStats[i] = profile.getStats (skills[i]);

        const double first = profile.getFirstElo (skills[i]);

        if (first > 0.0)
        {
            firstStats[i].elo = first;
            firstStats[i].sessions = 1;
            hasBaseline = true;
        }
    }

    abilityBars.setStats (currentStats, firstStats, hasBaseline);

    settingsDataLabel.setText (zh ("训练档案  ") + profileFile.getFullPathName() + "\n"
                                   + zh ("测评记录  ")
                                   + AssessmentStore::defaultPathFor (profileFile).getFullPathName(),
                               juce::dontSendNotification);
}

bool MainComponent::exportReportTo (const juce::File& file)
{
    return profile.exportReport (file, profileFile);
}

void MainComponent::exportReport()
{
    chooser = std::make_unique<juce::FileChooser> (zh ("导出训练报告"),
                                                   profileFile.getSiblingFile ("earlab-report.html"),
                                                   "*.html;*.csv");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& fc)
    {
        if (! safe) return;
        auto file = fc.getResult();

        if (file == juce::File())
            return;

        if (! file.hasFileExtension ("html;csv"))
        {
            file = file.withFileExtension ("html");
            if (file.exists())
            {
                safe->reportVisibleError (zh ("报告已存在"), zh ("请重新导出，直接选择已有的 .html 文件以确认覆盖，或使用新文件名。"));
                return;
            }
        }

        if (safe->exportReportTo (file))
            showAsyncAlert (juce::MessageBoxIconType::InfoIcon, zh ("报告已导出"),
                            file.getFullPathName());
        else
            safe->reportVisibleError (zh ("导出失败"), zh ("请检查训练档案能否读取，并选择独立、可写的 .html 或 .csv 文件。"));
    });
}

void MainComponent::updateStatus()
{
    juce::String text;

    if (! engine.hasMaterial())
    {
        text = zh ("未载入素材：点「载入素材」选择 wav / aiff / flac / mp3。");
    }
    else
    {
        const auto m = engine.getMatch();
        const auto applied = engine.getAppliedTrimsDb();

        text << engine.getMaterialName()
             << zh (" · 干声 ") << juce::String (m.dryLufs, 1) << " LUFS"
             << zh (" · 湿声 ") << juce::String (m.wetLufs, 1) << " LUFS"
             << zh (" · 等响 trim ") << juce::String (applied.first, 1)
             << " / " << juce::String (applied.second, 1) << " dB";
    }

    statusLabel.setText (text, juce::dontSendNotification);

    if (auto* device = engine.getDeviceManager().getCurrentAudioDevice())
    {
        deviceLabel.setText (device->getTypeName() + " · " + device->getName()
                                 + " · " + juce::String (device->getCurrentSampleRate() / 1000.0, 1) + " kHz",
                             juce::dontSendNotification);
        settingsAudioLabel.setText (deviceLabel.getText(), juce::dontSendNotification);
    }
    else
    {
        deviceLabel.setText (zh ("无音频设备（点「音频设置」选择输出）"), juce::dontSendNotification);
        settingsAudioLabel.setText (deviceLabel.getText(), juce::dontSendNotification);
    }
}

void MainComponent::showSessionSummary (int correct, int total)
{
    summaryVisible = true;

    const auto stats = profile.getStats (activeSkill);
    const int pct = total > 0 ? juce::roundToInt (100.0 * correct / total) : 0;

    summaryTitle.setText (zh ("本轮完成 · ") + juce::String (correct) + " / " + juce::String (total),
                          juce::dontSendNotification);

    summaryDetail.setText (zh ("准确率 ") + juce::String (pct) + "%"
                               + zh (" · JND ") + juce::String (stats.thresholdDb, 1) + " dB"
                               + zh (" · Elo ") + juce::String (juce::roundToInt (stats.elo))
                               + zh (" · 累计 ") + juce::String (stats.sessions) + zh (" 轮"),
                           juce::dontSendNotification);

    updatePageVisibility();
    resized();
    repaint();
}

void MainComponent::hideSessionSummary()
{
    if (! summaryVisible)
        return;

    summaryVisible = false;
    updatePageVisibility();
    resized();
    repaint();
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::spaceKey)
    {
        togglePlayback();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::returnKey)
    {
        // U-P1-3：Enter 只在小结真正可见、且当前就在它的宿主页时才重开一轮；
        // 否则回首页按 Enter 会凭空开始新训练。
        if (ui::canActivateSummaryWithEnter (summaryVisible, summaryAgainButton.isVisible(),
                                             currentPage == Page::training,
                                             currentPage == Page::plugin,
                                             assessmentActive, activeSkill))
        {
            summaryAgainButton.triggerClick();
            return true;
        }

        if (nextButton.isVisible() && nextButton.isEnabled())
        {
            nextButton.triggerClick();
            return true;
        }

        return false;
    }

    const auto character = key.getTextCharacter();

    if (character >= '1' && character <= '4')
    {
        // U-P1-1：数字键只在会话所属页作答；测评不能在其他页面盲答
        if (ui::canAcceptAnswerKey (currentPage == Page::training,
                                    currentPage == Page::plugin,
                                    currentPage == Page::assessment,
                                    ui::sessionKindOf (trainingActive, assessmentActive),
                                    activeSkill, answered, summaryVisible))
        {
            submitTrainingAnswer (static_cast<int> (character - '1'));
            return true;
        }

        return false;
    }

    if ((character == 'a' || character == 'A') && playAButton.isVisible())
    {
        playAButton.triggerClick();
        return true;
    }

    if ((character == 'b' || character == 'B') && playBButton.isVisible())
    {
        playBButton.triggerClick();
        return true;
    }

    return false;
}

bool MainComponent::sessionHostedOnPage (Page page) const noexcept
{
    return ui::sessionHostedOnPage (page == Page::training, page == Page::plugin,
                                    page == Page::assessment,
                                    ui::sessionKindOf (trainingActive, assessmentActive),
                                    activeSkill);
}

bool MainComponent::summaryHostedOnPage (Page page) const noexcept
{
    return ui::summaryHostedOnPage (page == Page::training, page == Page::plugin,
                                    summaryVisible, assessmentActive, activeSkill);
}

void MainComponent::requestAbortSession()
{
    if (! trainingActive || summaryVisible)
        return;

    const bool isAssessment = assessmentActive;

    juce::AlertWindow::showOkCancelBox (
        juce::MessageBoxIconType::WarningIcon,
        isAssessment ? zh ("中止本轮测评？") : zh ("中止本轮训练？"),
        isAssessment ? zh ("已答题目不会计入成绩，本轮测评将作废。")
                     : zh ("本轮训练进度不会保存。"),
        zh ("中止本轮"), zh ("继续"), this,
        juce::ModalCallbackFunction::create (
            [safe = juce::Component::SafePointer<MainComponent> (this), isAssessment] (int result)
            {
                if (safe == nullptr || result == 0)
                    return;

                safe->abortTraining (isAssessment
                                         ? zh ("已手动中止本轮测评（未计入成绩）。")
                                         : zh ("已手动中止本轮训练（未保存）。"),
                                     false);
            }));
}

void MainComponent::reportVisibleError (const juce::String& title, const juce::String& message)
{
    // U-P1-2：feedbackLabel 只在会话中可见，错误必须另有可见出口
    feedbackLabel.setText (message, juce::dontSendNotification);
    trainInfoLabel.setText (message, juce::dontSendNotification);

    juce::AlertWindow::showMessageBoxAsync (
        juce::MessageBoxIconType::WarningIcon, title, message, {}, nullptr,
        juce::ModalCallbackFunction::create ([] (int) {}));
}

void MainComponent::showLoadProblems()
{
    // 等消息循环起来、且命令行 --profile 已经生效后再提示（避免提示默认档案的旧状态）
    juce::Component::SafePointer<MainComponent> safe (this);

    juce::Timer::callAfterDelay (400, [safe]
    {
        if (safe == nullptr)
            return;

        auto& self = *safe;

        const auto show = [] (juce::MessageBoxIconType icon,
                              const juce::String& title,
                              const juce::String& message)
        {
            juce::AlertWindow::showMessageBoxAsync (
                icon, title, message, {}, nullptr,
                juce::ModalCallbackFunction::create ([] (int) {}));
        };

        if (self.pluginMemory.hadLoadProblem())
            show (juce::MessageBoxIconType::WarningIcon, zh ("插件记忆恢复提示"),
                  zh ("插件记忆主文件不可读，已尝试恢复备份。原有备份会保留，请检查数据目录。"));

        switch (self.profile.getLoadState())
        {
            case ProfileStore::LoadState::corrupted:
                show (juce::MessageBoxIconType::WarningIcon,
                      zh ("训练档案读取失败"),
                      zh ("无法解析训练档案：") + self.profileFile.getFullPathName()
                          + zh ("\n\n原文件保持不动，现有 .bak 不会被本次保存覆盖；"
                                "继续训练会写入新的可用档案。")
                          + zh ("\n如需恢复旧记录：退出后把 .bak 复制为 profile.json 再启动。"));
                break;

            case ProfileStore::LoadState::recoveredFromBackup:
                show (juce::MessageBoxIconType::InfoIcon,
                      zh ("训练档案已从备份恢复"),
                      zh ("主文件损坏，已从 .bak 恢复 ")
                          + juce::String (self.profile.getSessionCount())
                          + zh (" 轮训练记录；本次保存会保留 .bak。"));
                break;

            case ProfileStore::LoadState::ok:
            case ProfileStore::LoadState::missing:
            default:
                break;
        }

        switch (self.assessmentStore.getLoadState())
        {
            case AssessmentStore::LoadState::corrupted:
                show (juce::MessageBoxIconType::WarningIcon,
                      zh ("测评历史读取失败"),
                      zh ("无法解析测评历史文件：")
                          + self.assessmentStore.getLoadedFile().getFullPathName()
                          + zh ("\n\n原文件保持不动，现有 .bak 不会被本次保存覆盖；"
                                "新的测评会重建历史。"));
                break;

            case AssessmentStore::LoadState::recoveredFromBackup:
                show (juce::MessageBoxIconType::InfoIcon,
                      zh ("测评历史已从备份恢复"),
                      zh ("主文件损坏，已从 .bak 恢复 ")
                          + juce::String (self.assessmentStore.getCount())
                          + zh (" 条测评记录；本次保存会保留 .bak。"));
                break;

            case AssessmentStore::LoadState::ok:
            case AssessmentStore::LoadState::missing:
            default:
                break;
        }
    });
}

} // namespace earlab
