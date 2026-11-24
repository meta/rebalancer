'use client';

import type {ReactNode} from 'react';

import {Box} from '@mui/material';

import {COLOR, radius, TEXT} from '@/lib/ui-tokens';

/** A key or shortcut, as in "⌘K" or "Esc". */
export default function Kbd({children}: {children: ReactNode}) {
  return (
    <Box
      component="kbd"
      sx={{
        display: 'inline-block',
        fontFamily: 'var(--font-mono)',
        fontSize: TEXT['2xs'],
        lineHeight: 1.4,
        px: '5px',
        py: '1px',
        border: `1px solid ${COLOR.borderDefault}`,
        borderBottomWidth: 2,
        borderRadius: radius('xs'),
        color: COLOR.textTertiary,
        bgcolor: COLOR.bgSurface,
        whiteSpace: 'nowrap',
      }}>
      {children}
    </Box>
  );
}
