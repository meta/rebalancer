'use client';

import type {ReactNode} from 'react';

import {Box, type SxProps, type Theme} from '@mui/material';

import {COLOR, CONTROL, radius, TEXT} from '@/lib/ui-tokens';

/**
 * Chips say facts (column types, properties, names): 20px, radius 6, subtle.
 * `mono` is for identifiers. `small` is the inline chip of dense places
 * (table headers, parameter lists): 16px, no border.
 */
export default function Tag({
  children,
  mono = false,
  small = false,
  title,
  sx,
}: {
  children: ReactNode;
  mono?: boolean;
  small?: boolean;
  title?: string;
  sx?: SxProps<Theme>;
}) {
  return (
    <Box
      component="span"
      title={title}
      sx={[
        {
          display: 'inline-flex',
          alignItems: 'center',
          gap: 0.5,
          maxWidth: '100%',
          minHeight: small ? 16 : CONTROL.xs,
          px: 0.75,
          borderRadius: radius(small ? 'xs' : 'sm'),
          border: small ? 0 : `1px solid ${COLOR.borderDefault}`,
          bgcolor: COLOR.bgSubtle,
          color: COLOR.textSecondary,
          fontSize: mono || small ? TEXT['2xs'] : TEXT.xs,
          fontFamily: mono ? 'var(--font-mono)' : undefined,
          lineHeight: 1.4,
          overflowWrap: 'anywhere',
        },
        ...(Array.isArray(sx) ? sx : [sx]),
      ]}>
      {children}
    </Box>
  );
}

/** The chip look for MUI Chips (Autocomplete values, which need `onDelete`). */
export const TAG_CHIP_SX = {
  height: 'auto',
  minHeight: CONTROL.xs,
  maxWidth: '100%',
  borderRadius: radius('sm'),
  border: `1px solid ${COLOR.borderDefault}`,
  bgcolor: COLOR.bgSubtle,
  color: COLOR.textSecondary,
  fontSize: TEXT.xs,
  '& .MuiChip-label': {
    display: 'block',
    whiteSpace: 'normal',
    overflowWrap: 'anywhere',
    py: '1px',
    px: 0.75,
  },
  '& .MuiChip-deleteIcon': {fontSize: 14, color: COLOR.textDecorative},
} as const;
