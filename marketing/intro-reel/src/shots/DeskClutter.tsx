// The hook's picture: two computers, then two keyboards, then two mice land on the word that names them,
// and on "one desk?" a desk line draws under them and the whole set squeezes together.
import React from 'react';
import {Easing} from 'remotion';
import {C, EASE, alpha, mix, ramp} from '../theme';
import {wordAt} from '../timeline';

const overshoot = Easing.bezier(0.34, 1.56, 0.64, 1);
const XS = [300, 780];
const SCREEN_Y = 300; // screen top
const KEYS_Y = 640;
export const DESK_HITS = [wordAt(0, 0), wordAt(0, 2), wordAt(0, 4), wordAt(0, 6)];

const Monitor: React.FC<{i: number}> = ({i}) => (
  <g>
    <rect x={-180} y={0} width={360} height={226} rx={18} fill="#1e2833" stroke={alpha('#ffffff', 0.28)} strokeWidth={2} />
    <rect x={-166} y={14} width={332} height={198} rx={8} fill="#0b1a16" />
    <rect x={-166} y={14} width={332} height={198} rx={8} fill="url(#screenGlow)" />
    {i === 0 ? (
      <g>
        <rect x={-166} y={14} width={332} height={16} fill={alpha('#ffffff', 0.07)} />
        <rect x={-110} y={56} width={220} height={130} rx={10} fill="#22303c" stroke={alpha('#ffffff', 0.16)} />
        {['#ff5f57', '#febc2e', '#28c840'].map((c, k) => <circle key={c} cx={-94 + k * 14} cy={70} r={4.5} fill={c} />)}
      </g>
    ) : (
      <g>
        <rect x={-166} y={192} width={332} height={20} fill={alpha('#ffffff', 0.07)} />
        <rect x={-120} y={44} width={240} height={128} rx={6} fill="#22303c" stroke={alpha('#ffffff', 0.16)} />
        <rect x={-120} y={44} width={240} height={18} rx={6} fill="#2c3b4a" />
      </g>
    )}
    <rect x={-12} y={226} width={24} height={44} fill="#2a3542" />
    <rect x={-70} y={268} width={140} height={12} rx={6} fill="#2a3542" />
  </g>
);

const Keyboard: React.FC = () => (
  <g>
    <rect x={-150} y={0} width={300} height={58} rx={12} fill="#1e2833" stroke={alpha('#ffffff', 0.28)} strokeWidth={2} />
    {Array.from({length: 3}).flatMap((_, r) =>
      Array.from({length: 13}).map((__, c) => (
        <rect key={`${r}-${c}`} x={-136 + c * 21} y={9 + r * 14} width={17} height={10} rx={2.5} fill={alpha('#ffffff', 0.26)} />
      )),
    )}
  </g>
);

const Mouse: React.FC = () => (
  <g>
    <rect x={-24} y={0} width={48} height={76} rx={24} fill="#1e2833" stroke={alpha('#ffffff', 0.32)} strokeWidth={2} />
    <line x1={0} y1={4} x2={0} y2={28} stroke={alpha('#ffffff', 0.32)} strokeWidth={2} />
  </g>
);

export const DeskClutter: React.FC<{f: number}> = ({f}) => {
  const [hitPc, hitKeys, hitMice, hitDesk] = DESK_HITS;
  const desk = ramp(f, hitDesk, 12, EASE.smooth);
  // "on one desk?": the pairs crowd in, with a short jolt
  const squeeze = ramp(f, hitDesk, 10, EASE.snappy);
  const jolt = f >= hitDesk && f < hitDesk + 8 ? Math.sin((f - hitDesk) * 2.4) * 6 * (1 - (f - hitDesk) / 8) : 0;
  const tense = desk > 0 ? 0.5 + 0.5 * desk : 0;

  return (
    <svg width={1080} height={1920} style={{position: 'absolute', inset: 0, overflow: 'visible'}}>
      <defs>
        <radialGradient id="screenGlow" cx="0.5" cy="0.35" r="0.75">
          <stop offset="0" stopColor={C.emerald} stopOpacity={0.28} />
          <stop offset="1" stopColor={C.emerald} stopOpacity={0} />
        </radialGradient>
      </defs>
      {/* the picture is the hero of the hook: scaled up 1.15x, top at y 330 */}
      <g transform="translate(540 330) scale(1.15) translate(-540 -300)">
      {XS.map((x0, i) => {
        const x = x0 + (i === 0 ? 1 : -1) * 34 * squeeze + jolt * (i === 0 ? 1 : -1);
        const pc = overshoot(ramp(f, hitPc - 2 + i * 3, 14, EASE.linear));
        const kb = overshoot(ramp(f, hitKeys - 2 + i * 3, 12, EASE.linear));
        const ms = overshoot(ramp(f, hitMice - 2 + i * 3, 10, EASE.linear));
        const drift = Math.sin(f / 22 + i * 2) * 3;
        return (
          <g key={i}>
            <g transform={`translate(${x} ${SCREEN_Y - (1 - pc) * 120 + drift})`} opacity={Math.min(1, pc * 2)}>
              <Monitor i={i} />
            </g>
            <g transform={`translate(${x - 30} ${KEYS_Y + (1 - kb) * 90}) scale(${mix(0.8, 1, kb)})`} opacity={Math.min(1, kb * 2)}>
              <Keyboard />
            </g>
            <g transform={`translate(${x + 166} ${KEYS_Y - 10 + (1 - ms) * 60}) scale(${mix(0.6, 1, ms)})`} opacity={Math.min(1, ms * 2)}>
              <Mouse />
            </g>
          </g>
        );
      })}
      <line x1={540 - 470 * desk} y1={KEYS_Y + 80} x2={540 + 470 * desk} y2={KEYS_Y + 80} stroke={C.emeraldLight}
        strokeWidth={4} strokeLinecap="round" opacity={desk} style={{filter: `drop-shadow(0 0 ${(12 * tense).toFixed(1)}px ${C.emerald})`}} />
      </g>
    </svg>
  );
};
