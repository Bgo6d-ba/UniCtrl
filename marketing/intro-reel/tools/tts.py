# /// script
# requires-python = ">=3.10,<3.14"
# dependencies = ["kokoro-onnx==0.4.9", "soundfile", "numpy"]
# ///
"""Voice the reel script with Kokoro (local, Apache-2.0), one file per line in public/audio/vo/.

Model files go to ~/.cache/kokoro (not the repo):
  https://github.com/thewh1teagle/kokoro-onnx/releases/tag/model-files-v1.0

Run: uv run tools/tts.py
"""

import json
from pathlib import Path

import numpy as np
import soundfile as sf
from kokoro_onnx import Kokoro

ROOT = Path(__file__).resolve().parent.parent
CACHE = Path.home() / ".cache" / "kokoro"
VOICE = "af_heart"
SPEED = 1.15

# (spoken, caption): Kokoro spells "UniCtrl" out letter by letter, so it hears "Uni Control"
LINES = [
    ("Two computers. Two keyboards. Two mice. On one desk?", "Two computers. Two keyboards. Two mice. On one desk?"),
    ("Meet Uni Control.", "Meet UniCtrl."),
    ("One keyboard and mouse for all your computers.", "One keyboard and mouse for all your computers."),
    ("Just slide your cursor to the next screen.", "Just slide your cursor to the next screen."),
    ("Copy on one, paste on the other.", "Copy on one, paste on the other."),
    ("Pair them with a six-digit code.", "Pair them with a six-digit code."),
    ("Even three-finger swipes follow you.", "Even three-finger swipes follow you."),
    ("And it's completely free.", "And it's completely free."),
    ("Uni Control. Get it on GitHub.", "UniCtrl. Get it on GitHub."),
]


def main() -> None:
    kokoro = Kokoro(str(CACHE / "kokoro-v1.0.onnx"), str(CACHE / "voices-v1.0.bin"))
    out = ROOT / "public" / "audio" / "vo"
    out.mkdir(parents=True, exist_ok=True)
    cues = []
    for i, (spoken, caption) in enumerate(LINES):
        samples, rate = kokoro.create(spoken, voice=VOICE, speed=SPEED, lang="en-us")
        samples = trim_silence(samples)
        name = f"line-{i:02d}.wav"
        sf.write(out / name, samples, rate)
        cues.append({"line": i, "file": f"audio/vo/{name}", "caption": caption, "duration": round(len(samples) / rate, 3)})
    (ROOT / "src" / "data").mkdir(parents=True, exist_ok=True)
    (ROOT / "src" / "data" / "lines.json").write_text(json.dumps(cues, indent=2) + "\n")
    print(f"{len(LINES)} lines, {sum(c['duration'] for c in cues):.2f}s of speech")


def trim_silence(samples: np.ndarray, threshold: float = 0.01, pad: int = 600) -> np.ndarray:
    loud = np.where(np.abs(samples) > threshold)[0]
    if len(loud) == 0:
        return samples
    return samples[max(0, loud[0] - pad) : loud[-1] + pad]


if __name__ == "__main__":
    main()
