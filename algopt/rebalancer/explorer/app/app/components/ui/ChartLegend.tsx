'use client';

import type {ReactNode} from 'react';

import {Box} from '@mui/material';

import {COLOR, TEXT} from '@/lib/ui-tokens';

/**
 * A chart legend in the design language: under the chart, left-aligned,
 * 12px secondary text with a 10px rounded swatch per series. Pass it as a
 * recharts `<Legend content={ChartLegend} />`.
 */
export default function ChartLegend({
  payload,
}: {
  payload?: ReadonlyArray<{value?: unknown; color?: string}>;
}): ReactNode {
  if (payload == null || payload.length === 0) {
    return null;
  }
  return (
    <Box
      component="ul"
      sx={{
        display: 'flex',
        flexWrap: 'wrap',
        columnGap: 2,
        rowGap: 0.5,
        listStyle: 'none',
        m: 0,
        p: 0,
        pt: 1,
        pl: 1,
        fontSize: TEXT.xs,
        color: COLOR.textSecondary,
      }}>
      {payload.map(entry => (
        <Box
          component="li"
          key={String(entry.value)}
          sx={{display: 'inline-flex', alignItems: 'center', gap: 0.75}}>
          <Box
            component="span"
            aria-hidden
            sx={{
              width: 10,
              height: 10,
              borderRadius: '3px',
              flexShrink: 0,
              bgcolor: entry.color,
            }}
          />
          {String(entry.value)}
        </Box>
      ))}
    </Box>
  );
}
