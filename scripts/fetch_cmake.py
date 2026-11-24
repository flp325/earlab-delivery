"""下载 CMake 便携版到 earlab/tools/cmake（沙箱内无法用 venv / pip 安装）。

用法：python scripts/fetch_cmake.py
"""

import json
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
DEST = TOOLS / "cmake"
API = "https://api.github.com/repos/Kitware/CMake/releases/latest"


def main() -> int:
    if (DEST / "bin" / "cmake.exe").exists():
        print(f"CMake 已存在: {DEST}")
        return 0

    TOOLS.mkdir(parents=True, exist_ok=True)
    print("查询最新 CMake 版本…")
    release = json.load(urllib.request.urlopen(API, timeout=30))
    asset = next((a for a in release["assets"]
                  if a["name"].endswith("windows-x86_64.zip")), None)
    if asset is None:
        print("找不到 windows-x86_64.zip 资产")
        return 1

    zip_path = TOOLS / asset["name"]
    print(f"下载 {asset['name']} ({asset['size'] / 1e6:.0f} MB)")
    with urllib.request.urlopen(asset["browser_download_url"], timeout=120) as resp, \
         open(zip_path, "wb") as fh:
        shutil.copyfileobj(resp, fh, 1 << 20)
    print(f"下载完成 {zip_path.stat().st_size / 1e6:.1f} MB")

    tmp = TOOLS / "_cmake_tmp"
    if tmp.exists():
        shutil.rmtree(tmp)
    with zipfile.ZipFile(zip_path) as zf:
        zf.extractall(tmp)

    inner = next(tmp.glob("cmake-*"))
    if DEST.exists():
        shutil.rmtree(DEST)
    inner.rename(DEST)
    shutil.rmtree(tmp)
    zip_path.unlink(missing_ok=True)

    ok = (DEST / "bin" / "cmake.exe").exists()
    print(f"完成: {DEST}  cmake.exe={'OK' if ok else 'MISSING'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
