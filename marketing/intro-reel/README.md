# UniCtrl intro reel

24-second Instagram Reel (1080×1920, 30 fps) built in code with [Remotion](https://www.remotion.dev).

| Time | Shot | Source of the motion |
|---|---|---|
| 0–3.3 s | Hook over a tunnel of desk-clutter words | video-shotcraft `flying-words` |
| 3.3–7.9 s | UniCtrl icon jumps in the Dock, name and pitch open | video-shotcraft `icon-performance-moves` (attention-bounce) |
| 7.9–15.1 s | Four features, each on the word that names it | video-shotcraft `list-reveal` |
| 15.1–18.9 s | "FREE" lights solid on the spoken "free" | video-shotcraft `outline-word-fill` |
| 18.9–24.2 s | One line draws two screens and the icon, then fills | mg-styles-15 Line Art |

## Make it

```sh
npm install
uv run tools/tts.py          # voice lines -> public/audio/vo, timings -> src/data/lines.json
whisper public/audio/vo/line-*.wav --model base.en --word_timestamps True --output_format json --output_dir /tmp/vo --fp16 False
python3 tools/align.py /tmp/vo   # per-word caption timings
uv run tools/sfx.py          # synthesised sound effects -> public/audio/sfx
npm run render               # out/unictrl-intro-reel.mp4
npm run master               # out/unictrl-intro-reel-master.mp4, -14 LUFS for Reels
```

`npm run studio` opens the timeline for tweaking. Copy, colours and frame numbers live in `src/theme.ts` and `src/timeline.ts`;
the voice script is `LINES` in `tools/tts.py` (re-run the three voice steps after changing it).

Rendering without music: `npx remotion render src/index.ts IntroReel out/no-music.mp4 --props='{"music":false}'`.

See [CREDITS.md](CREDITS.md) for the voice, music and motion sources.
