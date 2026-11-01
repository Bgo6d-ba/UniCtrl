"""Add per-word timings to src/data/lines.json from Whisper word timestamps.

  whisper public/audio/vo/line-*.wav --model base.en --word_timestamps True \
    --output_format json --output_dir <dir> --fp16 False
  python3 tools/align.py <dir>

Caption words are matched to Whisper words by index when the counts agree ("six-digit" may come back as
"6-digit"); otherwise the line's spoken span is shared out by character length.
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def align(caption: str, heard: list[dict]) -> list[dict]:
    words = caption.split()
    if len(words) == len(heard):
        return [{"text": w, "start": h["start"], "end": h["end"]} for w, h in zip(words, heard)]
    start, end = heard[0]["start"], heard[-1]["end"]
    total = sum(len(w) for w in words)
    out, t = [], start
    for w in words:
        d = (end - start) * len(w) / total
        out.append({"text": w, "start": round(t, 3), "end": round(t + d, 3)})
        t += d
    return out


def main(whisper_dir: Path) -> None:
    path = ROOT / "src" / "data" / "lines.json"
    lines = json.loads(path.read_text())
    for line in lines:
        result = json.loads((whisper_dir / f"line-{line['line']:02d}.json").read_text())
        heard = [w for seg in result["segments"] for w in seg["words"]]
        line["words"] = align(line["caption"], heard)
    path.write_text(json.dumps(lines, indent=2) + "\n")


if __name__ == "__main__":
    assert align("a bb", [{"start": 0, "end": 1}, {"start": 1, "end": 2}])[1]["start"] == 1
    assert align("aa bb", [{"start": 0, "end": 3}])[1]["start"] == 1.5
    main(Path(sys.argv[1]))
