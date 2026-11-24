#pragma once

#include <juce_core/juce_core.h>

namespace earlab
{

/**
    跨进程单实例锁（D-P0-2）。

    用 juce::InterProcessLock（Windows 下是命名互斥量）按「档案目录」加锁：
    同一目录只允许一个 GUI 实例写档案，避免两个实例各自整表覆盖导致丢记录。
    无窗口模式（--vst3-scan-one / --vst3-scan-out / --vst3-preflight /
    --check-trims / --export-report）刻意不创建本锁，扫描子进程可以并行启动。

    锁名由目录绝对路径的哈希得到：不同 profile 目录互不影响，同一目录
    （含大小写差异）稳定命中同一把锁。
*/
class SingleInstanceLock
{
public:
    explicit SingleInstanceLock (const juce::File& dataDirectory)
        : lockName (makeLockName (dataDirectory))
    {
    }

    /** 尝试取得锁；timeoutMs = 0 表示不等待。 */
    bool tryAcquire (int timeoutMs = 0)
    {
        return lock.enter (timeoutMs);
    }

    void release()
    {
        lock.exit();
    }

    static juce::String makeLockName (const juce::File& dataDirectory)
    {
        auto path = dataDirectory.getFullPathName();

       #if JUCE_WINDOWS
        // Windows 路径大小写不敏感：统一小写，避免 Mail/ mail 拿到两把锁
        path = path.toLowerCase();
       #endif

        return "EarLab-instance-" + juce::String::toHexString (static_cast<juce::int64> (path.hashCode64()));
    }

private:
    juce::String lockName;
    juce::InterProcessLock lock { lockName };

    JUCE_DECLARE_NON_COPYABLE (SingleInstanceLock)
};

} // namespace earlab
