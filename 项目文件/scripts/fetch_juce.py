"""下载并解压 JUCE 8.0.15 到 earlab/third_party/JUCE。

沙箱内 git 不可用（无法创建管道），因此用 HTTPS + zip 方式获取。
"""

import shutil
import sys
import time
import urllib.request
import zipfile
from pathlib import Path

VERSION = "8.0.15"
URL = f"https://codeload.github.com/juce-framework/JUCE/zip/refs/tags/{VERSION}"
ROOT = Path(__file__).resolve().parents[1]
THIRD = ROOT / "third_party"
DEST = THIRD / "JUCE"
ZIP = THIRD / f"juce-{VERSION}.zip"


def main() -> int:
    if (DEST / "modules" / "juce_core").exists():
        print(f"JUCE 已存在: {DEST}")
        return 0

    THIRD.mkdir(parents=True, exist_ok=True)
    print(f"下载 {URL}")
    t0 = time.time()
    with urllib.request.urlopen(URL, timeout=120) as resp, open(ZIP, "wb") as fh:
        total = int(resp.headers.get("Content-Length") or 0)
        done = 0
        last = 0.0
        while True:
            chunk = resp.read(1 << 20)
            if not chunk:
                break
            fh.write(chunk)
            done += len(chunk)
            now = time.time()
            if now - last > 5:
                last = now
                pct = f"{done * 100 / total:.1f}%" if total else f"{done / 1e6:.0f}MB"
                print(f"  {done / 1e6:.1f} MB ({pct})", flush=True)
    print(f"下载完成 {ZIP.stat().st_size / 1e6:.1f} MB，用时 {time.time() - t0:.0f}s")

    print("解压…")
    tmp = THIRD / "_juce_tmp"
    if tmp.exists():
        shutil.rmtree(tmp)
    with zipfile.ZipFile(ZIP) as zf:
        zf.extractall(tmp)
    inner = next(tmp.glob("JUCE-*"))
    if DEST.exists():
        shutil.rmtree(DEST)
    inner.rename(DEST)
    shutil.rmtree(tmp)
    ZIP.unlink(missing_ok=True)

    ok = (DEST / "modules" / "juce_core").exists()
    print(f"完成: {DEST}  modules={'OK' if ok else 'MISSING'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
