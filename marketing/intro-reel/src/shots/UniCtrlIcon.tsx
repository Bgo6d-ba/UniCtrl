import React from 'react';
import {C} from '../theme';

// The app icon from src/apps/res/icons: emerald squircle with a dark lightning bolt.
export const BOLT = '58,8 22,56 46,56 40,92 78,42 54,42';

export const UniCtrlIcon: React.FC<{size: number; style?: React.CSSProperties}> = ({size, style}) => (
  <div style={{
    width: size, height: size, borderRadius: size * 0.25, overflow: 'hidden', position: 'relative',
    background: `linear-gradient(135deg, ${C.emeraldLight} 0%, ${C.emeraldDeep} 100%)`,
    boxShadow: 'inset 0 2px 0 rgba(255,255,255,0.35), inset 0 -8px 18px rgba(0,0,0,0.2)', ...style,
  }}>
    <svg width={size} height={size} viewBox="0 0 100 100" style={{position: 'absolute', inset: 0}}>
      <polygon points={BOLT} fill={C.iconDark} transform="translate(20 20) scale(0.6)" />
    </svg>
    <div style={{position: 'absolute', inset: 0, background: 'radial-gradient(ellipse closest-side at 35% 0%, rgba(255,255,255,0.3), transparent)'}} />
  </div>
);
