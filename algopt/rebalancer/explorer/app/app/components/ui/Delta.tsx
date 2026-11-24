'use client';

import type {ReactNode} from 'react';

import {Box} from '@mui/material';
import {ArrowDown, ArrowUp} from 'lucide-react';

import {COLOR, TABULAR, WEIGHT} from '@/lib/ui-tokens';

/** Whether a change is good: the metric's direction decides, not the sign. */
export type ChangeTone = 'good' | 'bad' | 'zero';

/**
 * Rebalancer minimizes every objective and constraint, so lower is better
 * unless `higherIsBetter`.
 */
export function changeTone(delta: number, higherIsBetter = false): ChangeTone {
  if (Number.isNaN(delta)) {
    return 'bad';
  }
  if (delta === 0) {
    return 'zero';
  }
  return delta < 0 !== higherIsBetter ? 'good' : 'bad';
}

const TONE_COLOR: Record<ChangeTone, string> = {
  good: COLOR.positiveText,
  bad: COLOR.negativeText,
  zero: COLOR.textTertiary,
};

/**
 * Deltas say change and whether it is good: signed text in the direction's
 * color, semibold and tabular; no change is tertiary.
 */
export default function Delta({
  tone,
  children,
}: {
  tone: ChangeTone;
  children: ReactNode;
}) {
  return (
    <Box
      component="span"
      sx={{
        color: TONE_COLOR[tone],
        fontWeight: tone === 'zero' ? WEIGHT.medium : WEIGHT.semibold,
        whiteSpace: 'nowrap',
        ...TABULAR,
      }}>
      {children}
    </Box>
  );
}

/**
 * The status icon at the start of a changed row: ↓ or ↑ in an 18px circle of
 * the direction's tint, so the color never stands alone.
 */
export function ChangeIcon({delta, tone}: {delta: number; tone: ChangeTone}) {
  if (tone === 'zero') {
    return null;
  }
  const Icon = delta < 0 ? ArrowDown : ArrowUp;
  const good = tone === 'good';
  return (
    <Box
      component="span"
      role="img"
      aria-label={`${delta < 0 ? 'lower' : 'higher'} (${good ? 'better' : 'worse'})`}
      sx={{
        display: 'inline-grid',
        placeItems: 'center',
        flexShrink: 0,
        width: 18,
        height: 18,
        borderRadius: '50%',
        bgcolor: good ? COLOR.positiveBg : COLOR.negativeBg,
        color: good ? COLOR.positiveText : COLOR.negativeText,
      }}>
      <Icon size={12} strokeWidth={2.5} aria-hidden />
    </Box>
  );
}
