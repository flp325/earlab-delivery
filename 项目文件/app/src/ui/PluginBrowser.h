#pragma once

#include "../plugin/PluginCatalog.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace earlab::plugin
{

/**
    插件库窗口。

    扫描在本进程内起子进程完成（崩溃隔离在 PluginScanRunner 里），
    结果写缓存；失败插件进黑名单，下次默认跳过。
*/
class PluginBrowser final : public juce::Component,
                            private juce::ListBoxModel
{
public:
    PluginBrowser (PluginCatalog& catalogToUse,
                   juce::File cacheFileToUse,
                   std::function<void (const PluginInfo&)> onChooseCallback);

    ~PluginBrowser() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

private:
    void startScan (bool ignoreBlacklist);
    void refreshList();
    void chooseSelected();
    void addDirectory();
    void saveAndRefresh();

    PluginCatalog& catalog;
    juce::File cacheFile;
    std::function<void (const PluginInfo&)> onChoose;

    std::vector<int> visibleIndices;   // 只列效果器

    juce::ListBox list { "plugins", this };
    juce::TextButton scanButton, addDirectoryButton, useButton;
    juce::ToggleButton ignoreFailuresButton;

    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginBrowser)
};

} // namespace earlab::plugin
