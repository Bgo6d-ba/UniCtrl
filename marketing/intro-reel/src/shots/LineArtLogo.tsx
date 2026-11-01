// Line Art (mg-styles-15 · 02): one constant-width emerald line draws the desk and two screens, runs on into the
// UniCtrl icon and its bolt, the camera follows the pen tip, then the line turns to fill and the wordmark comes up.
import React from 'react';
import {AbsoluteFill, useCurrentFrame} from 'remotion';
import {getLength, getPointAtLength} from '@remotion/paths';
import {C, CX, EASE, MONO, SANS, alpha, mix, ramp} from '../theme';
import {SHOTS, wordAt} from '../timeline';
import {Stage} from './Stage';

// screens: x 130–470 and 610–950, y 430–652, desk at y 700
const DESK =
  'M 60 700 L 300 700 L 300 652 L 148 652 Q 130 652 130 634 L 130 448 Q 130 430 148 430 L 452 430 Q 470 430 470 448 ' +
  'L 470 634 Q 470 652 452 652 L 300 652 L 300 700 L 780 700 L 780 652 L 628 652 Q 610 652 610 634 L 610 448 ' +
  'Q 610 430 628 430 L 932 430 Q 950 430 950 448 L 950 634 Q 950 652 932 652 L 780 652 L 780 700 L 1020 700';
// the desk line runs off right, swings back and becomes the icon squircle (x 340–740, y 880–1280)
const ICON =
  'M 1020 700 C 1070 780 960 880 650 880 L 430 880 Q 340 880 340 970 L 340 1190 Q 340 1280 430 1280 L 650 1280 ' +
  'Q 740 1280 740 1190 L 740 970 Q 740 880 650 880';
// the app icon's bolt, mapped from the 64-unit icon onto that squircle
const BOLT = 'M 650 880 L 560.5 972.5 L 468.3 1095.3 L 529.7 1095.3 L 514.4 1187.5 L 611.7 1059.5 L 550.3 1059.5 L 560.5 972.5';
const BOLT_FILL = 'M 560.5 972.5 L 468.3 1095.3 L 529.7 1095.3 L 514.4 1187.5 L 611.7 1059.5 L 550.3 1059.5 Z';

const SEGMENTS = [
  {d: DESK, from: 2, dur: 34},
  {d: ICON, from: 34, dur: 26},
  {d: BOLT, from: 58, dur: 12},
].map((s) => ({...s, length: getLength(s.d)}));
const FILL = 72;
const LINE = C.emeraldPale;
const draw = EASE.smooth; // eases into the corners, as the style asks

const tipAt = (r: number) => {
  const s = [...SEGMENTS].reverse().find((seg) => r >= seg.from) ?? SEGMENTS[0];
  return getPointAtLength(s.d, s.length * draw(Math.min(1, Math.max(0, (r - s.from) / s.dur))));
};

export const LineArtLogo: React.FC = () => {
  const f = useCurrentFrame() + SHOTS.logo.from;
  const r = f - SHOTS.logo.from;
  const tip = tipAt(r);
  const drawing = r < FILL;
  const dolly = ramp(r, 56, 24, EASE.smooth);
  const s = mix(1.32, 1, dolly) * (1 + 0.02 * ramp(r, 80, 80, EASE.swift));
  const focus = {x: mix(tip.x, CX, dolly), y: mix(tip.y, 1060, dolly)};
  const deskDim = r < FILL ? 1 - 0.65 * ramp(r, 36, 12, EASE.out) : 0.35 + 0.25 * ramp(r, FILL, 20, EASE.out);
  const fill = ramp(r, FILL, 9, EASE.out);
  const boltFill = ramp(r, FILL + 2, 6, EASE.out);
  const flash = r >= FILL ? 1 - ramp(r, FILL, 12, EASE.out) : 0;
  const cta = ramp(f, wordAt(8, 1) - 2, 12, EASE.snappy);
  const eyebrow = ramp(r, 96, 14, EASE.out);
  const name = 'UniCtrl';

  return (
    <AbsoluteFill>
      <Stage light={{x: 0.5, y: 0.5}} glow={0.08 + 0.25 * flash + 0.06 * fill} />
      <AbsoluteFill style={{transform: `translate(${(CX - focus.x * s).toFixed(2)}px, ${(900 - focus.y * s).toFixed(2)}px) scale(${s.toFixed(5)})`, transformOrigin: '0 0'}}>
        <svg width={1080} height={1920} style={{position: 'absolute', inset: 0, overflow: 'visible'}}>
          <defs>
            <radialGradient id="fillGrad" cx="0.3" cy="0.1" r="1.2">
              <stop offset="0" stopColor={C.emeraldLight} />
              <stop offset="1" stopColor={C.emeraldDeep} />
            </radialGradient>
            <clipPath id="iconClip">
              <path d="M 430 880 L 650 880 Q 740 880 740 970 L 740 1190 Q 740 1280 650 1280 L 430 1280 Q 340 1280 340 1190 L 340 970 Q 340 880 430 880 Z" />
            </clipPath>
          </defs>
          {SEGMENTS.map((seg, i) => {
            const p = draw(Math.min(1, Math.max(0, (r - seg.from) / seg.dur)));
            return p > 0 ? (
              <path key={i} d={seg.d} fill="none" stroke={LINE} strokeWidth={4} strokeLinecap="round" strokeLinejoin="round"
                strokeDasharray={seg.length} strokeDashoffset={seg.length * (1 - p)} opacity={i === 0 ? deskDim : 1} />
            ) : null;
          })}
          {/* line to fill: the squircle floods from the bolt outward, then the bolt sets dark */}
          {fill > 0 && (
            <g clipPath="url(#iconClip)">
              <circle cx={540} cy={1080} r={300 * fill} fill="url(#fillGrad)" />
            </g>
          )}
          {boltFill > 0 && <path d={BOLT_FILL} fill={C.iconDark} opacity={boltFill} />}
          {flash > 0 && (
            <rect x={340} y={880} width={400} height={400} rx={90} fill="none" stroke="#ffffff" strokeWidth={4 + 10 * flash}
              opacity={flash * 0.8} style={{filter: `drop-shadow(0 0 ${(30 * flash).toFixed(1)}px ${C.emerald})`}} />
          )}
          {drawing && r >= 2 && (
            <g>
              <circle cx={tip.x} cy={tip.y} r={18} fill={alpha(C.emeraldLight, 0.25)} />
              <circle cx={tip.x} cy={tip.y} r={6} fill="#ecfdf5" style={{filter: `drop-shadow(0 0 10px ${C.emeraldLight})`}} />
            </g>
          )}
        </svg>
      </AbsoluteFill>

      <div style={{position: 'absolute', left: 0, right: 0, top: 1170, textAlign: 'center', fontFamily: SANS}}>
        <div style={{display: 'flex', justifyContent: 'center', fontSize: 104, fontWeight: 300, letterSpacing: '0.1em', color: C.ink}}>
          {name.split('').map((ch, i) => {
            const e = ramp(r, 78 + i * 2, 14, EASE.snappy);
            return (
              <span key={i} style={{display: 'inline-block', opacity: e, transform: `translateY(${((1 - e) * 26).toFixed(1)}px)`, color: i >= 3 ? C.emeraldLight : C.ink}}>
                {ch}
              </span>
            );
          })}
        </div>
        <div style={{
          display: 'inline-block', marginTop: 34, padding: '16px 34px', borderRadius: 999, border: `2px solid ${alpha(C.emeraldLight, 0.7)}`,
          fontFamily: MONO, fontSize: 32, fontWeight: 600, color: C.emeraldPale, background: alpha(C.emerald, 0.1),
          opacity: cta, transform: `translateY(${((1 - cta) * 20).toFixed(1)}px) scale(${mix(0.9, 1, cta).toFixed(4)})`,
        }}>
          github.com/Bgo6d-ba/UniCtrl
        </div>
        <div style={{marginTop: 26, fontFamily: MONO, fontSize: 32, fontWeight: 500, letterSpacing: '0.16em', color: C.ink2, opacity: eyebrow}}>
          FREE · OPEN SOURCE · macOS
        </div>
      </div>
    </AbsoluteFill>
  );
};
