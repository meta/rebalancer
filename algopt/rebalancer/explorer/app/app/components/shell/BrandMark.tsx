'use client';

import {useId} from 'react';

import {Box} from '@mui/material';

import {SHADOW_SM} from '@/lib/ui-tokens';

/**
 * The product mark: the design language's tile (64 grid, radius 17, action →
 * alt gradient with a 16% top sheen) and the Explorer's glyph, a staircase of
 * white blocks with one slot at 30% white and the move that fills it.
 */
export default function BrandMark({size = 26}: {size?: number}) {
  const id = useId().replace(/:/g, '');
  const fill = `rbx-mark-fill-${id}`;
  const sheen = `rbx-mark-sheen-${id}`;
  return (
    <Box
      component="svg"
      viewBox="0 0 64 64"
      aria-hidden
      sx={{
        width: size,
        height: size,
        flexShrink: 0,
        display: 'block',
        borderRadius: `${(size * 17) / 64}px`,
        boxShadow: SHADOW_SM,
      }}>
      <defs>
        <linearGradient id={fill} x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#0866ff" />
          <stop offset="1" stopColor="#7a5af8" />
        </linearGradient>
        <linearGradient id={sheen} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0" stopColor="#fff" stopOpacity=".16" />
          <stop offset=".6" stopColor="#fff" stopOpacity="0" />
        </linearGradient>
      </defs>
      <rect width="64" height="64" rx="17" fill={`url(#${fill})`} />
      <rect width="64" height="64" rx="17" fill={`url(#${sheen})`} />
      <rect x="10.5" y="44" width="12" height="12" rx="3.2" fill="#fff" />
      <rect x="26" y="44" width="12" height="12" rx="3.2" fill="#fff" />
      <rect x="26" y="28.5" width="12" height="12" rx="3.2" fill="#fff" />
      <rect x="41.5" y="44" width="12" height="12" rx="3.2" fill="#fff" />
      <rect x="41.5" y="28.5" width="12" height="12" rx="3.2" fill="#fff" />
      <rect x="41.5" y="13" width="12" height="12" rx="3.2" fill="#fff" />
      <rect
        x="10.5"
        y="28.5"
        width="12"
        height="12"
        rx="3.2"
        fill="#fff"
        fillOpacity="0.3"
      />
      <path
        d="M38.3 18 C 34.8 6.5, 16.5 8.4, 16.5 23.9"
        fill="none"
        stroke="#fff"
        strokeWidth="3.6"
        strokeLinecap="round"
      />
      <path
        d="M11.8 18.3 L16.5 23.9 L21.2 18.3"
        fill="none"
        stroke="#fff"
        strokeWidth="3.6"
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </Box>
  );
}
