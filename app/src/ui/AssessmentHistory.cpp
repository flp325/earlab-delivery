#include "AssessmentHistory.h"

#include "GradeStyle.h"
#include "TimeFormat.h"

namespace earlab
{

namespace
{
juce::String zh (const char* text)
{
    return juce::String::fromUTF8 (text);
}

// P1-4 列宽：时间 200 / 等级 140 / 总分右对齐
constexpr int kTimeColumn  = 200;
constexpr int kGradeColumn = 140;
constexpr int kEyebrowRow  = 20;
constexpr float kGradeDot  = 6.0f;
} // namespace

AssessmentHistory::AssessmentHistory()
{
    setWantsKeyboardFocus (true);
    previousButton.setButtonText ("<");
    nextButton.setButtonText (">");
    previousButton.setTooltip (zh ("上一页 · Page Up"));
    nextButton.setTooltip (zh ("下一页 · Page Down"));
    previousButton.onClick = [this] { movePage (-1); };
    nextButton.onClick = [this] { movePage (1); };
    pageLabel.setJustificationType (juce::Justification::centred);
    pageLabel.setInterceptsMouseClicks (false, false);
    pageLabel.setColour (juce::Label::textColourId, theme::text3);
    addAndMakeVisible (previousButton);
    addAndMakeVisible (nextButton);
    addAndMakeVisible (pageLabel);
}

void AssessmentHistory::setResults (const std::vector<AssessmentResult>& results)
{
    const bool changed = items.size() != results.size()
        || (! items.empty() && ! results.empty() && items.front().timestamp != results.front().timestamp);
    items = results;
    pagination.setItemCount (static_cast<int> (items.size()));
    if (changed)
        pagination.goTo (0);
    resized();
    repaint();
}

void AssessmentHistory::resized()
{
    auto area = getLocalBounds().reduced (theme::cardPaddingX, theme::cardPaddingY);
    area.removeFromTop (kEyebrowRow + theme::space8 + theme::tableHeaderHeight + 1);
    auto footer = area.removeFromBottom (theme::space32);
    pagination.setRowsPerPage (area.getHeight() / theme::rowHeight);
    nextButton.setBounds (footer.removeFromRight (32));
    pageLabel.setBounds (footer.removeFromRight (80));
    previousButton.setBounds (footer.removeFromRight (32));
    updatePagingControls();
}

void AssessmentHistory::updatePagingControls()
{
    const bool paged = ! items.empty() && pagination.pageCount() > 1;
    previousButton.setVisible (paged);
    nextButton.setVisible (paged);
    pageLabel.setVisible (paged);
    previousButton.setEnabled (pagination.currentPage() > 0);
    nextButton.setEnabled (pagination.currentPage() + 1 < pagination.pageCount());
    pageLabel.setText (juce::String (pagination.currentPage() + 1) + " / "
                          + juce::String (pagination.pageCount()), juce::dontSendNotification);
}

void AssessmentHistory::movePage (int delta)
{
    pagination.move (delta);
    updatePagingControls();
    repaint();
}

bool AssessmentHistory::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::pageUpKey) { movePage (-1); return true; }
    if (key == juce::KeyPress::pageDownKey) { movePage (1); return true; }
    if (key == juce::KeyPress::homeKey) { movePage (-pagination.pageCount()); return true; }
    if (key == juce::KeyPress::endKey) { movePage (pagination.pageCount()); return true; }
    return false;
}

void AssessmentHistory::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (std::abs (wheel.deltaY) > 0.01f)
        movePage (wheel.deltaY < 0 ? 1 : -1);
}

void AssessmentHistory::paint (juce::Graphics& g)
{
    // ---- 空态（P1-7）：卡高 120，两行居中
    if (items.empty())
    {
        auto inner = getLocalBounds().reduced (theme::cardPaddingX, theme::cardPaddingY);
        auto lines = inner.withSizeKeepingCentre (inner.getWidth(), 40);

        g.setColour (theme::text2);
        g.setFont (theme::uiFont (theme::typeEyebrow));
        g.drawText (zh ("还没有测评记录"), lines.removeFromTop (20),
                    juce::Justification::centred);

        g.setColour (theme::text3);
        g.setFont (theme::uiFont (theme::typeEyebrow));
        g.drawText (zh ("完成一次测评后，这里会保存成绩与 JND"), lines,
                    juce::Justification::centred);
        return;
    }

    auto area = getLocalBounds().reduced (theme::cardPaddingX, theme::cardPaddingY);
    const auto innerLeft  = (float) area.getX();
    const auto innerRight = (float) area.getRight();

    g.setColour (theme::text3);
    g.setFont (theme::uiFont (theme::typeEyebrow));
    g.drawText (zh ("历史记录"), area.removeFromTop (kEyebrowRow),
                juce::Justification::centredLeft);

    area.removeFromTop (theme::space8);

    // ---- 表头 32
    auto header = area.removeFromTop (theme::tableHeaderHeight);

    g.setColour (theme::text3);
    g.setFont (theme::uiFont (theme::typeEyebrow));
    g.drawText (zh ("时间"), header.removeFromLeft (kTimeColumn), juce::Justification::centredLeft);
    g.drawText (zh ("等级"), header.removeFromLeft (kGradeColumn), juce::Justification::centredLeft);
    g.drawText (zh ("总分"), header, juce::Justification::centredRight);

    g.setColour (theme::border);
    g.drawHorizontalLine (area.getY(), innerLeft, innerRight);
    area.removeFromTop (1);

    // ---- 页脚（卡底 32，分隔线在其上方）：把死空间变成信息
    auto footer = area.removeFromBottom (theme::space32);

    g.setColour (theme::border);
    g.drawHorizontalLine (footer.getY(), innerLeft, innerRight);

    g.setColour (theme::text3);
    g.setFont (theme::uiFont (14.0f));
    g.drawText (zh ("共 ") + juce::String ((int) items.size()) + zh (" 次测评 · 保存在本机"),
                footer, juce::Justification::centredLeft);

    // ---- 数据行：行高 40；行间 1px border@0.6，横跨卡内全宽，最后一行不画
    const int visibleRows = juce::jmin (pagination.visibleCount(), area.getHeight() / theme::rowHeight);
    const auto timeFont  = theme::monoFont (theme::typeMeta);
    const auto gradeFont = theme::uiFont (theme::typeMeta);
    const auto totalFont = theme::monoFont (theme::typeBody, true);

    for (int i = 0; i < visibleRows; ++i)
    {
        const auto& item = items[static_cast<size_t> (pagination.firstVisible() + i)];
        auto row = area.removeFromTop (theme::rowHeight);

        g.setColour (theme::text2);
        g.setFont (timeFont);
        g.drawText (formatDateTime (item.timestamp), row.removeFromLeft (kTimeColumn),
                    juce::Justification::centredLeft);

        auto gradeColumn = row.removeFromLeft (kGradeColumn);

        g.setColour (gradeColour (item.grade));
        g.fillEllipse ((float) gradeColumn.getX(),
                       (float) gradeColumn.getCentreY() - kGradeDot * 0.5f,
                       kGradeDot, kGradeDot);

        g.setColour (theme::text2);
        g.setFont (gradeFont);
        g.drawText (item.grade, gradeColumn.withTrimmedLeft ((int) kGradeDot + theme::space8),
                    juce::Justification::centredLeft);

        g.setColour (theme::text1);
        g.setFont (totalFont);
        g.drawText (juce::String (item.score), row, juce::Justification::centredRight);

        if (i + 1 < visibleRows)
        {
            g.setColour (theme::border.withAlpha (0.6f));
            g.drawHorizontalLine (row.getBottom(), innerLeft, innerRight);
        }
    }
}

} // namespace earlab
