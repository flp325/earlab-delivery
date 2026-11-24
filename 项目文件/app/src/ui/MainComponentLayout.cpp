#include "MainComponent.h"
#include "MainComponentSupport.h"
#include "PluginBrowser.h"
#include "SessionRouting.h"
#include "TimeFormat.h"

namespace earlab
{
using namespace ui::detail;

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::canvas);

    // 左侧导航
    g.setColour (theme::surface);
    g.fillRect (sidebarBounds);
    g.setColour (theme::border);
    g.drawVerticalLine (sidebarBounds.getRight() - 1,
                        (float) sidebarBounds.getY(), (float) sidebarBounds.getBottom());

    // 卡片
    // P2-4：考试卡描边 accent@0.30（结束回到 border）；120ms 由 examOutlineFade 插值
    const auto examOutline = theme::surface.interpolatedWith (theme::accent, theme::examOutlineAlpha);

    for (const auto& card : cardRects)
    {
        g.setColour (theme::surface);
        g.fillRoundedRectangle (card.toFloat(), (float) theme::radiusCard);

        const bool examCard = assessmentActive
                              && ! examCardRect.isEmpty()
                              && card == examCardRect;

        g.setColour (examCard ? theme::border.interpolatedWith (examOutline, examOutlineFade.get())
                              : theme::border);
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), (float) theme::radiusCard, 1.0f);
    }

    // 训练/测评进度圆点（三态：未开始 / 已完成 / 当前，P0-2）
    // U-P1-1：只在会话所属页绘制：训练页不画测评进度，测评页不画训练进度
    if (! progressDots.isEmpty() && trainingActive && ! summaryVisible
        && sessionHostedOnPage (currentPage))
    {
        const int total = juce::jmax (1, progressTotal);
        constexpr int dotSize = 8;
        constexpr int step = 20;
        constexpr int ringSize = dotSize + 4;   // 2px 外环 → 视觉 12px
        const int centreY = progressDots.getCentreY();

        const int index = displayedTrialIndex;
        const bool currentVisible = ! answered;

        int x = progressDots.getX();

        for (int i = 0; i < total; ++i)
        {
            const bool current = currentVisible && i == index;
            const bool done = i < index || (i == index && ! current);

            if (current)
            {
                // 当前 = text1 填充 + 2px accent@35% 外环（整行最亮）
                g.setColour (theme::accent.withAlpha (0.35f));
                g.fillEllipse ((float) (x - 2), (float) (centreY - ringSize / 2),
                               (float) ringSize, (float) ringSize);
                g.setColour (theme::text1);
            }
            else
            {
                g.setColour (done ? theme::accent : theme::dotTodo);
            }

            g.fillEllipse ((float) x, (float) (centreY - dotSize / 2),
                           (float) dotSize, (float) dotSize);
            x += step;
        }
    }
}

void MainComponent::resized()
{
    auto full = getLocalBounds();
    sidebarBounds = full.removeFromLeft (theme::sidebarWidth);
    contentBounds = full.reduced (theme::contentPadding, theme::margin);

    auto nav = sidebarBounds.reduced (12, 16);
    auto brand = nav.removeFromTop (42).reduced (6, 0);
    brandIcon.setBounds (brand.removeFromLeft (40));
    brand.removeFromLeft (5);
    brandLabel.setBounds (brand);
    nav.removeFromTop (22);

    for (auto& button : navButtons)
    {
        if (&button == &navButtons.back())
            nav.removeFromTop (16);
        button.setBounds (nav.removeFromTop (44));
        nav.removeFromTop (3);
    }

    layoutActivePage (contentBounds);
}

void MainComponent::showPage (Page page)
{
    // 上一个「离开会话页」确认还没结束时，忽略连续的导航点击，避免叠加对话框
    if (page != currentPage && leaveConfirmationPending)
        return;

    // U-P1-1：会话进行中离开会话所属页，必须先确认；取消则留在原页。
    // 不计 confirmed 的内部切换（suppressLeaveConfirm）则直接放行。
    if (! suppressLeaveConfirm && page != currentPage && trainingActive && ! summaryVisible
        && ! sessionHostedOnPage (page))
    {
        leaveConfirmationPending = true;

        const bool isAssessment = assessmentActive;
        const auto title   = isAssessment ? zh ("离开测评页？") : zh ("离开训练页？");
        const auto message = isAssessment
            ? zh ("离开会中止本轮测评，已答题目不计入成绩。")
            : zh ("离开会中止本轮训练，本轮进度不会保存。");

        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::WarningIcon, title, message,
            zh ("中止并离开"), zh ("继续本轮"), this,
            juce::ModalCallbackFunction::create (
                [safe = juce::Component::SafePointer<MainComponent> (this), page, isAssessment] (int result)
                {
                    if (safe == nullptr)
                        return;

                    safe->leaveConfirmationPending = false;

                    if (result == 0)   // 取消：留在原页，不改变任何会话状态
                        return;

                    safe->suppressLeaveConfirm = true;
                    safe->abortTraining (isAssessment
                                             ? zh ("已离开测评页，本轮测评已中止（未计入成绩）。")
                                             : zh ("已离开训练页，本轮训练已中止（未保存）。"),
                                         false);
                    safe->suppressLeaveConfirm = false;
                    safe->showPage (page);
                }));

        return;
    }

    // 小结不属于任何会话；离开宿主页时收起，返回时回到正常空闲态
    if (page != currentPage && summaryVisible && ! summaryHostedOnPage (page))
        hideSessionSummary();

    currentPage = page;

    static const char* pageTitles[6] = { "耳力训练", "素材", "训练", "插件辨别（VST3）", "能力测评", "设置" };
    titleLabel.setText (zh (pageTitles[static_cast<int> (page)]), juce::dontSendNotification);

    for (size_t i = 0; i < navButtons.size(); ++i)
        navButtons[i].setToggleState (static_cast<int> (page) == static_cast<int> (i),
                                      juce::dontSendNotification);

    updatePageVisibility();
    updatePageStatus();
    resized();
    repaint();
    grabKeyboardFocus();

    if (page == Page::assessment)
        updateAssessmentView();
}

void MainComponent::updatePageStatus()
{
    juce::String text;
    auto tone = StatusChip::Tone::neutral;

    switch (currentPage)
    {
        case Page::home:
            text = engine.hasMaterial() ? zh ("就绪") : zh ("等待素材");
            if (engine.hasMaterial()) tone = StatusChip::Tone::success;
            break;

        case Page::material:
            text = engine.hasMaterial() ? engine.getMaterialName() : zh ("未载入素材");
            break;

        case Page::training:
            // U-P1-1：训练页状态只由训练会话推导；测评状态只在测评页显示
            if (trainingActive && ! assessmentActive)
            {
                text = zh ("训练中 · 第 ") + juce::String (displayedTrialIndex + 1)
                           + " / " + juce::String (kTrialsPerSession) + zh (" 题");
                tone = StatusChip::Tone::accent;
            }
            else
                text = zh ("就绪");
            break;

        case Page::plugin:
            text = pluginGenerator != nullptr ? zh ("已加载：") + pluginInfo.getDisplayName()
                                              : zh ("未加载插件");
            if (pluginGenerator != nullptr) tone = StatusChip::Tone::success;
            break;

        case Page::assessment:
            if (assessmentActive)
            {
                // P2-2：胶囊只放页面级状态；用屏上题号，作答后不提前跳到下一题
                text = zh ("测评中 · 第 ") + juce::String (displayedTrialIndex + 1)
                           + " / " + juce::String (kAssessmentTrials) + zh (" 题");
                tone = StatusChip::Tone::accent;
            }
            else if (! assessmentStore.getResults().empty())
            {
                // P2-2/P2-6：等级与分数交给成绩卡/历史表，胶囊只显示完成时间（MM-dd HH:mm）
                text = zh ("已完成 · ")
                           + formatShortDateTime (assessmentStore.getResults().front().timestamp);
                tone = StatusChip::Tone::success;
            }
            else
            {
                text = zh ("12 题 · 约 3 分钟");
            }
            break;

        case Page::settings:
            text = juce::String ("EarLab ") + JUCE_APPLICATION_VERSION_STRING;
            break;
    }

    if (pluginLoadJob && currentPage == Page::plugin)
        return; // The job owns the progress text until completion/cancellation.
    pageStatus.setText (text);
    pageStatus.setTone (tone);
}

void MainComponent::updatePageVisibility()
{
    const bool home = currentPage == Page::home;
    const bool material = currentPage == Page::material;
    const bool training = currentPage == Page::training;
    const bool pluginPage = currentPage == Page::plugin;
    const bool assessment = currentPage == Page::assessment;
    const bool settings = currentPage == Page::settings;

    // 测评只在测评页内进行；训练控件只在其所属页面显示（U-P1-1）。
    // 绝不能再用旧逻辑「trainingLike = training || pluginPage || assessmentExam」：
    // 那会让测评中的选项/下一题泄漏到训练页，并允许跨页提交答案。
    const bool assessmentExam = assessment && assessmentActive;
    const bool trainingLike = training || pluginPage || assessmentExam;

    const bool sessionHostedHere = sessionHostedOnCurrentPage();
    const bool showSessionControls = trainingActive && sessionHostedHere && ! summaryVisible;

    auto set = [] (juce::Component& c, bool visible) { c.setVisible (visible); };

    titleLabel.setVisible (true);
    pageStatus.setVisible (true);

    set (homeWelcomeLabel, home);
    set (deviceLabel, home);
    set (homeTrendLabel, home);

    for (auto& card : statCards)
        set (card, home);

    set (homeMaterialButton, home);
    set (homeTrainingButton, home);
    set (homeAssessmentButton, home);

    set (loadButton, material);
    set (whiteNoiseButton, material);
    set (pinkNoiseButton, material);
    set (materialLabel, material);
    set (playButton, material);
    set (dryButton, material);
    set (wetButton, material);
    set (statusLabel, material);
    eqCurve.setVisible (material);

    const bool summary = summaryVisible && summaryHostedOnPage (currentPage) && ! assessmentActive;

    set (summaryTitle, summary);
    set (summaryDetail, summary);
    set (summaryAgainButton, summary);
    set (summaryHomeButton, summary);

    set (skillBox, training && ! assessmentActive && ! summaryVisible);
    set (trainStartButton, trainingLike && ! assessmentActive && ! summaryVisible && ! trainingActive);
    set (trainInfoLabel, trainingLike && ! summaryVisible && ! assessmentExam);
    set (playAButton, showSessionControls);
    set (playBButton, showSessionControls);
    set (nextButton, showSessionControls);
    set (abortSessionButton, showSessionControls);
    set (feedbackLabel, showSessionControls);
    set (historyChart, home || training || pluginPage);
    set (abilityBars, training || pluginPage);

    for (auto& button : optionButtons)
        set (button, showSessionControls);

    set (pluginLoadButton, pluginPage);
    set (pluginBrowserButton, pluginPage);
    set (pluginLabel, pluginPage);

    // 考试期间隐藏测评介绍与报告卡，只留答题卡
    set (assessIntroLabel, assessment && ! assessmentExam);
    set (assessIntroLabel2, assessment && ! assessmentExam);
    set (assessCtaMetaLabel, assessment && ! assessmentExam);
    set (assessStartButton, assessment && ! assessmentExam);
    // P1-7：无测评记录时成绩卡不显示、也不占位
    set (assessCard, assessment && ! assessmentExam && ! assessmentStore.getResults().empty());
    set (assessHistory, assessment && ! assessmentExam);
    set (assessPluginTitle, assessment && ! assessmentExam);
    set (assessPluginLabel, assessment && ! assessmentExam);
    set (assessPluginButton, assessment && ! assessmentExam);

    set (assessTrialLabel, assessmentExam);
    set (assessCorrectLabel, assessmentExam);
    set (assessKeyHintLabel, assessmentExam);

    set (settingsAudioTitle, settings);
    set (settingsDataTitle, settings);
    set (settingsAboutTitle, settings);
    set (settingsAudioLabel, settings);
    set (settingsDataLabel, settings);
    set (settingsAboutLabel, settings);
    set (openDataButton, settings);
    set (settingsButton, settings);
    set (exportButton, settings);
    layoutActivePage (contentBounds);
    repaint();
}

void MainComponent::layoutActivePage (juce::Rectangle<int> content)
{
    cardRects.clear();
    examCardRect = {};

    auto area = content;

    // 页头：标题 + 状态胶囊
    auto header = area.removeFromTop (42);
    pageStatus.setBounds (header.removeFromRight (240).withSizeKeepingCentre (240, 30));
    header.removeFromRight (12);
    titleLabel.setBounds (header);
    area.removeFromTop (16);

    if (currentPage == Page::home)
    {
        auto welcome = area.removeFromTop (144);
        cardRects.push_back (welcome);
        auto intro = welcome.reduced (20, 16);
        homeWelcomeLabel.setBounds (intro.removeFromTop (24));
        deviceLabel.setBounds (intro.removeFromTop (22));
        auto actions = intro.removeFromBottom (40);
        homeTrainingButton.setBounds (actions.removeFromLeft (160));
        actions.removeFromLeft (12);
        homeMaterialButton.setBounds (actions.removeFromLeft (120));
        actions.removeFromLeft (12);
        homeAssessmentButton.setBounds (actions.removeFromLeft (120));

        area.removeFromTop (16);
        auto cardsRow = area.removeFromTop (108);
        for (size_t i = 0; i < statCards.size(); ++i)
        {
            const auto remaining = static_cast<int> (statCards.size() - i);
            statCards[i].setBounds (cardsRow.removeFromLeft ((cardsRow.getWidth() - 12 * (remaining - 1)) / remaining));
            cardsRow.removeFromLeft (12);
        }

        area.removeFromTop (16);

        auto trendCard = area;

        if (trendCard.getHeight() > 80)
        {
            cardRects.push_back (trendCard);
            auto inner = trendCard.reduced (20, 16);
            homeTrendLabel.setBounds (inner.removeFromTop (22));
            inner.removeFromTop (10);
            historyChart.setBounds (inner);
        }

        return;
    }

    if (currentPage == Page::material)
    {
        auto toolbar = area.removeFromTop (72);
        cardRects.push_back (toolbar);
        auto t = toolbar.reduced (20, 16);
        loadButton.setBounds (t.removeFromLeft (110));
        t.removeFromLeft (8);
        whiteNoiseButton.setBounds (t.removeFromLeft (72));
        t.removeFromLeft (8);
        pinkNoiseButton.setBounds (t.removeFromLeft (72));
        t.removeFromLeft (16);
        materialLabel.setBounds (t);

        area.removeFromTop (16);

        auto transport = area.removeFromTop (56);
        cardRects.push_back (transport);
        auto tr = transport.reduced (20, 8);
        playButton.setBounds (tr.removeFromLeft (96));
        tr.removeFromLeft (12);
        dryButton.setBounds (tr.removeFromLeft (96));
        tr.removeFromLeft (8);
        wetButton.setBounds (tr.removeFromLeft (96));

        area.removeFromTop (16);
        statusLabel.setBounds (area.removeFromBottom (24));
        area.removeFromBottom (8);

        eqPanel = area;
        cardRects.push_back (eqPanel);
        eqCurve.setBounds (eqPanel.reduced (12));
        return;
    }

    if (currentPage == Page::training || currentPage == Page::plugin)
    {
        if (currentPage == Page::plugin)
        {
            pluginPanel = area.removeFromTop (72);
            cardRects.push_back (pluginPanel);
            auto p = pluginPanel.reduced (20, 16);
            pluginLoadButton.setBounds (p.removeFromLeft (110));
            p.removeFromLeft (8);
            pluginBrowserButton.setBounds (p.removeFromLeft (100));
            p.removeFromLeft (16);
            pluginLabel.setBounds (p);
            area.removeFromTop (16);
        }

        const bool inSession = trainingActive && sessionHostedOnPage (currentPage);
        trainPanel = area.removeFromTop (summaryVisible || inSession ? 248 : 76);
        cardRects.push_back (trainPanel);
        auto trainArea = trainPanel.reduced (20, 14);

        auto infoRow = trainArea.removeFromTop (40);

        if (currentPage == Page::training)
        {
            skillBox.setBounds (infoRow.removeFromLeft (200));
            infoRow.removeFromLeft (12);
        }

        trainStartButton.setBounds (infoRow.removeFromLeft (120));
        infoRow.removeFromLeft (12);
        trainInfoLabel.setBounds (infoRow);

        trainArea.removeFromTop (8);
        progressDots = trainArea.removeFromTop (14);
        trainArea.removeFromTop (8);

        auto playRow = trainArea.removeFromTop (40);
        playAButton.setBounds (playRow.removeFromLeft (96));
        playRow.removeFromLeft (8);
        playBButton.setBounds (playRow.removeFromLeft (96));
        playRow.removeFromLeft (8);
        nextButton.setBounds (playRow.removeFromLeft (104));
        playRow.removeFromLeft (theme::space16);
        // U-P1-1：会话卡内的中止入口（次级样式，靠右）
        abortSessionButton.setBounds (playRow.removeFromRight (110)
                                          .withSizeKeepingCentre (110, theme::compactHeight));

        trainArea.removeFromTop (10);

        // P2-3：选项行高度 44 → 48（控件高度只留 32/40/48）
        auto optionRow = trainArea.removeFromTop (theme::answerHeight);
        const int optionWidth = (optionRow.getWidth() - 36) / 4;

        for (auto& b : optionButtons)
        {
            b.setBounds (optionRow.removeFromLeft (optionWidth));
            optionRow.removeFromLeft (12);
        }

        trainArea.removeFromTop (10);
        feedbackLabel.setBounds (trainArea.removeFromTop (36));

        // 本轮小结（隐藏时不影响其它控件）
        auto summaryArea = trainPanel.reduced (24, 16).withTrimmedTop (64);
        summaryTitle.setBounds (summaryArea.removeFromTop (32));
        summaryArea.removeFromTop (6);
        summaryDetail.setBounds (summaryArea.removeFromTop (22));
        summaryArea.removeFromTop (16);
        auto summaryButtons = summaryArea.removeFromTop (40);
        summaryAgainButton.setBounds (summaryButtons.removeFromLeft (140));
        summaryButtons.removeFromLeft (12);
        summaryHomeButton.setBounds (summaryButtons.removeFromLeft (120));

        area.removeFromTop (16);
        statsPanel = area.removeFromTop (juce::jlimit (170, 260, area.getHeight() / 2));
        cardRects.push_back (statsPanel);
        auto statsArea = statsPanel.reduced (20, 16);
        abilityBars.setBounds (statsArea.removeFromLeft (juce::jlimit (280, 420, statsArea.getWidth() * 2 / 5)));
        statsArea.removeFromLeft (20);
        historyChart.setBounds (statsArea);
        return;
    }

    if (currentPage == Page::assessment)
    {
        // P1-8：内容列 max-width 960 且水平居中（考试态与结果态共用；不影响其它页）
        const int columnWidth = juce::jmin (area.getWidth(), theme::assessmentContentWidth);
        auto pageArea = area.withSizeKeepingCentre (columnWidth, area.getHeight());

        if (assessmentActive)
        {
            // P0-1：考试卡垂直居中，高度 252。
            // 卡内：16 内边距 → 28 题号行 → 8 → 12 进度点 → 16 → 40 试听/下一题 →
            //       16 → 48 选项行 → 16 → 36 反馈行 → 16 底部内边距（合计 252）
            constexpr int kExamCardHeight = 252;
            const int cardTop = pageArea.getY()
                                + juce::jmax (theme::space24,
                                              (pageArea.getHeight() - kExamCardHeight) / 2);
            trainPanel = juce::Rectangle<int> (pageArea.getX(), cardTop, pageArea.getWidth(),
                                               kExamCardHeight);
            cardRects.push_back (trainPanel);
            examCardRect = trainPanel;   // P2-4：只有考试态这张卡上 accent@0.30 描边

            auto t = trainPanel.reduced (theme::cardPaddingX, theme::cardPaddingY);

            auto trialRow = t.removeFromTop (28);
            assessCorrectLabel.setBounds (trialRow.removeFromRight (160));
            assessTrialLabel.setBounds (trialRow);

            t.removeFromTop (theme::space8);
            progressDots = t.removeFromTop (12);
            t.removeFromTop (theme::space16);

            auto playRow = t.removeFromTop (theme::controlHeight);
            playAButton.setBounds (playRow.removeFromLeft (96));
            playRow.removeFromLeft (theme::space8);
            playBButton.setBounds (playRow.removeFromLeft (96));
            playRow.removeFromLeft (theme::space16);
            nextButton.setBounds (playRow.removeFromLeft (120));
            playRow.removeFromLeft (theme::space16);
            // U-P1-1：测评卡内的中止入口（不改变卡高 252）
            abortSessionButton.setBounds (playRow.removeFromRight (110)
                                              .withSizeKeepingCentre (110, theme::compactHeight));

            t.removeFromTop (theme::space16);
            auto optionRow = t.removeFromTop (theme::answerHeight);
            const int optionWidth = (optionRow.getWidth() - theme::space12 * 3) / 4;

            for (auto& b : optionButtons)
            {
                b.setBounds (optionRow.removeFromLeft (optionWidth));
                optionRow.removeFromLeft (theme::space12);
            }

            t.removeFromTop (theme::space16);
            auto feedbackRow = t.removeFromTop (36);
            assessKeyHintLabel.setBounds (feedbackRow.removeFromRight (320));
            feedbackLabel.setBounds (feedbackRow);
            return;
        }

        // ---- 结果态：说明两行 → CTA → 成绩卡 → 插件卡 → 历史卡；纵向间距统一 16
        assessIntroLabel.setBounds (pageArea.removeFromTop (20));
        assessIntroLabel2.setBounds (pageArea.removeFromTop (20));
        pageArea.removeFromTop (theme::space16);

        auto ctaRow = pageArea.removeFromTop (theme::controlHeight);
        assessStartButton.setBounds (ctaRow.removeFromLeft (160));
        ctaRow.removeFromLeft (theme::space12);
        assessCtaMetaLabel.setBounds (ctaRow);
        pageArea.removeFromTop (theme::space16);

        const bool hasResult = ! assessmentStore.getResults().empty();

        // P1-7：无记录时成绩卡不显示、也不占位
        if (hasResult)
        {
            auto cardArea = pageArea.removeFromTop (176);
            cardRects.push_back (cardArea);
            assessCard.setBounds (cardArea);
            pageArea.removeFromTop (theme::space16);
        }
        else
        {
            assessCard.setBounds (juce::Rectangle<int>());
        }

        // P1-5：插件卡 96 = 16 顶 + 24 标题行 + 8 + 20 描述 + 12 + 16 底
        auto pluginCard = pageArea.removeFromTop (96);
        cardRects.push_back (pluginCard);
        auto pluginArea = pluginCard.reduced (theme::cardPaddingX, theme::cardPaddingY);
        auto pluginTitleRow = pluginArea.removeFromTop (24);
        assessPluginButton.setBounds (pluginTitleRow.removeFromRight (120)
                                          .withSizeKeepingCentre (120, theme::compactHeight));
        pluginTitleRow.removeFromRight (theme::space16);
        assessPluginTitle.setBounds (pluginTitleRow);
        pluginArea.removeFromTop (theme::space8);
        assessPluginLabel.setBounds (pluginArea.removeFromTop (20));

        pageArea.removeFromTop (theme::space16);

        if (hasResult)
        {
            // P1-4：历史卡拉伸到内容底，最小 248
            constexpr int kMinHistoryHeight = 248;
            auto historyArea = pageArea.withHeight (
                juce::jmax (kMinHistoryHeight, pageArea.getHeight()));
            cardRects.push_back (historyArea);
            assessHistory.setBounds (historyArea);
        }
        else
        {
            // P1-7：空态历史卡固定 120 高
            auto historyArea = pageArea.removeFromTop (120);
            cardRects.push_back (historyArea);
            assessHistory.setBounds (historyArea);
        }
        return;
    }

    if (currentPage == Page::settings)
    {
        auto audioCard = area.removeFromTop (132);
        cardRects.push_back (audioCard);
        auto a = audioCard.reduced (20, 16);
        settingsAudioTitle.setBounds (a.removeFromTop (22));
        a.removeFromTop (8);
        auto audioRow = a.removeFromTop (32);
        settingsButton.setBounds (audioRow.removeFromRight (120));
        audioRow.removeFromRight (12);
        settingsAudioLabel.setBounds (audioRow);

        area.removeFromTop (16);

        auto dataCard = area.removeFromTop (172);
        cardRects.push_back (dataCard);
        auto d = dataCard.reduced (20, 16);
        settingsDataTitle.setBounds (d.removeFromTop (22));
        d.removeFromTop (8);
        settingsDataLabel.setBounds (d.removeFromTop (56));
        auto dataRow = d.removeFromTop (32);
        openDataButton.setBounds (dataRow.removeFromLeft (120));
        dataRow.removeFromLeft (12);
        exportButton.setBounds (dataRow.removeFromLeft (120));

        area.removeFromTop (16);

        auto aboutCard = area.removeFromTop (96);
        cardRects.push_back (aboutCard);
        auto ab = aboutCard.reduced (20, 16);
        settingsAboutTitle.setBounds (ab.removeFromTop (22));
        ab.removeFromTop (8);
        settingsAboutLabel.setBounds (ab.removeFromTop (22));
    }
}

} // namespace earlab
