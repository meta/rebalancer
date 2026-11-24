'use client';

import {useCallback, useState} from 'react';

import {Box} from '@mui/material';

import type {ChangeTone} from '@/app/components/ui/Delta';
import {getBadgeColor, precise} from '@/lib/format';
import {COLOR, TABULAR, WEIGHT} from '@/lib/ui-tokens';

function formatNumber(value: number, fixedDecimals?: number): string {
  if (fixedDecimals != null) {
    return value.toFixed(fixedDecimals);
  }
  return precise(value);
}

/** True minus signs, and "+" in front of a positive change. */
function display(text: string, value: number, signed: boolean): string {
  const t = text.replace(/-/g, '\u2212');
  return signed && value > 0 ? `+${t}` : t;
}

const TONE_COLOR: Record<ChangeTone, string> = {
  good: COLOR.positiveText,
  bad: COLOR.negativeText,
  zero: COLOR.textTertiary,
};

/**
 * A number that toggles between formatted and raw on click. With
 * `highlight`, the value reads as a delta: positive-text when it is good,
 * negative-text when it is bad (positive is bad unless `positiveIsBad` is
 * false), tertiary when it is 0. `signed` shows "+" on a positive change.
 */
export default function PreciseNumber({
  value,
  highlight,
  positiveIsBad = true,
  fixedDecimals,
  signed = false,
}: {
  value: number;
  highlight: boolean;
  positiveIsBad?: boolean;
  fixedDecimals?: number;
  signed?: boolean;
}) {
  const [showRaw, setShowRaw] = useState(false);

  const displayText = showRaw
    ? String(value)
    : display(formatNumber(value, fixedDecimals), value, signed);

  const toggle = useCallback((e: React.MouseEvent) => {
    e.stopPropagation();
    setShowRaw(prev => !prev);
  }, []);

  const title = showRaw ? 'Click for formatted' : 'Click for raw';
  const badge = highlight ? getBadgeColor(value, positiveIsBad) : null;
  const tone: ChangeTone | null =
    badge === 'success'
      ? 'good'
      : badge === 'error'
        ? 'bad'
        : highlight && value === 0
          ? 'zero'
          : null;

  return (
    <Box
      component="span"
      onClick={toggle}
      title={title}
      sx={{
        cursor: 'pointer',
        overflowWrap: 'anywhere',
        ...TABULAR,
        ...(tone != null && {
          color: TONE_COLOR[tone],
          fontWeight: tone === 'zero' ? WEIGHT.medium : WEIGHT.semibold,
        }),
        '&:hover': {textDecoration: 'underline'},
      }}>
      {displayText}
    </Box>
  );
}
