'use client';

import {Box} from '@mui/material';

import {formatCompact, formatCount} from '@/lib/format';
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

/**
 * Baseline / current bars: Initial (grey) over Final (blue) on one scale,
 * the larger of the two. Signs are shown as magnitudes; the text beside the
 * bars carries the values.
 */
export function BaselineCurrentBars({
  a,
  b,
  width = 96,
}: {
  a: number;
  b: number;
  width?: number | string;
}) {
  const max = Math.max(Math.abs(a), Math.abs(b));
  const pct = (v: number) =>
    max === 0
      ? '0%'
      : `${Math.max(Math.abs(v) > 0 ? 2 : 0, (100 * Math.abs(v)) / max)}%`;
  const bar = (v: number, color: string) => (
    <Box
      sx={{
        height: 5,
        borderRadius: radius('full'),
        bgcolor: COLOR.bgSubtle,
        overflow: 'hidden',
      }}>
      <Box
        sx={{
          width: pct(v),
          height: '100%',
          borderRadius: radius('full'),
          bgcolor: color,
        }}
      />
    </Box>
  );
  return (
    <Box
      role="img"
      aria-label={`Initial ${formatCompact(a)}, final ${formatCompact(b)}`}
      sx={{
        width,
        flexShrink: 0,
        display: 'grid',
        gap: '2px',
      }}>
      {bar(a, COLOR.baseline)}
      {bar(b, COLOR.current)}
    </Box>
  );
}

/**
 * Moves out and in on one scale: out grows left from the center in the
 * baseline grey (what the item had), in grows right in the current blue
 * (what it gained). Callers write the counts as well.
 */
export function OutInBar({
  out,
  into,
  max,
  width = 64,
}: {
  out: number;
  into: number;
  max: number;
  width?: number;
}) {
  const half = width / 2;
  const w = (n: number) =>
    n === 0 || max === 0 ? 0 : Math.max(2, (n / max) * half);
  return (
    <Box
      role="img"
      aria-label={`${formatCount(out)} out, ${formatCount(into)} in`}
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
          right: half,
          width: w(out),
          borderRadius: radius('full'),
          bgcolor: COLOR.baseline,
        }}
      />
      <Box
        sx={{
          position: 'absolute',
          top: 0,
          bottom: 0,
          left: half,
          width: w(into),
          borderRadius: radius('full'),
          bgcolor: COLOR.current,
        }}
      />
    </Box>
  );
}

/**
 * A part of a whole on a 6px track, in the current blue (what changed).
 * Callers write the numbers as well.
 */
export function ShareBar({
  part,
  whole,
  label,
  width = 64,
}: {
  part: number;
  whole: number;
  label: string;
  width?: number;
}) {
  const pct = whole > 0 ? Math.min(100, Math.max(3, (100 * part) / whole)) : 0;
  return (
    <Box
      role="img"
      aria-label={label}
      sx={{
        width,
        height: 6,
        flexShrink: 0,
        borderRadius: radius('full'),
        bgcolor: COLOR.bgSubtle,
        overflow: 'hidden',
      }}>
      <Box
        sx={{
          width: `${pct}%`,
          height: '100%',
          borderRadius: radius('full'),
          bgcolor: COLOR.current,
        }}
      />
    </Box>
  );
}
