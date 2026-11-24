"""把 PowerShell 脚本转成 UTF-8 BOM + CRLF（PowerShell 5.1 需要）。"""
import sys
from pathlib import Path

for name in sys.argv[1:]:
    p = Path(name)
    text = p.read_text(encoding="utf-8-sig")
    with open(p, "w", encoding="utf-8-sig", newline="\r\n") as fh:
        fh.write(text)
    head = p.read_bytes()[:3]
    print(f"{p.name}: bom={head == b'\xef\xbb\xbf'} bytes={p.stat().st_size}")
