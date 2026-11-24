// Tuple-index colors: one hue per objective tuple index, from the most
// important (index 0, dark indigo) through blue and teal to the least
// important (amber). The ramp is the redesign mock's priority ramp, darkened
// where needed so a white label on it keeps WCAG AA contrast (4.5:1).

type Rgb = readonly [number, number, number];

const RAMP: readonly Rgb[] = [
  [49, 46, 129],
  [67, 56, 202],
  [37, 99, 235],
  [8, 145, 178],
  [13, 148, 136],
  [101, 163, 13],
  [161, 98, 7],
];

// A little above 4.5 so rounding never lands below the AA threshold.
const MIN_CONTRAST_WITH_WHITE = 4.6;

function channelLuminance(c: number): number {
  const s = c / 255;
  return s <= 0.04045 ? s / 12.92 : Math.pow((s + 0.055) / 1.055, 2.4);
}

/** WCAG relative luminance of an sRGB color. */
export function relativeLuminance(rgb: Rgb): number {
  return (
    0.2126 * channelLuminance(rgb[0]) +
    0.7152 * channelLuminance(rgb[1]) +
    0.0722 * channelLuminance(rgb[2])
  );
}

/** WCAG contrast ratio of an sRGB color against white. */
export function contrastWithWhite(rgb: Rgb): number {
  return 1.05 / (relativeLuminance(rgb) + 0.05);
}

/** The ramp color at `t` in [0, 1], before the contrast adjustment. */
function rampAt(t: number): Rgb {
  const x = Math.min(1, Math.max(0, t)) * (RAMP.length - 1);
  const i = Math.floor(x);
  const f = x - i;
  const a = RAMP[i];
  const b = RAMP[Math.min(RAMP.length - 1, i + 1)];
  return [
    Math.round(a[0] + (b[0] - a[0]) * f),
    Math.round(a[1] + (b[1] - a[1]) * f),
    Math.round(a[2] + (b[2] - a[2]) * f),
  ];
}

/**
 * Color of a tuple index, as `rgb(...)`. The run's largest tuple index maps to
 * the end of the ramp, so the same index has the same color on every page of
 * a run.
 */
export function tupleColor(tupleIndex: number, maxTupleIndex: number): string {
  const t = maxTupleIndex > 0 ? tupleIndex / maxTupleIndex : 0;
  let c = rampAt(t);
  for (
    let i = 0;
    i < 40 && contrastWithWhite(c) < MIN_CONTRAST_WITH_WHITE;
    i++
  ) {
    c = [
      Math.round(c[0] * 0.96),
      Math.round(c[1] * 0.96),
      Math.round(c[2] * 0.96),
    ];
  }
  return `rgb(${c[0]}, ${c[1]}, ${c[2]})`;
}
