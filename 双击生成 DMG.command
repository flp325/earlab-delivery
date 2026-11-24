#!/bin/bash
root="$(cd "$(dirname "$0")" && pwd)" || exit 1
echo "正在检查构建工具，然后编译 EarLab。首次构建需要一些时间，请保持窗口打开。"
/bin/bash "$root/scripts/build-macos.command" --show-in-finder
status=$?
if (( status != 0 )); then
    printf '\n本次构建未完成。请查看上方提示，处理后重新双击。\n' >&2
    if [[ -t 0 ]]; then
        read -r -p "按回车键关闭此窗口……" || true
    fi
fi
exit "$status"
