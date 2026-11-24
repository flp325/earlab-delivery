#pragma once

#include "../engine/Assessment.h"
#include "../engine/AudioEngine.h"
#include "../engine/ProfileStore.h"
#include "../engine/TrainingTask.h"
#include "../plugin/PluginCatalog.h"
#include "../plugin/PluginMemory.h"
#include "../plugin/PluginLoadJob.h"
#include "../plugin/PluginTask.h"
#include "AbilityBars.h"
#include "AssessmentCard.h"
#include "AssessmentHistory.h"
#include "EqCurvePanel.h"
#include "FadeBase.h"
#include "HistoryChart.h"
#include "LabButton.h"
#include "NavButton.h"
#include "OptionButton.h"
#include "StatCard.h"
#include "StatusChip.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <vector>

namespace earlab
{

/** Label 经 LookAndFeel 取字体时只保留字号、不保留字重；P1 需要 600 字重的标题用自绘。 */
class PaintedTitle final : public juce::Component
{
public:
    void setText (const juce::String& newText)
    {
        if (text == newText)
            return;

        text = newText;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::text1);
        g.setFont (theme::uiFont (theme::typeH2, true));
        g.drawText (text, getLocalBounds(), juce::Justification::centredLeft, true);
    }

private:
    juce::String text;
};

class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    /** 右侧竖排的六个模块。 */
    enum class Page
    {
        home = 0,
        material = 1,
        training = 2,
        plugin = 3,
        assessment = 4,
        settings = 5
    };

    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    /** 供命令行/自动化跳页。 */
    void showPage (Page page);

    /** 供命令行/自动化打开素材。 */
    void openMaterial (const juce::File& file);
    void beginTrainingForQa (int skillId = 1);
    void setProfilePath (const juce::File& file);

    /** 供命令行/自动化加载第三方 VST3。 */
    void loadPluginFile (const juce::File& file);

    /** 供命令行/按钮启动内置噪声源（1 = 白噪，2 = 粉噪）。 */
    void startNoise (int which);

    /** 自动化验收：自动按正确答案作答，跑完一整轮。 */
    void setQaAutoPlay (bool shouldAutoPlay) { qaAutoPlay = shouldAutoPlay; }

    /** 自动化验收：直接打开插件库窗口。 */
    void showPluginBrowserForQa() { showPluginBrowser(); }

    /** 自动化验收：强制插件题型（缺省自动）。 */
    void setQaQuestionKind (plugin::PluginQuestionKind kind) { qaKind = kind; }

    /** 自动化验收：把报告写到指定文件（按扩展名选 CSV / HTML）。 */
    bool exportReportTo (const juce::File& file);

    /** 自动化验收：直接开始测评。 */
    void beginAssessmentForQa()
    {
        if (pluginLoadJob) { pendingQaSkill = -1; return; }
        beginAssessment();
    }

private:
    void timerCallback() override;

    void chooseMaterial();
    void choosePlugin();
    void showPluginBrowser();
    void loadPluginInfo (const plugin::PluginInfo& info);
    void beginPluginLoad (const juce::String& path, std::optional<plugin::PluginInfo> info = {});
    void pollPluginLoad();
    void cancelPluginLoad();
    bool finishPluginLoad (const plugin::PluginInfo& info);
    void showAudioSettings();
    void togglePlayback();
    void playSource (DualChain::Source source);
    void updateStatus();
    void updateTransportText();

    void startTraining();
    void nextTrainingTask();
    void startPluginTrial();
    void applyPluginTask (const plugin::PluginTask& task);
    void abortTraining (const juce::String& message, bool showAlert = true);
    void requestAbortSession();
    void reportVisibleError (const juce::String& title, const juce::String& message);
    void showLoadProblems();
    void setSessionControlsEnabled (bool shouldBeEnabled);
    void setOptionsEnabled (bool shouldBeEnabled);
    void submitTrainingAnswer (int optionIndex);
    void finishTraining();
    void setEqControlsEnabled (bool shouldBeEnabled);
    void updateProfileUi();
    void exportReport();

    // ---- 页面与会话归属（U-P1-1：会话控件只在会话所属页可用）
    [[nodiscard]] bool sessionHostedOnPage (Page page) const noexcept;
    [[nodiscard]] bool sessionHostedOnCurrentPage() const noexcept
    {
        return sessionHostedOnPage (currentPage);
    }
    [[nodiscard]] bool summaryHostedOnPage (Page page) const noexcept;

    // ---- 页面与测评
    void layoutActivePage (juce::Rectangle<int> content);
    void updatePageVisibility();
    void updatePageStatus();

    void beginAssessment();
    void finishAssessment();
    [[nodiscard]] int assessmentGainDbForTrial (int index) const;
    [[nodiscard]] Skill assessmentSkillForTrial (int index) const;
    void updateAssessmentView();

    AudioEngine engine;

    TaskGenerator generator { 20261023u };
    Staircase eqStaircase { 9.0f, 1.5f, 12.0f };
    Staircase compStaircase { 9.0f, 1.5f, 12.0f };
    Staircase pluginStaircase { 6.0f, 1.5f, 12.0f };
    EloRating elo;
    TrainingTask currentTask;
    Skill activeSkill = Skill::eqBandCenter;
    float lastMeasuredGr = 0.0f;

    // 第三方插件：generator 专用于离线测参数，音频线程用引擎里的另一个实例
    plugin::PluginCatalog pluginCatalog;
    plugin::PluginMemoryStore pluginMemory;
    plugin::PluginInfo pluginInfo;
    std::vector<plugin::HostParameter> pluginParams;
    std::unique_ptr<juce::AudioPluginInstance> pluginGenerator;
    std::unique_ptr<plugin::PluginLoadJob> pluginLoadJob;
    int pendingQaSkill = 0;
    std::uint64_t sessionEpoch = 0;
    plugin::PluginTask pluginTask;
    int preferredPluginParameter = -1;
    std::uint32_t pluginSeed = 20261024u;

    ProfileStore profile;
    juce::File profileFile;
    HistoryChart historyChart;
    AbilityBars abilityBars;
    EqCurvePanel eqCurve { engine };

    static constexpr int kTrialsPerSession = 10;
    int trialIndex = 0;
    int correctCount = 0;
    bool trainingActive = false;
    bool answered = false;
    bool qaAutoPlay = false;
    plugin::PluginQuestionKind qaKind = plugin::PluginQuestionKind::automatic;

    juce::Label titleLabel;
    juce::Label deviceLabel;
    juce::Label materialLabel;
    juce::Label statusLabel;
    juce::Label trainInfoLabel;
    juce::Label feedbackLabel;
    juce::Label pluginLabel;
    StatusChip pageStatus;
    std::array<StatCard, 3> statCards;

    // 左侧导航
    std::array<NavButton, 6> navButtons;
    juce::ImageComponent brandIcon;
    juce::Label brandLabel;
    Page currentPage = Page::home;

    // 首页
    juce::Label homeWelcomeLabel;
    juce::Label homeTrendLabel;
    LabButton homeMaterialButton, homeTrainingButton, homeAssessmentButton;

    // 测评
    bool assessmentActive = false;
    int assessmentIndex = 0;
    int displayedTrialIndex = 0;   // 屏上题号：作答后锁定，直到「下一题」才推进
    int assessEqCorrect = 0, assessEqTotal = 0;
    int assessCompCorrect = 0, assessCompTotal = 0;
    float assessEqJnd = 99.0f, assessCompJnd = 99.0f;
    AssessmentStore assessmentStore;
    juce::Label assessIntroLabel;     // 说明第一行（16/400 text2）
    juce::Label assessIntroLabel2;    // 说明第二行（固定 20 基距，行距 ≥ 19）
    LabButton assessStartButton;
    juce::Label assessCtaMetaLabel;   // CTA 右侧「12 题 · 约 3 分钟」
    juce::Label assessTrialLabel;    // 考试态：第 N / 12 题（文字等价）
    juce::Label assessCorrectLabel;  // 考试态：正确 N
    juce::Label assessKeyHintLabel;  // 考试态：键位提示
    AssessmentCard assessCard;
    AssessmentHistory assessHistory;
    PaintedTitle assessPluginTitle;  // 18/600：Label 会丢字重，用自绘
    juce::Label assessPluginLabel;
    LabButton assessPluginButton;

    // 设置
    juce::Label settingsAudioTitle, settingsDataTitle, settingsAboutTitle;
    juce::Label settingsAudioLabel, settingsDataLabel, settingsAboutLabel;
    LabButton openDataButton;

    static constexpr int kAssessmentTrials = 12;

    LabButton settingsButton;
    LabButton loadButton;
    LabButton whiteNoiseButton;
    LabButton pinkNoiseButton;
    LabButton playButton;
    LabButton dryButton;
    LabButton wetButton;
    LabButton pluginLoadButton;
    LabButton pluginBrowserButton;
    LabButton exportButton;

    juce::ComboBox skillBox;
    LabButton trainStartButton;
    LabButton playAButton;
    LabButton playBButton;
    LabButton nextButton;
    LabButton abortSessionButton;
    std::array<OptionButton, 4> optionButtons;

    // U-P1-1：离开会话页的确认 / 内部切换时抑制二次确认
    bool leaveConfirmationPending = false;
    bool suppressLeaveConfirm = false;

    // 本轮小结
    bool summaryVisible = false;
    juce::Label summaryTitle, summaryDetail;
    LabButton summaryAgainButton, summaryHomeButton;
    void showSessionSummary (int correct, int total);
    void hideSessionSummary();

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component::SafePointer<juce::DialogWindow> pluginBrowserWindow, audioSettingsWindow;
    juce::Rectangle<int> contentBounds;
    juce::Rectangle<int> sidebarBounds;
    std::vector<juce::Rectangle<int>> cardRects;
    juce::Rectangle<int> pluginPanel;
    juce::Rectangle<int> eqPanel;
    juce::Rectangle<int> trainPanel;
    juce::Rectangle<int> statsPanel;
    juce::Rectangle<int> progressDots;
    int progressTotal = 10;

    // P2-4：考试卡描边的 120ms 过渡值（0 = border，1 = accent@0.30）
    FadeValue examOutlineFade;
    juce::Rectangle<int> examCardRect;   // 仅考试态有效：只给这张卡上强调描边

    // P2-5：主 Timer 提到 60Hz 供过渡插值使用；原有 5Hz 状态刷新按每 12 帧一次保留
    int uiFrameCounter = 0;

    // P2-6：JND 首次出现的解释用 tooltip（父组件 = this，随窗口一起绘制/截图）
    juce::TooltipWindow tooltipWindow { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace earlab
