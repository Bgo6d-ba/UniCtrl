// The "how it works" stage: two screens that act out whichever feature the list is on.
// Each demo runs from its row's start frame (t0) and cross-fades into the next one.
import React from 'react';
import {Easing} from 'remotion';
import {C, EASE, MONO, SANS, alpha, mix, ramp} from '../theme';

const overshoot = Easing.bezier(0.34, 1.56, 0.64, 1);
const L = 300; // left screen centre x
const R = 780; // right screen centre x
const TOP = 370; // screen top, leaves room for the ⌘C / ⌘V pills under the eyebrow
const SW = 360;
const SH = 226;
const INNER = {top: TOP + 14, bottom: TOP + 212, half: 166};
const MID = INNER.top + 99;

const Screen: React.FC<{x: number}> = ({x}) => (
  <g>
    <rect x={x - SW / 2} y={TOP} width={SW} height={SH} rx={18} fill="#1e2833" stroke={alpha('#ffffff', 0.28)} strokeWidth={2} />
    <rect x={x - INNER.half} y={INNER.top} width={INNER.half * 2} height={198} rx={8} fill="#0b1a16" />
    <rect x={x - 12} y={TOP + SH} width={24} height={40} fill="#2a3542" />
    <rect x={x - 70} y={TOP + SH + 38} width={140} height={12} rx={6} fill="#2a3542" />
  </g>
);

const Cursor: React.FC<{x: number; y: number; s?: number}> = ({x, y, s = 1}) => (
  <path d="M0 0 L0 30 L8 23 L14 36 L20 33 L14 21 L24 21 Z" fill="#ffffff" stroke="#000" strokeWidth={1.6} strokeLinejoin="round"
    transform={`translate(${x} ${y}) scale(${s})`} />
);

const Pill: React.FC<{x: number; y: number; text: string; p: number}> = ({x, y, text, p}) =>
  p > 0 ? (
    <g transform={`translate(${x} ${y}) scale(${overshoot(Math.min(1, p))})`} opacity={Math.min(1, p * 2)}>
      <rect x={-58} y={-24} width={116} height={48} rx={24} fill={C.emerald} />
      <text y={9} textAnchor="middle" fontFamily={MONO} fontSize={26} fontWeight={600} fill={C.iconDark}>{text}</text>
    </g>
  ) : null;

const Window: React.FC<{x: number; y: number; w: number; h: number; on?: number}> = ({x, y, w, h, on = 0}) => (
  <g>
    <rect x={x} y={y} width={w} height={h} rx={8} fill="#22303c" stroke={on > 0 ? alpha(C.emeraldLight, on) : alpha('#ffffff', 0.16)} strokeWidth={2} />
    <rect x={x} y={y} width={w} height={14} rx={6} fill="#2c3b4a" />
  </g>
);

// 1 · the cursor runs off the left screen's edge and comes in on the right one
const Slide: React.FC<{r: number}> = ({r}) => {
  const p = ramp(r, 6, 34, EASE.smooth);
  const x = mix(L - 40, R + 40, p);
  const y = MID + Math.sin(p * Math.PI) * -22;
  const inGap = x > L + INNER.half - 4 && x < R - INNER.half + 4;
  const entry = ramp(r, 22, 14, EASE.out);
  const click = r >= 44 && r < 52 ? 1 - 0.15 * Math.sin(((r - 44) / 8) * Math.PI) : 1;
  return (
    <g>
      <Window x={R - 120} y={MID - 50} w={240} h={120} on={ramp(r, 44, 6, EASE.out)} />
      <rect x={R - INNER.half} y={INNER.top} width={10} height={198} fill={C.emeraldLight} opacity={entry > 0 && entry < 1 ? 0.8 * (1 - entry) : 0} />
      <path d={`M ${L - 40} ${MID} Q ${(L + R) / 2} ${MID - 44} ${R + 40} ${MID}`} fill="none" stroke={alpha(C.emeraldLight, 0.35)}
        strokeWidth={3} strokeDasharray="4 10" strokeLinecap="round" opacity={ramp(r, 4, 10, EASE.out)} />
      {!inGap && <Cursor x={x} y={y} s={1.4 * click} />}
    </g>
  );
};

// 2 · a line is selected and copied on the left, flies over, and is pasted on the right
const CopyPaste: React.FC<{r: number}> = ({r}) => {
  const sel = ramp(r, 2, 8, EASE.out);
  const fly = ramp(r, 14, 22, EASE.smooth);
  const landed = ramp(r, 36, 8, EASE.out);
  const cx = mix(L, R, fly);
  const cy = MID - 6 - Math.sin(fly * Math.PI) * 130;
  return (
    <g>
      <Window x={L - 130} y={MID - 60} w={260} h={130} />
      <rect x={L - 112} y={MID - 26} width={180 * sel} height={22} rx={4} fill={alpha(C.emerald, 0.55)} />
      <text x={L - 106} y={MID - 9} fontFamily={SANS} fontSize={18} fontWeight={600} fill={C.ink}>Meeting at 3 pm</text>
      {[14, 40].map((dy) => <rect key={dy} x={L - 106} y={MID + dy} width={170} height={10} rx={5} fill={alpha('#ffffff', 0.14)} />)}
      <Window x={R - 130} y={MID - 60} w={260} h={130} />
      <text x={R - 106} y={MID - 9} fontFamily={SANS} fontSize={18} fontWeight={600} fill={C.ink} opacity={landed}>Meeting at 3 pm</text>
      {[14, 40].map((dy) => <rect key={dy} x={R - 106} y={MID + dy} width={170} height={10} rx={5} fill={alpha('#ffffff', 0.14)} />)}
      {fly > 0 && fly < 1 && (
        <g transform={`translate(${cx} ${cy})`}>
          <rect x={-90} y={-18} width={180} height={36} rx={10} fill={C.emerald} />
          <text y={7} textAnchor="middle" fontFamily={SANS} fontSize={18} fontWeight={700} fill={C.iconDark}>Meeting at 3 pm</text>
        </g>
      )}
      <Pill x={L} y={TOP - 34} text="⌘C" p={ramp(r, 8, 10, EASE.linear)} />
      <Pill x={R} y={TOP - 34} text="⌘V" p={ramp(r, 34, 10, EASE.linear)} />
    </g>
  );
};

// 3 · the host shows a code, the joining Mac types it, both say connected
const Pair: React.FC<{r: number}> = ({r}) => {
  const code = '482913';
  const ok = ramp(r, 32, 10, EASE.out);
  return (
    <g fontFamily={MONO}>
      <text x={R} y={MID - 50} textAnchor="middle" fontSize={20} fontWeight={600} fill={C.ink2} letterSpacing="0.2em">HOST</text>
      <text x={R} y={MID + 20} textAnchor="middle" fontSize={50} fontWeight={600} fill={C.emeraldLight} opacity={ramp(r, 2, 8, EASE.out)}>482 913</text>
      <text x={L} y={MID - 50} textAnchor="middle" fontSize={20} fontWeight={600} fill={C.ink2} letterSpacing="0.2em">JOIN</text>
      {code.split('').map((d, k) => {
        const typed = ramp(r, 8 + k * 4, 4, EASE.out);
        const x = L - 135 + k * 46 + (k > 2 ? 8 : 0);
        return (
          <g key={k}>
            <rect x={x} y={MID - 22} width={38} height={52} rx={8} fill="#22303c" stroke={typed > 0 ? alpha(C.emeraldLight, 0.7) : alpha('#ffffff', 0.18)} strokeWidth={2} />
            <text x={x + 19} y={MID + 15} textAnchor="middle" fontSize={30} fontWeight={600} fill={C.ink} opacity={typed}>{d}</text>
          </g>
        );
      })}
      {[L, R].map((x) => ok > 0 && (
        <g key={x} transform={`translate(${x} ${MID + 74}) scale(${overshoot(ok)})`}>
          <circle r={16} fill={C.emerald} />
          <path d="M -7 0 L -2 5 L 8 -6" fill="none" stroke={C.iconDark} strokeWidth={3.5} strokeLinecap="round" strokeLinejoin="round" />
          <text x={26} y={7} fontFamily={SANS} fontSize={20} fontWeight={700} fill={C.emeraldPale}>Connected</text>
        </g>
      ))}
    </g>
  );
};

// 4 · three fingers swipe up on the trackpad; the right screen opens Mission Control
const Swipe: React.FC<{r: number}> = ({r}) => {
  const fingers = ramp(r, 2, 16, EASE.smooth);
  const mc = ramp(r, 14, 18, EASE.smooth);
  const windows = [
    {x: R - 130, y: MID - 60, w: 200, h: 110, gx: R - 105, gy: MID - 10},
    {x: R - 70, y: MID - 40, w: 200, h: 110, gx: R, gy: MID - 10},
    {x: R - 100, y: MID - 20, w: 200, h: 110, gx: R + 105, gy: MID - 10},
  ];
  return (
    <g>
      {[0, 1, 2].map((k) => (
        <rect key={k} x={R - 150 + k * 104} y={INNER.top + 10} width={92} height={34} rx={6} fill="#22303c"
          stroke={alpha(C.emeraldLight, k === 1 ? 0.8 : 0.2)} strokeWidth={2} opacity={mc} transform={`translate(0 ${(1 - mc) * -20})`} />
      ))}
      {windows.map((w, k) => {
        const s = mix(1, 0.45, mc);
        const x = mix(w.x, w.gx - (w.w * s) / 2, mc);
        const y = mix(w.y, w.gy, mc);
        return (
          <g key={k} transform={`translate(${x} ${y}) scale(${s})`}>
            <Window x={0} y={0} w={w.w} h={w.h} on={mc * 0.6} />
          </g>
        );
      })}
      {/* the host's trackpad, drawn over the screen it belongs to */}
      <g transform={`translate(${L} ${MID - 40})`} opacity={ramp(r, 0, 8, EASE.out)}>
        <rect x={-95} y={-6} width={190} height={88} rx={14} fill="#1e2833" stroke={alpha('#ffffff', 0.28)} strokeWidth={2} />
        {[-36, 0, 36].map((dx) => (
          <g key={dx}>
            <line x1={dx} y1={66} x2={dx} y2={mix(66, 18, fingers)} stroke={alpha(C.emeraldLight, 0.5)} strokeWidth={6} strokeLinecap="round" />
            <circle cx={dx} cy={mix(66, 18, fingers)} r={11} fill={C.emeraldPale} />
          </g>
        ))}
      </g>
    </g>
  );
};

const DEMOS = [Slide, CopyPaste, Pair, Swipe];

export const FeatureDemo: React.FC<{f: number; starts: number[]; end: number}> = ({f, starts, end}) => {
  const linked = ramp(f, starts[2] + 32, 10, EASE.out);
  return (
    <svg width={1080} height={1920} style={{position: 'absolute', inset: 0, overflow: 'visible'}}>
      <line x1={L + SW / 2 + 8} y1={TOP + SH / 2} x2={R - SW / 2 - 8} y2={TOP + SH / 2} stroke={C.emeraldLight}
        strokeWidth={3} strokeDasharray={linked > 0 ? undefined : '6 8'} opacity={0.35 + 0.5 * linked} />
      <Screen x={L} />
      <Screen x={R} />
      {DEMOS.map((Demo, i) => {
        const next = starts[i + 1] ?? end;
        const vis = ramp(f, starts[i], 6, EASE.out) * (1 - ramp(f, next, 6, EASE.out));
        return vis > 0 ? (
          <g key={i} opacity={vis}>
            <Demo r={f - starts[i]} />
          </g>
        ) : null;
      })}
    </svg>
  );
};
