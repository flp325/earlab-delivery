#pragma once

#include "PluginHost.h"
#include "PluginScanRunner.h"
#include <atomic>
#include <optional>
#include <thread>

namespace earlab::plugin
{
/** Owns only metadata and child-process work, never a UI or live plugin instance.
    The UI polls completion; destruction cancels and joins the bounded child wait.
    Actual plugin creation stays on the message thread (required by some VST3s). */
class PluginLoadJob
{
public:
    enum class Phase { scanning, preflight, finished };
    struct Result { PluginInfo info; juce::String error; bool ok = false; };

    PluginLoadJob (juce::File executable, juce::String path,
                   std::optional<PluginInfo> knownInfo = {})
        : worker ([this, executable, path, knownInfo]
        {
            const auto cancelled = [this] { return stopping.load (std::memory_order_relaxed); };
            try
            {
                if (knownInfo)
                    result.info = *knownInfo;
                else
                {
                    auto scanned = PluginScanRunner::scanInChild (executable, path, 20000, cancelled);
                    const int index = PluginHost::chooseLoadableIndex (scanned.plugins);
                    if (index < 0)
                    {
                        result.error = scanned.error.isNotEmpty() ? scanned.error : utf8 ("未找到可加载的插件类型");
                        finish();
                        return;
                    }
                    result.info = scanned.plugins[static_cast<size_t> (index)];
                }
                if (! cancelled())
                {
                    phase.store (Phase::preflight, std::memory_order_relaxed);
                    const auto checked = PluginScanRunner::preflightInChild (
                        executable, result.info.fileOrIdentifier, 25000, cancelled);
                    result.ok = checked.ok && ! cancelled();
                    result.error = checked.error;
                }
            }
            catch (const std::exception& error)
            {
                result.error = juce::String::fromUTF8 (error.what());
            }
            catch (...)
            {
                result.error = utf8 ("插件检查失败");
            }
            finish();
        }) {}

    ~PluginLoadJob()
    {
        stopping.store (true, std::memory_order_relaxed);
        worker.join();
    }

    [[nodiscard]] Phase getPhase() const noexcept { return phase.load (std::memory_order_acquire); }
    [[nodiscard]] bool isFinished() const noexcept { return getPhase() == Phase::finished; }
    Result takeResult() { jassert (isFinished()); return std::move (result); }

private:
    void finish() noexcept { phase.store (Phase::finished, std::memory_order_release); }
    Result result;
    std::atomic<Phase> phase { Phase::scanning };
    std::atomic<bool> stopping { false };
    std::thread worker;
};
} // namespace earlab::plugin
