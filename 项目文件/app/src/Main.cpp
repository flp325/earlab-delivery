#include "ui/MainComponent.h"

#include "engine/AudioEngine.h"
#include "plugin/PluginScanRunner.h"
#include "ui/EarLabLookAndFeel.h"
#include "ui/Theme.h"
#include "util/SingleInstanceLock.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#if JUCE_WINDOWS
 #include <dwmapi.h>
 #include <windows.h>
#endif

namespace
{
class EarLabWindow final : public juce::DocumentWindow
{
public:
    EarLabWindow()
        : DocumentWindow ("EarLab",
                          earlab::theme::canvas,
                          DocumentWindow::allButtons)
    {
        content = new earlab::MainComponent();

        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (true, true);
        setResizeLimits (1000, 880, 1600, 1400);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        applyDarkTitleBar();
    }

    void applyDarkTitleBar()
    {
       #if JUCE_WINDOWS
        if (auto* peer = getPeer())
        {
            if (auto hwnd = static_cast<HWND> (peer->getNativeHandle()))
            {
                const BOOL dark = TRUE;

                // Win11 / Win10 20H1+ 用 20，旧版本用 19，两个都设一次
                DwmSetWindowAttribute (hwnd, 20, &dark, sizeof (dark));
                DwmSetWindowAttribute (hwnd, 19, &dark, sizeof (dark));

                SetWindowPos (hwnd, nullptr, 0, 0, 0, 0,
                              SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            }
        }
       #endif
    }

    void openMaterial (const juce::File& file)
    {
        if (content != nullptr)
            content->openMaterial (file);
    }

    void startNoise (int which)
    {
        if (content != nullptr)
            content->startNoise (which);
    }

    void showPage (int page)
    {
        if (content != nullptr)
            content->showPage (static_cast<earlab::MainComponent::Page> (page));
    }

    void beginAssessmentForQa()
    {
        if (content != nullptr)
            content->beginAssessmentForQa();
    }

    void beginTraining (int skillId)
    {
        if (content != nullptr)
            content->beginTrainingForQa (skillId);
    }

    void setProfilePath (const juce::File& file)
    {
        if (content != nullptr)
            content->setProfilePath (file);
    }

    void loadPluginFile (const juce::File& file)
    {
        if (content != nullptr)
            content->loadPluginFile (file);
    }

    void setQaAutoPlay (bool shouldAutoPlay)
    {
        if (content != nullptr)
            content->setQaAutoPlay (shouldAutoPlay);
    }

    void setQaKind (earlab::plugin::PluginQuestionKind kind)
    {
        if (content != nullptr)
            content->setQaQuestionKind (kind);
    }

    void showPluginBrowser()
    {
        if (content != nullptr)
            content->showPluginBrowserForQa();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    earlab::MainComponent* content = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EarLabWindow)
};

earlab::EarLabLookAndFeel globalLookAndFeel;

class EarLabApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "EarLab"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }

    // 扫描子进程需要能再起一个自己
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&globalLookAndFeel);

        const auto args = juce::StringArray::fromTokens (commandLine, true);

        // ---- 扫描子进程模式：不开窗口，只读一个插件、写 JSON、退出
        {
            juce::String scanOne, scanOut;
            const auto& scanArgs = args;

            for (int i = 0; i < scanArgs.size(); ++i)
            {
                if (scanArgs[i] == "--vst3-scan-one" && i + 1 < scanArgs.size())
                    scanOne = scanArgs[i + 1].unquoted();
                else if (scanArgs[i] == "--vst3-scan-out" && i + 1 < scanArgs.size())
                    scanOut = scanArgs[i + 1].unquoted();
            }

            if (scanOne.isNotEmpty() && scanOut.isNotEmpty())
            {
                const int code = earlab::plugin::PluginScanRunner::runScanChild (scanOne, juce::File (scanOut));
                setApplicationReturnValue (code);
                quit();
                return;
            }
        }

        // ---- 等响 trim 自检：确认恢复音频回调不会把已算好的 trim 清零
        if (std::any_of (args.begin(), args.end(), [] (const auto& arg) { return arg.unquoted() == "--check-trims"; }))
        {
            earlab::AudioEngine engine;
            engine.loadNoise (earlab::MaterialPlayer::NoiseType::white);
            engine.refreshLoudnessMatch();

            const auto match = engine.getMatch();

            // 模拟设备恢复（removeAudioCallback 后再 add 会触发 audioDeviceAboutToStart）
            engine.audioDeviceAboutToStart (nullptr);

            const auto applied = engine.getAppliedTrimsDb();
            const float expectedDry = match.valid ? match.dryTrimDb : 0.0f;
            const float expectedWet = match.valid ? match.wetTrimDb : 0.0f;

            const bool ok = match.valid && std::abs (applied.first - expectedDry) < 0.05f
                         && std::abs (applied.second - expectedWet) < 0.05f;

            std::printf ("trim check: match=%.2f/%.2f applied=%.2f/%.2f %s\n",
                         expectedDry, expectedWet, applied.first, applied.second,
                         ok ? "PASS" : "FAIL");

            setApplicationReturnValue (ok ? 0 : 1);
            quit();
            return;
        }

        // ---- 加载预检子进程：真正实例化一次，崩溃/超时由父进程判定
        {
            juce::String preflightPath;

            for (int i = 0; i < args.size(); ++i)
                if (args[i] == "--vst3-preflight" && i + 1 < args.size())
                    preflightPath = args[i + 1].unquoted();

            if (preflightPath.isNotEmpty())
            {
                const int code = earlab::plugin::PluginScanRunner::runPreflightChild (preflightPath);
                setApplicationReturnValue (code);
                quit();
                return;
            }
        }

        // 用法：EarLab Engine.exe <素材路径> [--autostart] [--skill=comp] [--profile=路径]
        juce::File material;
        juce::File profileFile;
        juce::File pluginFile;
        juce::File reportFile;
        bool exportRequested = false;
        bool autostart = false;
        bool qaAuto = false;
        bool openBrowser = false;
        bool runAssessment = false;
        int pageId = -1;
        int noiseMode = 0;
        auto qaKind = earlab::plugin::PluginQuestionKind::automatic;
        int skillId = 1;

        for (const auto& rawArg : args)
        {
            // fromTokens 传了 preserveQuotedStrings=true：带空格的参数会连引号一起返回，
            // 必须先剥掉引号再匹配，否则 "--plugin=C:\Program Files\..." 永远匹配不上
            const auto arg = rawArg.unquoted();

            if (arg == "--autostart")
            {
                autostart = true;
            }
            else if (arg == "--qa-auto")
            {
                qaAuto = true;
            }
            else if (arg == "--open-browser")
            {
                openBrowser = true;
            }
            else if (arg == "--assessment")
            {
                runAssessment = true;
            }
            else if (arg.startsWith ("--page="))
            {
                const auto page = arg.substring (7).unquoted();

                if (page == "home")            pageId = 0;
                else if (page == "material")   pageId = 1;
                else if (page == "training")   pageId = 2;
                else if (page == "vst")        pageId = 3;
                else if (page == "assessment") pageId = 4;
                else if (page == "settings")   pageId = 5;
            }
            else if (arg.startsWith ("--noise="))
            {
                const auto kind = arg.substring (8).unquoted();

                if (kind == "white") noiseMode = 1;
                else if (kind == "pink") noiseMode = 2;
            }
            else if (arg.startsWith ("--qa-kind="))
            {                const auto kind = arg.substring (10);

                if (kind == "amount")
                    qaKind = earlab::plugin::PluginQuestionKind::bandAmount;
                else if (kind == "level")
                    qaKind = earlab::plugin::PluginQuestionKind::levelAmount;
                else if (kind == "direction")
                    qaKind = earlab::plugin::PluginQuestionKind::bandDirection;
            }
            else if (arg.startsWith ("--profile="))
            {
                const auto path = arg.substring (10).unquoted();
                if (path.isNotEmpty())
                    profileFile = juce::File::getCurrentWorkingDirectory().getChildFile (path);
            }
            else if (arg.startsWith ("--plugin="))
            {
                pluginFile = juce::File (arg.substring (9).unquoted());
            }
            else if (arg == "--export-report" || arg.startsWith ("--export-report="))
            {
                exportRequested = true;
                const auto path = arg.substring (16).unquoted();
                if (path.isNotEmpty())
                    reportFile = juce::File::getCurrentWorkingDirectory().getChildFile (path);
            }
            else if (arg.startsWith ("--skill="))
            {
                if (arg.endsWith ("comp"))        skillId = 2;
                else if (arg.endsWith ("plugin")) skillId = 3;
                else                              skillId = 1;
            }
            else if (! arg.startsWith ("--"))
            {
                const juce::File candidate (arg.unquoted());

                if (candidate.existsAsFile())
                    material = candidate;
            }
        }

        const auto input = profileFile == juce::File() ? earlab::ProfileStore::defaultPath() : profileFile;
        if (exportRequested)
        {
            earlab::ProfileStore profile;
            const bool loaded = profile.load (input);
            const bool firstRun = profileFile == juce::File()
                                  && profile.getLoadState() == earlab::ProfileStore::LoadState::missing;
            const bool exported = (loaded || firstRun) && reportFile != juce::File()
                                  && profile.exportReport (reportFile, input);
            if (! exported)
                std::fprintf (stderr, "EarLab: cannot export %s from %s. Check the input profile and choose a separate writable .csv or .html file.\n",
                              reportFile.getFullPathName().toRawUTF8(), input.getFullPathName().toRawUTF8());
            setApplicationReturnValue (exported ? 0 : 1);
            quit();
            return;
        }

        {
            const auto dataDir = input.getParentDirectory();
            instanceLock = std::make_unique<earlab::SingleInstanceLock> (dataDir);

            if (! instanceLock->tryAcquire (0))
            {
                instanceLock.reset();

                std::fprintf (stderr,
                              "EarLab: another instance is already using %s\n",
                              dataDir.getFullPathName().toRawUTF8());

                // 异步提示 + 自动退出：不让第二个实例停留在无窗口模态循环里
                juce::Timer::callAfterDelay (4000, []
                {
                    if (auto* app = juce::JUCEApplication::getInstance())
                        app->quit();
                });

                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    juce::String::fromUTF8 ("EarLab 已在运行"),
                    juce::String::fromUTF8 ("另一个 EarLab 窗口正在使用同一份档案，本窗口将退出。"),
                    juce::String::fromUTF8 ("知道了"), nullptr,
                    juce::ModalCallbackFunction::create ([] (int)
                    {
                        if (auto* app = juce::JUCEApplication::getInstance())
                            app->quit();
                    }));

                return;
            }
        }

        window = std::make_unique<EarLabWindow>();
        if (profileFile != juce::File())
            window->setProfilePath (profileFile);

        if (material.existsAsFile())
            window->openMaterial (material);

        if (noiseMode > 0)
            window->startNoise (noiseMode);

        if (pageId >= 0)
            window->showPage (pageId);

        // 插件加载与自动开训必须等消息循环起来：
        // 出题用模态进度窗口，在 initialise() 里调用会拿不到消息循环。
        const juce::Component::SafePointer<EarLabWindow> content (window.get());

        if (qaAuto)
            window->setQaAutoPlay (true);

        if (qaKind != earlab::plugin::PluginQuestionKind::automatic)
            window->setQaKind (qaKind);

        if (openBrowser)
            juce::MessageManager::callAsync ([content] { if (content) content->showPluginBrowser(); });

        if (pluginFile != juce::File() && pluginFile.exists())
            juce::MessageManager::callAsync ([content, pluginFile]
                                             { if (content) content->loadPluginFile (pluginFile); });

        if (autostart && (material.existsAsFile() || noiseMode > 0))
            juce::MessageManager::callAsync ([content, skillId]
                                             { if (content) content->beginTraining (skillId); });

        if (runAssessment)
            juce::MessageManager::callAsync ([content] { if (content) content->beginAssessmentForQa(); });
    }

    void shutdown() override
    {
        window.reset();
        instanceLock.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

private:
    std::unique_ptr<EarLabWindow> window;
    std::unique_ptr<earlab::SingleInstanceLock> instanceLock;
};
} // namespace

START_JUCE_APPLICATION (EarLabApplication)
