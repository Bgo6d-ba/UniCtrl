// Burned-in captions for sound-off viewers: the line being read, the current word lit in emerald.
import React from 'react';
import {useCurrentFrame} from 'remotion';
import {C, EASE, FPS, SANS, alpha, ramp} from '../theme';
import {CAPTIONED, LINES, VO_AT, lineEnd} from '../timeline';

const TOP = 1340; // clear of the Reels caption and username block (bottom ~420 px)

export const Captions: React.FC = () => {
  const f = useCurrentFrame();
  const line = LINES.find((l) => CAPTIONED.has(l.line) && f >= VO_AT[l.line] - 2 && f < lineEnd(l.line) + 6);
  if (!line) return null;
  const at = VO_AT[line.line];
  const e = ramp(f, at - 2, 6, EASE.snappy);
  return (
    <div style={{position: 'absolute', left: 70, right: 70, top: TOP, display: 'flex', justifyContent: 'center'}}>
      <div style={{
        display: 'flex', flexWrap: 'wrap', justifyContent: 'center', columnGap: 14, rowGap: 4, padding: '14px 26px', borderRadius: 22,
        background: alpha('#020304', 0.62), fontFamily: SANS, fontSize: 46, fontWeight: 800, letterSpacing: '-0.02em', lineHeight: 1.15,
        opacity: e, transform: `translateY(${((1 - e) * 14).toFixed(1)}px)`,
      }}>
        {line.words.map((w, i) => {
          const s = at + w.start * FPS;
          const said = f >= s;
          const now = said && f < at + w.end * FPS + 2;
          return (
            <span key={i} style={{color: now ? C.emeraldLight : said ? C.ink : alpha(C.ink, 0.45)}}>
              {w.text}
            </span>
          );
        })}
      </div>
    </div>
  );
};
