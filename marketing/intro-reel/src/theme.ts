import {Easing} from 'remotion';
import {loadFont as loadInter} from '@remotion/google-fonts/Inter';
import {loadFont as loadMono} from '@remotion/google-fonts/JetBrainsMono';

export const {fontFamily: SANS} = loadInter('normal', {weights: ['300', '400', '500', '600', '700', '800'], subsets: ['latin']});
export const {fontFamily: MONO} = loadMono('normal', {weights: ['500', '600'], subsets: ['latin']});

export const W = 1080;
export const H = 1920;
export const FPS = 30;
export const CX = W / 2;
export const CY = H / 2;

// UniCtrl GUI theme (src/lib/gui) and app icon
export const C = {
  bg: '#05070a',
  bg2: '#0b0e12',
  surface: '#0f1318',
  ink: '#e2e8f0',
  ink2: '#94a3b8',
  ink3: '#475569',
  emerald: '#10b981',
  emeraldLight: '#34d399',
  emeraldPale: '#6ee7b7',
  emeraldDeep: '#047857',
  iconDark: '#04110c',
};

export const alpha = (hex: string, a: number) => {
  const n = parseInt(hex.slice(1), 16);
  return `rgba(${(n >> 16) & 255},${(n >> 8) & 255},${n & 255},${a})`;
};

export const EASE = {
  linear: (t: number) => t,
  snappy: Easing.bezier(0.16, 1, 0.3, 1),
  out: Easing.bezier(0.22, 1, 0.36, 1),
  smooth: Easing.bezier(0.65, 0, 0.35, 1),
  swift: Easing.bezier(0.4, 0, 0.2, 1),
  softBack: Easing.bezier(0.3, 1.18, 0.5, 1),
};

const clamp01 = (t: number) => Math.min(1, Math.max(0, t));
export const ramp = (f: number, start: number, dur: number, ease: (t: number) => number = EASE.snappy) =>
  ease(clamp01((f - start) / Math.max(1e-6, dur)));
export const mix = (a: number, b: number, t: number) => a + (b - a) * t;

export const rand = (n: number) => {
  const x = Math.sin(n * 127.1 + 311.7) * 43758.5453;
  return x - Math.floor(x);
};
