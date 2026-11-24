#pragma once

#include <juce_core/juce_core.h>

namespace earlab
{

/**
    原子写文本文件：先写 <name>.tmp，再把旧文件备份成 <name>.bak，最后 rename 覆盖。

    直接 replaceWithText 在写一半时崩溃/断电会留下半截 JSON，
    下次 load 解析失败就会把用户数据当成空档案，再保存一次就永久丢失。

    createBackup = false 用于「本次是从 .bak / 坏档恢复的」场景：此时主文件不可信，
    若仍按常规把主文件复制成 .bak，就会用损坏字节覆盖唯一一份好备份（D-P0-1）。
    跳过备份时仍然走 tmp → rename 的原子替换，.bak 保持恢复前的有效内容。
*/
inline bool writeTextAtomically (const juce::File& file, const juce::String& text,
                                 bool createBackup = true)
{
    auto dir = file.getParentDirectory();

    if (! dir.isDirectory() && ! dir.createDirectory().wasOk())
        return false;

    // Unique sibling names avoid collisions between independent saves.
    const juce::TemporaryFile temporary (file);
    const auto& tmp = temporary.getFile();

    if (! tmp.replaceWithText (text))
    {
        tmp.deleteFile();
        return false;
    }

    if (createBackup && file.existsAsFile())
    {
        const auto backup = file.getSiblingFile (file.getFileName() + ".bak");
        const juce::TemporaryFile backupTemporary (backup);
        if (! file.copyFileTo (backupTemporary.getFile())
            || ! backupTemporary.getFile().replaceFileIn (backup))
            return false;
    }

    // moveFileTo deletes its destination first in JUCE. replaceFileIn uses
    // Windows ReplaceFile, preserving the target on failure. Never copy over
    // the live file as a fallback; callers must receive a failed save instead.
    return tmp.replaceFileIn (file);
}

} // namespace earlab
