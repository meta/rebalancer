'use client';

import type {ReactNode} from 'react';

import {Box, type SxProps, type Theme} from '@mui/material';

import {
  COLOR,
  CONTROL,
  radius,
  TABULAR,
  TEXT,
  TONES,
  type ToneName,
  WEIGHT,
} from '@/lib/ui-tokens';

/**
 * Pills say status: 20px, full radius, the tone's tint, border and text.
 * Each product keeps its status vocabulary a closed list.
 */
export default function Pill({
  tone = 'neutral',
  children,
  title,
  sx,
}: {
  tone?: ToneName;
  children: ReactNode;
  title?: string;
  sx?: SxProps<Theme>;
}) {
  const t = TONES[tone];
  return (
    <Box
      component="span"
      title={title}
      sx={[
        {
          display: 'inline-flex',
          alignItems: 'center',
          gap: 0.5,
          flexShrink: 0,
          height: CONTROL.xs,
          px: 1,
          borderRadius: radius('full'),
          border: `1px solid ${t.border}`,
          bgcolor: t.bg,
          color: t.text,
          fontSize: TEXT.xs,
          fontWeight: WEIGHT.semibold,
          lineHeight: 1,
          whiteSpace: 'nowrap',
          ...TABULAR,
        },
        ...(Array.isArray(sx) ? sx : [sx]),
      ]}>
      {children}
    </Box>
  );
}

/** A count beside a label or tab: 11px tertiary on the subtle track. */
export function Count({children}: {children: ReactNode}) {
  return (
    <Box
      component="span"
      sx={{
        flexShrink: 0,
        px: 0.75,
        borderRadius: radius('full'),
        bgcolor: COLOR.bgSubtle,
        color: COLOR.textTertiary,
        fontSize: TEXT['2xs'],
        lineHeight: '18px',
        ...TABULAR,
      }}>
      {children}
    </Box>
  );
}
