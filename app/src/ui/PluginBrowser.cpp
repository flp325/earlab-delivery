#include "PluginBrowser.h"

#include "Theme.h"

namespace earlab::plugin
{

namespace
{
const juce::Colour kBackground = theme::canvas;
const juce::Colour kPanel      = theme::surface;
const juce::Colour kTextBright = theme::text1;
const juce::Colour kTextDim    = theme::text3;

juce::String zh (const char* text)
{
    return juce::String::fromUTF8 (text);
}

juce::Font uiFont (float height)
{
    return theme::uiFont (height);
}

/** 扫描线程：进度窗口自带消息循环，跑完就直接拿到结果。
    U-P1-4：worker 只产出 ScanOutcome 快照，由消息线程在 runThread() 返回后发布，
    期间消息线程仍可安全重绘旧的 catalog 列表（无跨线程容器替换）。 */
class ScanThread final : public juce::ThreadWithProgressWindow
{
public:
    ScanThread (const PluginCatalog& catalogToUse, juce::StringArray directoriesToScan, bool ignoreBlacklist)
        : juce::ThreadWithProgressWindow (zh ("正在扫描本机 VST3 插件…"), true, true, 10000, zh ("取消")),
          catalog (catalogToUse), directories (std::move (directoriesToScan)),
          ignore (ignoreBlacklist)
    {
    }

    void run() override
    {
        outcome = catalog.scanDetached (directories,
                                        [this] (int done, int total, const juce::String& current)
                                        {
                                            if (total > 0)
                                                setProgress (static_cast<double> (done) / static_cast<double> (total));

                                            setStatusMessage (current);
                                        },
                                        20000, ignore,
                                        [this] { return threadShouldExit(); });
    }

    PluginCatalog::ScanOutcome outcome;

private:
    const PluginCatalog& catalog;
    juce::StringArray directories;
    bool ignore;
};
} // namespace

PluginBrowser::PluginBrowser (PluginCatalog& catalogToUse,
                              juce::File cacheFileToUse,
                              std::function<void (const PluginInfo&)> onChooseCallback)
    : catalog (catalogToUse),
      cacheFile (std::move (cacheFileToUse)),
      onChoose (std::move (onChooseCallback))
{
    setColour (juce::Label::textColourId, kTextBright);

    scanButton.setButtonText (zh ("扫描本机插件"));
    addDirectoryButton.setButtonText (zh ("添加目录…"));
    useButton.setButtonText (zh ("使用选中"));
    ignoreFailuresButton.setButtonText (zh ("忽略失败列表（重新尝试）"));
    ignoreFailuresButton.setColour (juce::ToggleButton::textColourId, kTextDim);

    for (auto* button : { &scanButton, &addDirectoryButton, &useButton })
    {
        button->setColour (juce::TextButton::buttonColourId, kPanel);
        button->setColour (juce::TextButton::textColourOffId, kTextBright);
        addAndMakeVisible (*button);
    }

    addAndMakeVisible (ignoreFailuresButton);

    scanButton.onClick = [this] { startScan (ignoreFailuresButton.getToggleState()); };
    addDirectoryButton.onClick = [this] { addDirectory(); };
    useButton.onClick = [this] { chooseSelected(); };

    list.setColour (juce::ListBox::backgroundColourId, kPanel);
    list.setColour (juce::ListBox::outlineColourId, theme::border);
    list.setRowHeight (28);
    addAndMakeVisible (list);

    statusLabel.setFont (uiFont (12.0f));
    statusLabel.setColour (juce::Label::textColourId, kTextDim);
    addAndMakeVisible (statusLabel);

    refreshList();

    statusLabel.setText (zh ("已载入 ") + juce::String (static_cast<int> (visibleIndices.size()))
                             + zh (" 个效果器")
                             + (catalog.getNumInstruments() > 0
                                    ? zh ("，另有 ") + juce::String (catalog.getNumInstruments())
                                          + zh (" 个音源未列出（训练用不到）")
                                    : juce::String())
                             + zh ("。点「扫描本机插件」更新列表。"),
                         juce::dontSendNotification);

    setSize (780, 520);
}

void PluginBrowser::refreshList()
{
    visibleIndices.clear();

    const auto& plugins = catalog.getPlugins();

    for (int i = 0; i < static_cast<int> (plugins.size()); ++i)
        if (plugins[static_cast<size_t> (i)].isEffect())
            visibleIndices.push_back (i);

    list.updateContent();

    if (! visibleIndices.empty())
        list.selectRow (0);
}

int PluginBrowser::getNumRows()
{
    return static_cast<int> (visibleIndices.size());
}

void PluginBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (! juce::isPositiveAndBelow (row, static_cast<int> (visibleIndices.size())))
        return;

    const auto& plugins = catalog.getPlugins();
    const int index = visibleIndices[static_cast<size_t> (row)];

    // 扫描/取消可能让目录变短，索引必须每次重新校验
    if (! juce::isPositiveAndBelow (index, static_cast<int> (plugins.size())))
        return;

    if (rowIsSelected)
        g.fillAll (theme::accentWash);

    const auto& info = plugins[static_cast<size_t> (index)];

    g.setColour (kTextBright);
    g.setFont (uiFont (13.0f));
    g.drawText (info.manufacturer + "  ·  " + info.name,
                juce::Rectangle<int> (10, 0, width - 190, height),
                juce::Justification::centredLeft, true);

    g.setColour (kTextDim);
    g.setFont (uiFont (11.0f));
    g.drawText (info.category,
                juce::Rectangle<int> (width - 180, 0, 170, height),
                juce::Justification::centredRight, true);
}

void PluginBrowser::listBoxItemDoubleClicked (int, const juce::MouseEvent&)
{
    chooseSelected();
}

void PluginBrowser::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    g.setColour (theme::border);
    g.drawRect (list.getBounds().expanded (1));
}

void PluginBrowser::resized()
{
    auto area = getLocalBounds().reduced (14);

    auto buttonRow = area.removeFromTop (32);
    scanButton.setBounds (buttonRow.removeFromLeft (120));
    buttonRow.removeFromLeft (8);
    addDirectoryButton.setBounds (buttonRow.removeFromLeft (110));
    buttonRow.removeFromLeft (8);
    useButton.setBounds (buttonRow.removeFromLeft (100));
    buttonRow.removeFromLeft (14);
    ignoreFailuresButton.setBounds (buttonRow.removeFromLeft (220));

    area.removeFromTop (10);
    statusLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (12);
    list.setBounds (area);
}

void PluginBrowser::chooseSelected()
{
    const int row = list.getSelectedRow();

    if (! juce::isPositiveAndBelow (row, static_cast<int> (visibleIndices.size())))
        return;

    const int index = visibleIndices[static_cast<size_t> (row)];

    if (! juce::isPositiveAndBelow (index, static_cast<int> (catalog.getPlugins().size())))
        return;

    const auto& info = catalog.getPlugins()[static_cast<size_t> (index)];

    if (onChoose)
        onChoose (info);

    if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
        dialog->closeButtonPressed();
}

void PluginBrowser::addDirectory()
{
    chooser = std::make_unique<juce::FileChooser> (zh ("选择 VST3 插件目录"), juce::File(), "");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        const auto dir = fc.getResult();

        if (! dir.isDirectory())
            return;

        catalog.addExtraDirectory (dir.getFullPathName());
        catalog.save (cacheFile);
        statusLabel.setText (zh ("已添加目录：") + dir.getFullPathName()
                                 + zh ("，点「扫描本机插件」生效。"),
                             juce::dontSendNotification);
    });
}

void PluginBrowser::startScan (bool ignoreBlacklist)
{
    scanButton.setEnabled (false);
    addDirectoryButton.setEnabled (false);
    useButton.setEnabled (false);
    repaint();

    ScanThread thread (catalog, catalog.getAllScanDirectories(), ignoreBlacklist);
    const bool finished = thread.runThread();

    scanButton.setEnabled (true);
    addDirectoryButton.setEnabled (true);
    useButton.setEnabled (true);

    if (! finished)
    {
        // 取消：catalog 未被扫描线程修改，旧列表仍然完整
        statusLabel.setText (zh ("扫描已取消，已扫到 ")
                                 + juce::String (static_cast<int> (thread.outcome.report.found))
                                 + zh (" 个插件。"),
                             juce::dontSendNotification);

        refreshList();
        return;
    }

    // 消息线程一次性发布结果；这里之后列表才换成新快照
    const auto report = thread.outcome.report;
    catalog.applyScanOutcome (std::move (thread.outcome));

    statusLabel.setText (zh ("找到 ") + juce::String (report.found)
                             + zh (" 个插件（效果器 ") + juce::String (catalog.getNumEffects())
                             + zh ("，音源 ") + juce::String (catalog.getNumInstruments())
                             + zh (" 未列出）")
                             + zh ("，跳过 ") + juce::String (report.skipped)
                             + zh ("，失败 ") + juce::String (report.failed)
                             + (report.cancelled ? zh ("，已取消") : juce::String()),
                         juce::dontSendNotification);

    saveAndRefresh();
}

void PluginBrowser::saveAndRefresh()
{
    catalog.save (cacheFile);
    refreshList();
}

} // namespace earlab::plugin
