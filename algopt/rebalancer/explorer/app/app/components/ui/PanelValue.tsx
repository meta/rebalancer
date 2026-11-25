'use client';

import type {ReactNode} from 'react';

import {Box} from '@mui/material';

import {COLOR, radius, TABULAR, TEXT, WEIGHT} from '@/lib/ui-tokens';

/**
 * A mini KPI of the panel: a 12px label (with the side's dot for A and B)
 * over the value.
 */
export default function PanelValue({
  label,
  side,
  children,
}: {
  label: string;
  side?: 'A' | 'B';
  children: ReactNode;
}) {
  return (
    <Box
      sx={{
        minWidth: 0,
        px: 1.5,
        py: 1.25,
        border: `1px solid ${COLOR.borderDefault}`,
        borderRadius: radius('lg'),
        bgcolor: COLOR.bgSurface,
      }}>
      <Box
        sx={{
          display: 'flex',
          alignItems: 'center',
          gap: 0.75,
          color: COLOR.textTertiary,
          fontSize: TEXT.xs,
          fontWeight: WEIGHT.medium,
        }}>
        {side != null && (
          <Box
            component="span"
            aria-hidden
            sx={{
              width: 8,
              height: 8,
              borderRadius: '50%',
              flexShrink: 0,
              bgcolor: side === 'A' ? COLOR.baseline : COLOR.current,
            }}
          />
        )}
        <Box
          component="span"
          sx={{
            overflow: 'hidden',
            textOverflow: 'ellipsis',
            whiteSpace: 'nowrap',
          }}>
          {label}
        </Box>
      </Box>
      <Box
        sx={{
          mt: 0.5,
          fontSize: TEXT.lg,
          fontWeight: WEIGHT.heading,
          lineHeight: 1.3,
          overflowWrap: 'anywhere',
          ...TABULAR,
        }}>
        {children}
      </Box>
    </Box>
  );
}
