// list-reveal (video-shotcraft), portrait: one row per spoken feature, each finding its place on the word that names it,
// while the whole list drifts slowly upward; the highlight follows the line being read.
import React from 'react';
import {AbsoluteFill, useCurrentFrame} from 'remotion';
import {C, EASE, MONO, SANS, alpha, ramp} from '../theme';
import {SHOTS, VO_AT} from '../timeline';
import {Stage} from './Stage';

const ITEMS = [
  {label: 'Slide to the next screen', meta: '→ |', line: 3},
  {label: 'Copy here, paste there', meta: '⌘C ⌘V', line: 4},
  {label: 'Pair with a 6-digit code', meta: '482 913', line: 5},
  {label: 'Three-finger swipes', meta: 'Mac ↔ Mac', line: 6},
];
const ROW_W = 850;
const ROW_H = 132;
const GAP = 24;
const LIST_X = (1080 - 850) / 2;
const LIST_Y = 660;
const DRIFT = 96;
const EACH = 24;
const startOf = (i: number) => VO_AT[ITEMS[i].line] - 4;

const Icon: React.FC<{i: number; color: string}> = ({i, color}) => {
  const p = {fill: 'none', stroke: color, strokeWidth: 1.8, strokeLinecap: 'round' as const, strokeLinejoin: 'round' as const};
  return (
    <svg width={44} height={44} viewBox="0 0 24 24">
      {i === 0 && (<><rect x={2.5} y={5} width={8} height={11} rx={1.5} {...p} /><rect x={13.5} y={5} width={8} height={11} rx={1.5} {...p} /><path d="M7 10.5h10m-2.5-2.5 2.5 2.5-2.5 2.5" {...p} /></>)}
      {i === 1 && (<><rect x={6} y={4.5} width={12} height={16} rx={2} {...p} /><path d="M9.5 4.5V3.5h5v1M9 10h6M9 13.5h6M9 17h3.5" {...p} /></>)}
      {i === 2 && (<><rect x={5} y={10.5} width={14} height={10} rx={2} {...p} /><path d="M8 10.5V8a4 4 0 0 1 8 0v2.5M12 14.5v2.5" {...p} /></>)}
      {i === 3 && (<><path d="M8 13V5.5a1.5 1.5 0 0 1 3 0V12M11 11V4.5a1.5 1.5 0 0 1 3 0V12M14 11.5V6a1.5 1.5 0 0 1 3 0v8.5c0 3.5-2.4 6-6 6-2.6 0-4.2-1.2-5.6-3.4L4 14.4a1.4 1.4 0 0 1 2.3-1.6L8 15" {...p} /></>)}
    </svg>
  );
};

export const FeatureList: React.FC = () => {
  const f = useCurrentFrame() + SHOTS.list.from;
  const span = SHOTS.list.to - SHOTS.list.from;
  const drift = DRIFT / 2 - (DRIFT * (f - SHOTS.list.from)) / (span - 1);
  const active = ITEMS.reduce((a, _, i) => (f >= startOf(i) + 8 ? i : a), -1);
  const title = ramp(f, VO_AT[3] + 14, 18, EASE.snappy);
  const out = ramp(f, SHOTS.list.to - 7, 7, EASE.swift);
  // fades in over the end of the bounce shot instead of cutting through black
  const fadeIn = ramp(f, SHOTS.list.from, 10, EASE.smooth);

  return (
    <AbsoluteFill style={{opacity: fadeIn * (1 - out), transform: `scale(${(1 + out * 0.08).toFixed(4)})`}}>
      <Stage light={{x: 0.3, y: 0.15}} glow={0.16} />
      <div style={{position: 'absolute', left: 80, right: 80, top: 290, fontFamily: SANS}}>
        <div style={{fontFamily: MONO, fontSize: 32, fontWeight: 600, letterSpacing: '0.16em', color: C.emeraldLight, opacity: ramp(f, SHOTS.list.from + 4, 14, EASE.out)}}>
          UNICTRL · HOW IT WORKS
        </div>
        <div style={{
          fontSize: 96, fontWeight: 800, letterSpacing: '-0.045em', lineHeight: 1.02, color: C.ink, marginTop: 22,
          opacity: title, transform: `translateY(${((1 - title) * 24).toFixed(1)}px)`,
        }}>
          Many computers.<br /><span style={{color: C.ink2}}>One desk.</span>
        </div>
      </div>

      <div style={{position: 'absolute', left: LIST_X, top: LIST_Y, width: ROW_W, transform: `translateY(${drift.toFixed(3)}px)`}}>
        {ITEMS.map((it, i) => {
          const t0 = startOf(i);
          const lin = ramp(f, t0, EACH, EASE.linear);
          const p = EASE.softBack(lin);
          const pIcon = EASE.softBack(ramp(f, t0 + 2, EACH, EASE.linear));
          const pMeta = ramp(f, t0 + 4, EACH - 4, EASE.out);
          const on = i === active ? ramp(f, t0 + 8, 6, EASE.out) : i === active - 1 ? 1 - ramp(f, startOf(active) + 8, 6, EASE.out) : 0;
          const blur = (1 - EASE.out(lin)) * 8;
          const sheen = ramp(f, t0 + 10, 22, EASE.swift);
          return (
            <div key={i} style={{
              position: 'absolute', left: 0, top: i * (ROW_H + GAP), width: ROW_W, height: ROW_H, borderRadius: 30,
              boxSizing: 'border-box', display: 'flex', alignItems: 'center', gap: 28, padding: '0 36px 0 30px', overflow: 'hidden',
              background: 'linear-gradient(180deg, #151a21 0%, #0f1318 100%)',
              boxShadow: `inset 0 0 0 1px ${alpha('#ffffff', 0.07)}, 0 18px 36px -16px rgba(0,0,0,0.85)`,
              opacity: Math.min(1, lin * 2.2), filter: blur > 0.3 ? `blur(${blur.toFixed(2)}px)` : undefined,
              transform: `translateY(${((1 - p) * 44).toFixed(2)}px) scale(${(0.82 + 0.18 * p).toFixed(4)})`, transformOrigin: '30% 50%',
            }}>
              <div style={{
                position: 'absolute', inset: 0, opacity: on,
                background: `linear-gradient(180deg, ${C.emeraldLight} 0%, ${C.emerald} 100%)`,
                boxShadow: `inset 0 1px 0 rgba(255,255,255,0.5), 0 0 70px -6px ${alpha(C.emerald, 0.55)}`,
              }} />
              {i === active && sheen > 0 && sheen < 1 && (
                <div style={{
                  position: 'absolute', top: 0, bottom: 0, width: 160, left: -160 + sheen * (ROW_W + 320),
                  background: 'linear-gradient(90deg, transparent, rgba(255,255,255,0.45), transparent)', transform: 'skewX(-18deg)',
                }} />
              )}
              <div style={{
                position: 'relative', width: 72, height: 72, borderRadius: 20, flex: 'none', display: 'flex', alignItems: 'center', justifyContent: 'center',
                background: alpha(on > 0.5 ? '#000000' : '#ffffff', on > 0.5 ? 0.1 : 0.05),
                transform: `scale(${(0.72 + 0.28 * pIcon).toFixed(4)})`, opacity: Math.min(1, Math.max(0, pIcon) * 2),
              }}>
                <Icon i={i} color={on > 0.5 ? C.iconDark : C.emeraldLight} />
              </div>
              <div style={{position: 'relative', fontFamily: SANS, fontSize: 44, fontWeight: on > 0.5 ? 700 : 600, letterSpacing: '-0.02em', color: on > 0.5 ? C.iconDark : C.ink, whiteSpace: 'nowrap'}}>
                {it.label}
              </div>
              <div style={{
                position: 'relative', marginLeft: 'auto', fontFamily: MONO, fontSize: 30, fontWeight: 500,
                color: on > 0.5 ? alpha('#04110c', 0.6) : C.ink3, opacity: pMeta, transform: `translateX(${((1 - pMeta) * 14).toFixed(1)}px)`,
              }}>
                {it.meta}
              </div>
            </div>
          );
        })}
      </div>
    </AbsoluteFill>
  );
};
