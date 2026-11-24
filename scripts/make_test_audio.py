"""生成一段 44.1 kHz 的测试素材（鼓循环近似），用于训练链路与截图验收。"""

import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts" / "test-loop-44k.wav"

SR = 44100
DURATION = 4.0


def main() -> int:
    n = int(SR * DURATION)
    x = np.zeros(n, dtype=np.float64)

    # 底鼓：每 0.5 秒，音高快速下滑
    for k in range(8):
        start = int(k * 0.5 * SR)
        length = int(0.18 * SR)
        tt = np.arange(length) / SR
        env = np.exp(-tt * 18.0)
        freq = 55.0 + 30.0 * np.exp(-tt * 30.0)
        tone = np.sin(2.0 * np.pi * np.cumsum(freq) / SR)
        x[start:start + length] += 0.55 * tone * env

    # 军鼓：反拍噪声
    for k in (1, 3, 5, 7):
        start = int(k * 0.5 * SR)
        length = int(0.12 * SR)
        tt = np.arange(length) / SR
        env = np.exp(-tt * 35.0)
        rng = np.random.default_rng(k)
        x[start:start + length] += 0.30 * rng.standard_normal(length) * env

    # 踩镲：八分音符
    rng = np.random.default_rng(7)
    for k in range(32):
        start = int(k * 0.125 * SR)
        length = int(0.03 * SR)
        tt = np.arange(length) / SR
        env = np.exp(-tt * 80.0)
        x[start:start + length] += 0.10 * rng.standard_normal(length) * env

    x = np.clip(x, -1.0, 1.0)
    stereo = np.stack([x, x], axis=1)
    pcm = (stereo * 32767.0).astype("<i2")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(OUT), "wb") as wf:
        wf.setnchannels(2)
        wf.setsampwidth(2)
        wf.setframerate(SR)
        wf.writeframes(pcm.tobytes())

    peak = float(np.max(np.abs(x)))
    print(f"wrote {OUT} ({DURATION:.1f}s, peak={peak:.2f})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
