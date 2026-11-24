"""编译前的静态护栏：粗略剥离注释与字符串后，检查括号是否平衡。

这不能替代编译器，只能尽早发现手写代码里的低级括号错误。
用法：python scripts/check_cpp_balance.py
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "app"


def strip_noise(src: str) -> str:
    src = re.sub(r"//[^\n]*", "", src)
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r'"(?:\\.|[^"\\])*"', '""', src)
    src = re.sub(r"'(?:\\.|[^'\\])*'", "''", src)
    return src


def main() -> int:
    files = sorted(p for p in ROOT.rglob("*") if p.suffix in (".cpp", ".h"))
    if not files:
        print(f"没有找到源文件: {ROOT}")
        return 1

    bad = 0
    for path in files:
        src = strip_noise(path.read_text(encoding="utf-8"))
        problems = []
        for opener, closer, name in [("{", "}", "brace"), ("(", ")", "paren"), ("[", "]", "bracket")]:
            if src.count(opener) != src.count(closer):
                problems.append(f"{name} {src.count(opener)}/{src.count(closer)}")
        status = "OK" if not problems else "MISMATCH " + ", ".join(problems)
        if problems:
            bad += 1
        print(f"{path.relative_to(ROOT).as_posix():42s} {status}")

    print()
    print("GREEN: 括号平衡" if bad == 0 else f"RED: {bad} 个文件不平衡")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
