// icon-performance-moves · attention-bounce (video-shotcraft), portrait: the UniCtrl icon jumps in the Dock,
// higher each time, the camera is drawn to it, then the name and pitch open above it.
import React from 'react';
import {AbsoluteFill, spring, useCurrentFrame} from 'remotion';
import {C, CX, EASE, FPS, SANS, alpha, mix, ramp} from '../theme';
import {LINES, SHOTS, wordAt} from '../timeline';
import {Stage} from './Stage';
import {UniCtrlIcon} from './UniCtrlIcon';

const ICON = 340;
const GROUND = 1250; // icon bottom edge at rest
const DOCK_TOP = GROUND - ICON - 30;
// rising jumps, air time ∝ √height (card: 0.35 → 1.2 × icon height)
const JUMPS = [
  {start: 104, dur: 14, peak: 0.35},
  {start: 120, dur: 18, peak: 0.6},
  {start: 140, dur: 22, peak: 1.0},
];
const LAND_FINAL = 162; // lands as the tagline starts, so the name opens with it
export const LANDINGS = JUMPS.map((j) => j.start + j.dur);
export const PANEL = 164;

const body = (f: number) => {
  let y = 0, v = 0, squash = 0;
  JUMPS.forEach((j, ji) => {
    const t = (f - j.start) / j.dur;
    if (t > 0 && t < 1) {
      y = j.peak * ICON * 4 * t * (1 - t);
      v = 1 - 2 * t;
    }
    const land = j.start + j.dur;
    const q = 0.1 + 0.1 * (ji / 2);
    if (ji < JUMPS.length - 1 && f >= land && f < land + 2) squash = q * (1 - (f - land) / 2) + q * 0.4;
  });
  if (f >= 100 && f < 104) squash = 0.08 * EASE.out((f - 100) / 4);
  if (f >= LAND_FINAL) {
    const k = f - LAND_FINAL;
    squash = 0.2 * Math.exp(-k * 0.38) * Math.cos(k * 0.72);
  }
  return {y, v, squash};
};

const NEIGHBORS = [
  {x: CX - 420, bg: 'linear-gradient(180deg, #64748b 0%, #334155 100%)', glyph: 'M110 250 L110 120 L180 120 L200 140 L260 140 L260 250 Z'},
  {x: CX + 420, bg: 'linear-gradient(180deg, #3b4a63 0%, #1e293b 100%)', glyph: 'M95 130 L265 130 L265 240 L95 240 Z M100 136 L180 195 L260 136'},
];

const Neighbor: React.FC<{glyph: string; bg: string}> = ({glyph, bg}) => (
  <div style={{width: ICON, height: ICON, borderRadius: ICON * 0.25, background: bg, boxShadow: 'inset 0 2px 0 rgba(255,255,255,0.18)'}}>
    <svg width={ICON} height={ICON} viewBox="0 0 360 360">
      <path d={glyph} fill="none" stroke="#e2e8f0" strokeWidth={18} strokeLinejoin="round" strokeLinecap="round" />
    </svg>
  </div>
);

export const IconBounce: React.FC = () => {
  const f = useCurrentFrame() + SHOTS.bounce.from;
  const {y, v, squash} = body(f);
  const stretch = Math.abs(v) * 0.1 * (y > 0 ? 1 : 0);
  const sx = 1 + squash - stretch * 0.5;
  const sy = 1 - squash + stretch;
  const enter = ramp(f, 100, 10, EASE.snappy);
  const zoom = 1 + 0.015 * ramp(f, 100, 42, EASE.smooth) + 0.065 * ramp(f, 140, 18, EASE.smooth) - 0.05 * ramp(f, PANEL, 30, EASE.smooth);
  const camY = 90 * ramp(f, PANEL, 26, EASE.smooth);
  const defocus = ramp(f, 116, 46, EASE.swift);
  const panel = f >= PANEL ? spring({frame: f - PANEL, fps: FPS, config: {damping: 17, stiffness: 150}}) : 0;
  const dot = ramp(f, LAND_FINAL + 2, 8, EASE.out);
  const out = ramp(f, SHOTS.bounce.to - 8, 8, EASE.swift);
  const tagline = LINES[2].words;

  return (
    <AbsoluteFill style={{opacity: 1 - out, transform: `translateY(${(-out * 120).toFixed(1)}px)`}}>
      <Stage light={{x: 0.5, y: 0.6}} glow={0.18 + 0.12 * panel} />
      <AbsoluteFill style={{transform: `translateY(${camY.toFixed(1)}px) scale(${zoom.toFixed(5)})`, transformOrigin: `${CX}px ${GROUND - ICON / 2}px`}}>
        <div style={{
          position: 'absolute', left: -80, right: -80, top: DOCK_TOP, height: ICON + 64, borderRadius: 72,
          background: 'linear-gradient(180deg, rgba(30,41,59,0.55) 0%, rgba(15,19,24,0.75) 100%)',
          boxShadow: `inset 0 1px 0 ${alpha('#ffffff', 0.12)}, 0 30px 60px -20px rgba(0,0,0,0.8)`,
          opacity: enter, transform: `translateY(${((1 - enter) * 60).toFixed(1)}px)`,
        }} />
        {NEIGHBORS.map((n, i) => (
          <div key={i} style={{
            position: 'absolute', left: n.x - ICON / 2, top: GROUND - ICON, opacity: enter * (1 - 0.35 * defocus),
            filter: `blur(${(defocus * 7).toFixed(2)}px) saturate(${(1 - 0.3 * defocus).toFixed(2)})`,
          }}>
            <Neighbor glyph={n.glyph} bg={n.bg} />
          </div>
        ))}
        {LANDINGS.map((at, i) => {
          const r = ramp(f, at, 16, EASE.out);
          return f >= at && r < 1 ? (
            <div key={i} style={{
              position: 'absolute', left: CX - ICON * 0.4 * (1 + r), top: GROUND - 12, width: ICON * 0.8 * (1 + r), height: 24 * (1 + r),
              borderRadius: '50%', border: `2px solid ${alpha(C.emeraldLight, 0.6 * (1 - r))}`,
            }} />
          ) : null;
        })}
        <div style={{
          position: 'absolute', left: CX - ICON / 2, top: GROUND - ICON - y, width: ICON, height: ICON,
          transform: `scale(${sx.toFixed(4)}, ${sy.toFixed(4)})`, transformOrigin: '50% 100%', opacity: enter,
          filter: `drop-shadow(0 ${(20 + y * 0.08).toFixed(1)}px ${(30 + y * 0.1).toFixed(1)}px rgba(0,0,0,0.6))`,
        }}>
          <UniCtrlIcon size={ICON} />
        </div>
        <div style={{
          position: 'absolute', left: CX - 7, top: GROUND + 16, width: 14, height: 14, borderRadius: 7,
          background: C.emeraldPale, opacity: dot, boxShadow: `0 0 16px ${alpha(C.emerald, 0.9)}`,
        }} />
      </AbsoluteFill>

      <div style={{position: 'absolute', left: 0, right: 0, top: 380, textAlign: 'center', fontFamily: SANS}}>
        <div style={{
          fontSize: 168, fontWeight: 800, letterSpacing: '-0.05em', color: C.ink, lineHeight: 1,
          opacity: Math.min(1, panel * 1.6), transform: `translateY(${((1 - panel) * 60).toFixed(1)}px) scale(${mix(0.86, 1, panel).toFixed(4)})`,
        }}>
          Uni<span style={{color: C.emeraldLight}}>Ctrl</span>
        </div>
        <div style={{
          margin: '34px auto 0', maxWidth: 860, fontSize: 58, fontWeight: 600, letterSpacing: '-0.02em', lineHeight: 1.18, color: C.ink2,
          display: 'flex', flexWrap: 'wrap', justifyContent: 'center', columnGap: 16,
        }}>
          {tagline.map((w, i) => {
            const e = ramp(f, wordAt(2, i) - 2, 8, EASE.snappy);
            const key = i < 4;
            return (
              <span key={i} style={{
                opacity: e, transform: `translateY(${((1 - e) * 18).toFixed(1)}px)`, display: 'inline-block',
                color: key ? C.ink : C.ink2,
              }}>
                {w.text}
              </span>
            );
          })}
        </div>
      </div>
    </AbsoluteFill>
  );
};
