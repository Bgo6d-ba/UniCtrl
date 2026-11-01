import lines from './data/lines.json';
import {FPS} from './theme';

// Frame where each voice line starts. Shots are placed around these, so the read drives the cut.
export const VO_AT = [3, 112, 160, 238, 297, 353, 406, 492, 636];

export const SHOTS = {
  hook: {from: 0, to: 100},
  bounce: {from: 100, to: 242},
  list: {from: 232, to: 454},
  free: {from: 454, to: 566},
  logo: {from: 566, to: 726},
};
export const TOTAL = SHOTS.logo.to;

export type Word = {text: string; start: number; end: number};
export type Line = {line: number; file: string; caption: string; duration: number; words: Word[]};
export const LINES = lines as Line[];

// absolute frame a word of a line is spoken
export const wordAt = (line: number, word: number) => VO_AT[line] + Math.round(LINES[line].words[word].start * FPS);
export const lineEnd = (line: number) => VO_AT[line] + Math.round(LINES[line].duration * FPS);

// lines whose words are already the on-screen type do not get a caption as well
export const CAPTIONED = new Set([1, 3, 4, 5, 6]);
