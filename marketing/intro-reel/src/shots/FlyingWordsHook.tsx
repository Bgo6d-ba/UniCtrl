// flying-words (video-shotcraft), portrait: desk-clutter words fly through a tunnel behind the spoken hook.
import React from 'react';
import {AbsoluteFill, useCurrentFrame} from 'remotion';
import {C, CX, CY, EASE, SANS, alpha, mix, ramp, rand} from '../theme';
import {SHOTS, wordAt} from '../timeline';
import {Stage} from './Stage';

const PERIOD = 180; // the tunnel runs 2 cycles per 6 s, as tuned in the demo
const CYCLES = 2;
const WORDS = ['Keyboard', 'Mouse', 'Trackpad', 'Cables', 'KVM switch', 'Mac mini', 'MacBook', 'Dock', 'Tabs',
  'Clipboard', 'Bluetooth', 'USB hub', 'Re-pairing', 'Monitors', 'Copy', 'Paste', 'Desk', 'Focus', 'Two mice',
  'Dongles', 'Context', 'Workflow'];
const N = WORDS.length;
const ACCENT = new Set([0, 1, 9, 3]);
const INKS = ['#f1f5f9', C.ink, C.ink2];

const P = 4400;
const Z0 = -7000;
const Z1 = 3200;

const ITEMS = WORDS.map((text, i) => {
  const a = i * 2.39996 + rand(i * 7 + 1) * 0.8;
  const r = 300 + rand(i * 13 + 2) * 560;
  const accent = ACCENT.has(i);
  return {
    text, accent, light: rand(i + 40) > 0.7,
    size: 62 + rand(i + 3) * 50,
    color: accent ? C.emeraldLight : INKS[Math.floor(rand(i + 5) * 3)],
    // portrait: the tunnel is a tall ellipse
    x: Math.cos(a) * r * 0.62, y: Math.sin(a) * r * 1.15, rz: (rand(i + 21) - 0.5) * 12, ph: i / N,
  };
});
const DUST = Array.from({length: 56}, (_, i) => {
  const a = i * 2.39996 + rand(i * 5 + 77) * 1.2;
  const r = 180 + rand(i * 3 + 91) * 1500;
  return {x: Math.cos(a) * r * 0.6, y: Math.sin(a) * r * 1.1, ph: rand(i * 9 + 13), w: 1 + rand(i + 50) * 1.6};
});

const OP = [0, 1, 0.5, 0.2, 0];
const OT = [0, 0.25, 0.6, 0.85, 1];
const curve = (u: number) => {
  for (let k = 0; k < 4; k++) if (u <= OT[k + 1]) return OP[k] + ((OP[k + 1] - OP[k]) * (u - OT[k])) / (OT[k + 1] - OT[k]);
  return 0;
};
const project = (x: number, y: number, u: number, roll: number, drift = true) => {
  const z = Z0 + (Z1 - Z0) * u;
  const d = drift ? 0.5 + u * 1.35 : 1;
  const s = P / (P - z);
  const c = Math.cos(roll), sn = Math.sin(roll);
  return {sx: CX + (x * c - y * sn) * d * s, sy: CY + (x * sn + y * c) * d * s, s};
};

// the spoken hook, one stacked line per beat of the read
const HOOK = [
  {text: '2 computers.', at: -8}, // on screen from frame 0, so the cover frame reads
  {text: '2 keyboards.', at: wordAt(0, 2)},
  {text: '2 mice.', at: wordAt(0, 4)},
  {text: 'On one desk?', at: wordAt(0, 6), accent: true},
];

export const FlyingWordsHook: React.FC = () => {
  const f = useCurrentFrame();
  // start mid-tunnel so the first frame already has words in flight
  const t = (f + 40) / PERIOD;
  const roll = ((2.5 * Math.PI) / 180) * Math.sin(t * Math.PI * 2);
  const du = CYCLES / PERIOD;
  const out = ramp(f, SHOTS.hook.to - 8, 8, EASE.swift);

  return (
    <AbsoluteFill style={{opacity: 1 - out * 0.6}}>
      <Stage light={{x: 0.5, y: 0.5}} glow={0.2 + 0.05 * Math.sin(t * Math.PI * 4)} />
      <svg width={1080} height={1920} style={{position: 'absolute', inset: 0}}>
        {Array.from({length: 10}, (_, k) => {
          const u = (t * CYCLES + k / 10) % 1;
          const p = project(0, 0, u, roll, false);
          const rx = 700 * p.s;
          const op = Math.min(1, u / 0.3) * (1 - Math.min(1, Math.max(0, (u - 0.55) / 0.3)));
          return op > 0.01 ? (
            <ellipse key={k} cx={CX} cy={CY} rx={rx} ry={rx * 1.75} fill="none" stroke={C.emerald}
              strokeOpacity={op * 0.16} strokeWidth={1 + p.s * 0.8} transform={`rotate(${(roll * 180) / Math.PI} ${CX} ${CY})`} />
          ) : null;
        })}
        {DUST.map((d, i) => {
          const u = (t * CYCLES + d.ph) % 1;
          const a = project(d.x, d.y, Math.max(0, u - du * 2.6), roll);
          const b = project(d.x, d.y, u, roll);
          const op = curve(u) * 0.75;
          return op > 0.01 ? (
            <line key={i} x1={a.sx} y1={a.sy} x2={b.sx} y2={b.sy} stroke="#d1fae5" strokeOpacity={op}
              strokeWidth={d.w * Math.min(2.4, b.s)} strokeLinecap="round" />
          ) : null;
        })}
      </svg>

      {ITEMS.map((it, i) => {
        const u = (t * CYCLES + it.ph) % 1;
        const p = project(it.x, it.y, u, roll);
        const q = project(it.x, it.y, Math.max(0, u - du), roll);
        const vx = p.sx - q.sx, vy = p.sy - q.sy;
        const k = ((p.sx - CX) / 480) ** 2 + ((p.sy - CY) / 420) ** 2;
        const occl = k < 1 ? 0.2 + 0.8 * k * k : 1;
        const op = curve(u) * occl;
        if (op <= 0.005) return null;
        const blur = (u < 0.22 ? (1 - u / 0.22) * 2.2 : 0) + (u > 0.86 ? (u - 0.86) * 60 : 0);
        const trail = Math.hypot(vx, vy) > 6
          ? [1, 2, 3, 4, 5, 6].map((j) => {
            const kk = ((j / 6) * 0.9) / p.s;
            return `${(-vx * kk).toFixed(1)}px ${(-vy * kk).toFixed(1)}px ${(j * 0.8).toFixed(1)}px ${alpha(it.color, 0.2 - j * 0.026)}`;
          }).join(', ')
          : '';
        const glow = it.accent ? `0 0 18px ${alpha(C.emerald, 0.55)}, 0 0 48px ${alpha(C.emerald, 0.3)}` : '';
        return (
          <div key={i} style={{
            position: 'absolute', left: p.sx, top: p.sy, whiteSpace: 'nowrap', lineHeight: 1,
            transform: `translate(-50%,-50%) scale(${p.s.toFixed(4)}) rotate(${it.rz + (roll * 180) / Math.PI}deg)`,
            fontFamily: SANS, fontStyle: it.light ? 'italic' : undefined, fontWeight: it.light ? 300 : 700,
            fontSize: it.size, letterSpacing: '-0.03em', color: it.color, opacity: op,
            textShadow: [glow, trail].filter(Boolean).join(', ') || undefined,
            filter: blur > 0.05 ? `blur(${(blur / Math.max(0.6, p.s)).toFixed(2)}px)` : undefined,
          }}>
            {it.text}
          </div>
        );
      })}

      <AbsoluteFill style={{transform: `scale(${mix(1, 1.6, out)})`, filter: out > 0.01 ? `blur(${(out * 14).toFixed(1)}px)` : undefined, opacity: 1 - out}}>
        <div style={{
          position: 'absolute', left: CX - 520, top: CY - 420, width: 1040, height: 840, borderRadius: '50%',
          background: `radial-gradient(ellipse 50% 50% at 50% 50%, ${alpha('#020304', 0.9)} 0%, ${alpha('#020304', 0.4)} 50%, transparent 74%)`,
        }} />
        <div style={{position: 'absolute', left: 0, right: 0, top: CY - 300, display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 6}}>
          {HOOK.map((h, i) => {
            const e = ramp(f, h.at - 1, 9, EASE.snappy);
            return (
              <div key={i} style={{
                fontFamily: SANS, fontWeight: 800, fontSize: h.accent ? 124 : 104, letterSpacing: '-0.045em', lineHeight: 1.08,
                color: h.accent ? C.emeraldLight : C.ink, marginTop: h.accent ? 26 : 0,
                opacity: Math.min(1, e * 2), transform: `translateY(${((1 - e) * 34).toFixed(1)}px) scale(${mix(1.18, 1, e).toFixed(4)})`,
                filter: e < 0.98 ? `blur(${((1 - e) * 10).toFixed(1)}px)` : undefined,
                textShadow: h.accent ? `0 0 40px ${alpha(C.emerald, 0.45)}` : `0 4px 40px ${alpha('#000', 0.6)}`,
              }}>
                {h.text}
              </div>
            );
          })}
        </div>
      </AbsoluteFill>
    </AbsoluteFill>
  );
};
