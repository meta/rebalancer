'use client';

import {Box} from '@mui/material';

import {ACCENT, TEXT_DISABLED} from '@/lib/ui-tokens';

/**
 * A small line of values over time with a dot at the end. A flat series is
 * drawn as a muted midline. `label` is the accessible description.
 */
export default function Sparkline({
  points,
  label,
  width = 120,
  height = 28,
}: {
  points: ReadonlyArray<{t: number; v: number}>;
  label: string;
  width?: number;
  height?: number;
}) {
  if (points.length < 2) {
    return null;
  }
  const pad = 3;
  const t0 = points[0].t;
  const t1 = points[points.length - 1].t;
  let vMin = Infinity;
  let vMax = -Infinity;
  for (const p of points) {
    vMin = Math.min(vMin, p.v);
    vMax = Math.max(vMax, p.v);
  }
  const flat = vMax === vMin;
  const x = (t: number) =>
    pad + (t1 === t0 ? 0 : ((t - t0) / (t1 - t0)) * (width - 2 * pad));
  const y = (v: number) =>
    flat ? height / 2 : pad + ((vMax - v) / (vMax - vMin)) * (height - 2 * pad);
  const d = points
    .map(
      (p, i) =>
        `${i === 0 ? 'M' : 'L'}${x(p.t).toFixed(1)},${y(p.v).toFixed(1)}`,
    )
    .join('');
  const last = points[points.length - 1];
  const color = flat ? TEXT_DISABLED : ACCENT;
  return (
    <Box
      component="svg"
      role="img"
      aria-label={label}
      viewBox={`0 0 ${width} ${height}`}
      sx={{width, height, display: 'block', flexShrink: 0}}>
      <title>{label}</title>
      <path
        d={d}
        fill="none"
        stroke={color}
        strokeWidth={1.5}
        strokeLinejoin="round"
        strokeLinecap="round"
      />
      <circle cx={x(last.t)} cy={y(last.v)} r={2.25} fill={color} />
    </Box>
  );
}
