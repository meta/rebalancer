'use client';

import {Box} from '@mui/material';

/**
 * A small line over evenly spaced values, with a light area under it and a
 * dot at the last value. `label` is the accessible description.
 */
export default function Sparkline({
  values,
  color,
  label,
  width = 104,
  height = 26,
}: {
  values: readonly number[];
  color: string;
  label: string;
  width?: number;
  height?: number;
}) {
  if (values.length < 2) {
    return null;
  }
  const pad = 2;
  let lo = Math.min(...values);
  let hi = Math.max(...values);
  if (hi - lo < 1e-12) {
    hi = lo + 1;
  }
  const n = values.length;
  const x = (i: number) => pad + (i * (width - 2 * pad)) / (n - 1);
  const y = (v: number) =>
    height - pad - ((v - lo) / (hi - lo)) * (height - 2 * pad);
  const line = values
    .map(
      (v, i) => `${i === 0 ? 'M' : 'L'}${x(i).toFixed(1)},${y(v).toFixed(1)}`,
    )
    .join('');
  const bottom = (height - pad).toFixed(1);
  const area = `${line}L${x(n - 1).toFixed(1)},${bottom}L${x(0).toFixed(1)},${bottom}Z`;
  const last = values[n - 1];
  return (
    <Box
      component="svg"
      role="img"
      aria-label={label}
      viewBox={`0 0 ${width} ${height}`}
      sx={{width, height, display: 'block', flexShrink: 0}}>
      <title>{label}</title>
      <path d={area} fill={color} fillOpacity={0.1} />
      <path
        d={line}
        fill="none"
        stroke={color}
        strokeWidth={1.6}
        strokeLinejoin="round"
        strokeLinecap="round"
      />
      <circle cx={x(n - 1)} cy={y(last)} r={2.4} fill={color} />
    </Box>
  );
}
