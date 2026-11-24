'use client';

import {Box} from '@mui/material';

import {COLOR, radius} from '@/lib/ui-tokens';

/**
 * Composition bar: the shares of a group, in order better · worse ·
 * unchanged, on an 8px track. Callers write the counts out as well; a slice
 * is never the only sign of a regression.
 */
export function CompositionBar({
  better,
  worse,
  unchanged,
  label,
  width = 150,
}: {
  better: number;
  worse: number;
  unchanged: number;
  label: string;
  width?: number | string;
}) {
  const total = better + worse + unchanged;
  const pct = (n: number) => (total > 0 ? `${(100 * n) / total}%` : '0%');
  return (
    <Box
      role="img"
      aria-label={label}
      sx={{
        display: 'flex',
        width,
        height: 8,
        flexShrink: 0,
        borderRadius: radius('full'),
        overflow: 'hidden',
        bgcolor: COLOR.bgSubtle,
      }}>
      <Box sx={{width: pct(better), bgcolor: COLOR.positive}} />
      <Box sx={{width: pct(worse), bgcolor: COLOR.negative}} />
      <Box sx={{width: pct(unchanged), bgcolor: COLOR.borderStrong}} />
    </Box>
  );
}

/**
 * Unit-free change bar: change ÷ |baseline| (a full bar when the baseline is
 * 0), good growing left from the center, bad right.
 */
export function ChangeBar({
  delta,
  baseline,
  good,
  width = 72,
}: {
  delta: number;
  baseline: number;
  good: boolean;
  width?: number;
}) {
  if (delta === 0 || Number.isNaN(delta)) {
    return <Box aria-hidden sx={{width, height: 6, flexShrink: 0}} />;
  }
  const rel =
    baseline === 0 ? 1 : Math.min(1, Math.abs(delta) / Math.abs(baseline));
  // Never thinner than 3px, so a tiny change is still visible.
  const half = width / 2;
  const w = Math.max(3, rel * half);
  return (
    <Box
      aria-hidden
      sx={{
        position: 'relative',
        width,
        height: 6,
        flexShrink: 0,
        borderRadius: radius('full'),
        bgcolor: COLOR.bgSubtle,
      }}>
      <Box
        sx={{
          position: 'absolute',
          top: 0,
          bottom: 0,
          width: w,
          borderRadius: radius('full'),
          bgcolor: good ? COLOR.positive : COLOR.negative,
          ...(good ? {right: half} : {left: half}),
        }}
      />
    </Box>
  );
}
