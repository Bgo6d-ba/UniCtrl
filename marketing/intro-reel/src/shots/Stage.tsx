import React from 'react';
import {AbsoluteFill} from 'remotion';
import {C, alpha} from '../theme';

// Dark brand ground: base colour, one soft emerald key light, vignette and static grain.
export const Stage: React.FC<{light?: {x: number; y: number}; glow?: number; children?: React.ReactNode}> = ({
  light = {x: 0.5, y: 0.45},
  glow = 0.22,
  children,
}) => (
  <AbsoluteFill style={{background: C.bg, overflow: 'hidden'}}>
    <AbsoluteFill
      style={{
        background: `radial-gradient(ellipse 80% 55% at ${light.x * 100}% ${light.y * 100}%, ${alpha(C.emerald, glow)} 0%, ${alpha(C.emerald, glow * 0.25)} 40%, transparent 72%)`,
      }}
    />
    {children}
    <AbsoluteFill
      style={{background: 'radial-gradient(ellipse 75% 65% at 50% 50%, transparent 55%, rgba(0,0,0,0.65) 100%)'}}
    />
    <AbsoluteFill
      style={{
        opacity: 0.07,
        mixBlendMode: 'overlay',
        backgroundImage:
          "url(\"data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='220' height='220'><filter id='n'><feTurbulence type='fractalNoise' baseFrequency='0.9' numOctaves='2' stitchTiles='stitch'/></filter><rect width='100%' height='100%' filter='url(%23n)'/></svg>\")",
      }}
    />
  </AbsoluteFill>
);
