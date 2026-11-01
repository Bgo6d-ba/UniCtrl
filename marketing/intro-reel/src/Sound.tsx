import React from 'react';
import {Audio, Sequence, interpolate, staticFile} from 'remotion';
import {DESK_HITS} from './shots/DeskClutter';
import {LANDINGS, PANEL} from './shots/IconBounce';
import {POP} from './shots/OutlineFree';
import {LINES, SHOTS, TOTAL, VO_AT, lineEnd} from './timeline';

const SFX: {at: number; file: string; volume?: number}[] = [
  {at: 0, file: 'whoosh'},
  ...DESK_HITS.slice(0, 3).map((at, i) => ({at: at + 6, file: `duk-${i}`, volume: 0.55})),
  {at: DESK_HITS[3] + 2, file: 'tick'},
  {at: SHOTS.bounce.from - 6, file: 'whoosh', volume: 0.7},
  ...LANDINGS.map((at, i) => ({at, file: `duk-${i}`})),
  {at: PANEL, file: 'ding'},
  {at: SHOTS.list.from - 6, file: 'whoosh', volume: 0.7},
  ...[3, 4, 5, 6].map((line) => ({at: VO_AT[line] + 4, file: 'tick'})),
  {at: POP - 45, file: 'riser', volume: 0.5},
  {at: POP + 2, file: 'impact', volume: 0.6},
  {at: SHOTS.logo.from - 6, file: 'whoosh', volume: 0.6},
  {at: SHOTS.logo.from + 2, file: 'pen'},
  {at: SHOTS.logo.from + 72, file: 'chime'},
];

// music sits under the voice: ducked while a line is read, lifted in the gaps, faded at the end
const MUSIC = 0.24;
const DUCKED = 0.13;
const musicVolume = (f: number) => {
  // ramp into and out of each line over 8 frames instead of gating
  const duck = Math.max(
    ...VO_AT.map((at, i) =>
      interpolate(f, [at - 10, at - 2, lineEnd(i) + 2, lineEnd(i) + 10], [0, 1, 1, 0], {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'}),
    ),
  );
  const base = MUSIC + (DUCKED - MUSIC) * duck;
  const fadeIn = interpolate(f, [0, 6], [0, 1], {extrapolateRight: 'clamp'});
  const fadeOut = interpolate(f, [TOTAL - 30, TOTAL], [1, 0], {extrapolateLeft: 'clamp', extrapolateRight: 'clamp'});
  return base * fadeIn * fadeOut;
};

export const Sound: React.FC<{music: boolean}> = ({music}) => (
  <>
    {music && <Audio src={staticFile('audio/music.mp3')} volume={musicVolume} />}
    {LINES.map((l) => (
      <Sequence key={l.line} from={VO_AT[l.line]}>
        <Audio src={staticFile(l.file)} volume={1} />
      </Sequence>
    ))}
    {SFX.map((s, i) => (
      <Sequence key={i} from={Math.max(0, s.at)}>
        <Audio src={staticFile(`audio/sfx/${s.file}.wav`)} volume={s.volume ?? 1} />
      </Sequence>
    ))}
  </>
);
