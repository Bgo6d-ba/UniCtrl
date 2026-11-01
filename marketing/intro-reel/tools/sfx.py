# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "soundfile"]
# ///
"""Synthesise the reel's sound effects into public/audio/sfx/ (no third-party samples, nothing to license).

Run: uv run tools/sfx.py
"""

from pathlib import Path

import numpy as np
import soundfile as sf

RATE = 48000
OUT = Path(__file__).resolve().parent.parent / "public" / "audio" / "sfx"
rng = np.random.default_rng(7)


def t(seconds: float) -> np.ndarray:
    return np.arange(int(seconds * RATE)) / RATE


def env(n: int, attack: float, decay: float) -> np.ndarray:
    x = np.arange(n) / RATE
    return np.minimum(1, x / max(attack, 1e-4)) * np.exp(-x / decay)


def lowpass(x: np.ndarray, cutoff: np.ndarray | float) -> np.ndarray:
    # one-pole filter with a per-sample cutoff, enough for sweeps
    cutoff = np.broadcast_to(cutoff, x.shape)
    a = np.exp(-2 * np.pi * cutoff / RATE)
    y = np.zeros_like(x)
    for i in range(1, len(x)):
        y[i] = (1 - a[i]) * x[i] + a[i] * y[i - 1]
    return y


def whoosh(length=0.5) -> np.ndarray:
    n = int(length * RATE)
    shape = np.sin(np.linspace(0, np.pi, n)) ** 2
    sweep = 300 + 5000 * np.sin(np.linspace(0, np.pi, n)) ** 3
    return lowpass(rng.normal(0, 1, n), sweep) * shape * 2.2


def duk(pitch: float) -> np.ndarray:
    x = t(0.22)
    f = pitch * (1 + 1.5 * np.exp(-x / 0.02))
    body = np.sin(2 * np.pi * np.cumsum(f) / RATE) * env(len(x), 0.002, 0.07)
    click = lowpass(rng.normal(0, 1, len(x)), 2500) * env(len(x), 0.0005, 0.006)
    return body + 0.4 * click


def ding(base=1320.0, length=1.2) -> np.ndarray:
    x = t(length)
    partials = [(1, 1.0), (2.01, 0.35), (2.76, 0.2), (5.4, 0.08)]
    return sum(a * np.sin(2 * np.pi * base * k * x) * env(len(x), 0.002, length / (2 + k)) for k, a in partials) * 0.6


def tick() -> np.ndarray:
    x = t(0.08)
    return (np.sin(2 * np.pi * 2200 * x) * env(len(x), 0.0005, 0.012) + 0.5 * lowpass(rng.normal(0, 1, len(x)), 4000) * env(len(x), 0.0003, 0.004)) * 0.5


def riser(length=1.5) -> np.ndarray:
    x = t(length)
    ramp = (x / length) ** 2
    tone = np.sin(2 * np.pi * np.cumsum(200 + 900 * ramp) / RATE) * 0.25
    air = lowpass(rng.normal(0, 1, len(x)), 400 + 6000 * ramp) * 0.8
    return (tone + air) * ramp * np.minimum(1, (length - x) / 0.02)


def impact() -> np.ndarray:
    x = t(1.4)
    thump = np.sin(2 * np.pi * np.cumsum(45 + 120 * np.exp(-x / 0.05)) / RATE) * env(len(x), 0.001, 0.35)
    crack = lowpass(rng.normal(0, 1, len(x)), 6000) * env(len(x), 0.0005, 0.03)
    return thump * 1.1 + crack * 0.5 + ding(1760, 1.4) * 0.35


def pen(length=2.4) -> np.ndarray:
    n = int(length * RATE)
    grain = lowpass(rng.normal(0, 1, n), 3500) - lowpass(rng.normal(0, 1, n), 900)
    wobble = 0.6 + 0.4 * np.sin(np.linspace(0, 9 * np.pi, n)) ** 2
    fade = np.minimum(1, np.minimum(np.arange(n), n - np.arange(n)) / (0.08 * RATE))
    return grain * wobble * fade * 0.5


def chime() -> np.ndarray:
    a, b = ding(1046.5, 2.0), ding(1568.0, 2.0)
    offset = int(0.06 * RATE)
    out = a.copy()
    out[offset:] += b[: len(b) - offset] * 0.8
    return out * 0.7


def write(name: str, x: np.ndarray, gain=0.5) -> None:
    peak = np.max(np.abs(x)) or 1
    sf.write(OUT / f"{name}.wav", np.stack([x, x], axis=1) / peak * gain, RATE)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    write("whoosh", whoosh())
    for i, pitch in enumerate([150, 185, 230]):
        write(f"duk-{i}", duk(pitch), 0.6)
    write("ding", ding(), 0.35)
    write("tick", tick(), 0.3)
    write("riser", riser(), 0.4)
    write("impact", impact(), 0.75)
    write("pen", pen(), 0.18)
    write("chime", chime(), 0.45)
    print("sfx written to", OUT)


if __name__ == "__main__":
    main()
