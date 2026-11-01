// outline-word-fill (video-shotcraft), portrait and emerald: a hollow "FREE" slams in, a dashed ring closes on it,
// and the word lights solid in one frame on the spoken "free". The side rulers run vertically to fit 9:16.
import React from 'react';
import {AbsoluteFill, useCurrentFrame} from 'remotion';
import {C, CX, CY, EASE, MONO, SANS, alpha, mix, ramp} from '../theme';
import {SHOTS, wordAt} from '../timeline';
import {Stage} from './Stage';
import {UniCtrlIcon} from './UniCtrlIcon';

const R = 410;
const WORD = 'FREE';
const SIZE = 230;
export const POP = wordAt(7, 3); // hard cut on "free."
const T0 = POP - 54; // the card's 54-frame build before the pop

type RGB = [number, number, number];
const lerp = (a: RGB, b: RGB, t: number) => `rgb(${a.map((v, i) => Math.round(mix(v, b[i], t))).join(',')})`;
const GREY: RGB = [120, 134, 156];
const HOT: RGB = [190, 250, 222];
const EMERALD: RGB = [16, 185, 129];

const wordScaleAt = (r: number) => mix(3.2, 1, ramp(r, 2, 11, EASE.snappy));

export const OutlineFree: React.FC = () => {
  const f = useCurrentFrame() + SHOTS.free.from;
  const r = f - T0;
  const born = ramp(r, 2, 6, EASE.out);
  const ws = wordScaleAt(r);
  const zoomBlur = Math.min(6, Math.abs(wordScaleAt(r + 0.5) - wordScaleAt(r - 0.5)) * 9);
  const cin = ramp(r, 8, 10, EASE.out);
  const shrink = ramp(r, 8, 44, EASE.out);
  const ext = ramp(r, 28, 18, EASE.out);
  const warm = ramp(r, 44, 9, EASE.swift);
  const lit = r >= 54 ? 1 : 0;
  const flash = lit * (1 - ramp(r, 54, 8, EASE.out));
  const lock = lit * (0.55 + 0.45 * flash);
  const shock = ramp(r, 54, 14, EASE.out);
  const rot = 0.22 * Math.min(r, 54) + (r > 54 ? 6 * ramp(r, 54, 14, EASE.out) : 0);
  const sub = (d: number) => ramp(r, 56 + d, 14, EASE.snappy);
  const push = 1 + 0.015 * ramp(f, SHOTS.free.from, SHOTS.free.to - SHOTS.free.from, EASE.swift);
  const out = ramp(f, SHOTS.free.to - 6, 6, EASE.swift);

  const strokeC = lerp(GREY, HOT, warm);
  const lineC = lit ? lerp([80, 120, 110], EMERALD, lock) : lerp(GREY, [90, 120, 120], warm);
  const top = 250; // below the Reels top bar
  const bottom = 440; // above the Reels caption block
  const reach = (CY - R - 28 - top) * ext;
  const reachUp = (CY - R - 28 - bottom) * ext;

  return (
    <AbsoluteFill style={{opacity: 1 - out}}>
      <Stage light={{x: 0.5, y: 0.5}} glow={0.14 + 0.35 * flash + 0.08 * lit} />
      <AbsoluteFill style={{transform: `scale(${push.toFixed(5)})`}}>
        <svg width={1080} height={1920} style={{position: 'absolute', inset: 0}}>
          {lit > 0 && shock < 1 && (
            <circle cx={CX} cy={CY} r={R + shock * 160} fill="none" stroke={alpha(C.emeraldLight, 0.55 * (1 - shock))} strokeWidth={2 + 10 * (1 - shock)} />
          )}
          <g transform={`translate(${CX} ${CY}) scale(${mix(2.8, 1, shrink).toFixed(5)})`} opacity={cin}>
            <g transform={`rotate(${rot.toFixed(3)})`}>
              <circle r={R} fill="none" stroke={lineC} strokeWidth={2} strokeDasharray="22 18" />
              {[0, 90, 180, 270].map((a) => (
                <line key={a} x1={0} y1={-R - 22} x2={0} y2={-R + 10} stroke={lineC} strokeWidth={2.5} strokeLinecap="round" transform={`rotate(${a})`} />
              ))}
            </g>
            <g transform={`rotate(${(-0.14 * r).toFixed(3)})`}>
              <circle r={R - 34} fill="none" stroke={lineC} strokeWidth={1} opacity={0.35} />
              {Array.from({length: 72}, (_, i) => (
                <line key={i} x1={0} y1={-(R - 34)} x2={0} y2={-(R - (i % 6 === 0 ? 58 : 44))} stroke={lineC}
                  strokeWidth={i % 6 === 0 ? 2 : 1.2} opacity={i % 6 === 0 ? 0.7 : 0.32} transform={`rotate(${i * 5})`} />
              ))}
            </g>
          </g>
          <line x1={CX} y1={top} x2={CX} y2={top + reach} stroke={lineC} strokeWidth={2} strokeDasharray="14 12" opacity={ext} />
          <line x1={CX} y1={1920 - bottom} x2={CX} y2={1920 - bottom - reachUp} stroke={lineC} strokeWidth={2} strokeDasharray="14 12" opacity={ext} />
          <circle cx={CX} cy={top + reach} r={5} fill={lineC} opacity={ext} />
          <circle cx={CX} cy={1920 - bottom - reachUp} r={5} fill={lineC} opacity={ext} />
        </svg>

        <div style={{position: 'absolute', left: CX + 28, top: top + 4, opacity: ext, fontFamily: MONO, fontSize: 32, fontWeight: 500, letterSpacing: '0.12em', color: C.ink3}}>
          PRICE <span style={{color: lit ? C.emeraldLight : C.ink2, textShadow: lit && flash > 0.02 ? `0 0 ${(18 * flash).toFixed(1)}px ${alpha(C.emerald, 0.8)}` : undefined}}>$0</span>
        </div>
        <div style={{position: 'absolute', right: CX + 28, bottom: bottom + 4, opacity: ext, fontFamily: MONO, fontSize: 32, fontWeight: 500, letterSpacing: '0.12em', color: C.ink3, textAlign: 'right'}}>
          LICENSE <span style={{color: lit ? C.emeraldLight : C.ink2}}>GPL-2.0</span>
        </div>

        <div style={{
          position: 'absolute', left: 0, right: 0, top: CY - 210, display: 'flex', justifyContent: 'center', alignItems: 'center', gap: 16,
          opacity: sub(0), transform: `translateY(${((1 - sub(0)) * 16).toFixed(1)}px)`, fontFamily: MONO, fontSize: 32, fontWeight: 600, letterSpacing: '0.16em', color: C.ink2,
        }}>
          <UniCtrlIcon size={40} />
          <span>unictrl</span>
        </div>
        <div style={{
          position: 'absolute', left: 0, right: 0, top: CY + 136, textAlign: 'center', opacity: sub(3),
          transform: `translateY(${((1 - sub(3)) * 16).toFixed(1)}px)`, fontFamily: SANS, fontSize: 40, fontWeight: 500, letterSpacing: '-0.01em', color: C.ink2,
        }}>
          Open source. Built on Deskflow.
        </div>

        <div style={{
          position: 'absolute', left: CX, top: CY, fontFamily: SANS, fontSize: SIZE, fontWeight: 500, letterSpacing: '0.01em', lineHeight: 1,
          opacity: born, transform: `translate(-50%, -54%) scale(${ws.toFixed(5)})`, whiteSpace: 'nowrap',
        }}>
          <div style={{
            color: 'transparent', WebkitTextStroke: `2px ${strokeC}`, opacity: 1 - lit,
            filter: zoomBlur > 0.05 ? `blur(${(zoomBlur / ws).toFixed(2)}px)` : warm > 0.01 ? `drop-shadow(0 0 ${(10 * warm).toFixed(1)}px ${alpha(C.emeraldLight, 0.5 * warm)})` : undefined,
          }}>
            {WORD}
          </div>
          <div style={{
            position: 'absolute', inset: 0, color: '#f0fdf8', opacity: lit,
            textShadow: flash > 0.01
              ? `0 0 ${(14 * flash).toFixed(1)}px rgba(255,255,255,${(0.7 * flash).toFixed(3)}), 0 0 ${(60 * flash).toFixed(1)}px ${alpha(C.emerald, 0.7 * flash)}`
              : `0 0 40px ${alpha(C.emerald, 0.22)}`,
          }}>
            {WORD}
          </div>
        </div>
      </AbsoluteFill>
    </AbsoluteFill>
  );
};
