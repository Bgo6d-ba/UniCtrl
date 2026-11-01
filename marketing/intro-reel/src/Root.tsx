import React from 'react';
import {AbsoluteFill, Composition, Sequence} from 'remotion';
import {Sound} from './Sound';
import {Captions} from './shots/Captions';
import {FeatureList} from './shots/FeatureList';
import {FlyingWordsHook} from './shots/FlyingWordsHook';
import {IconBounce} from './shots/IconBounce';
import {LineArtLogo} from './shots/LineArtLogo';
import {OutlineFree} from './shots/OutlineFree';
import {C, FPS, H, W} from './theme';
import {SHOTS, TOTAL} from './timeline';

const at = (s: {from: number; to: number}) => ({from: s.from, durationInFrames: s.to - s.from});

const IntroReel: React.FC<{music: boolean}> = ({music}) => (
  <AbsoluteFill style={{background: C.bg}}>
    <Sequence {...at(SHOTS.hook)}><FlyingWordsHook /></Sequence>
    <Sequence {...at(SHOTS.bounce)}><IconBounce /></Sequence>
    <Sequence {...at(SHOTS.list)}><FeatureList /></Sequence>
    <Sequence {...at(SHOTS.free)}><OutlineFree /></Sequence>
    <Sequence {...at(SHOTS.logo)}><LineArtLogo /></Sequence>
    <Captions />
    <Sound music={music} />
  </AbsoluteFill>
);

export const Root: React.FC = () => (
  <Composition id="IntroReel" component={IntroReel} durationInFrames={TOTAL} fps={FPS} width={W} height={H} defaultProps={{music: true}} />
);
