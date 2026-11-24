'use client';

import {Box, Typography} from '@mui/material';
import {ArrowRight} from 'lucide-react';

import {precise} from '@/lib/format';
import {COLOR} from '@/lib/ui-tokens';

/**
 * Destination value and the change after the source value, in the tree-table's
 * baseline → current form: the change is signed and green when it moves the
 * way the expression is optimized.
 */
export function NodeDelta({
  sourceValue,
  destinationValue,
  minimizing,
}: {
  sourceValue: number;
  destinationValue: number;
  minimizing: boolean;
}) {
  const delta = destinationValue - sourceValue;
  if (delta === 0) {
    return null;
  }

  const effectiveDelta = (minimizing ? 1 : -1) * delta;
  const color = effectiveDelta < 0 ? COLOR.positiveText : COLOR.negativeText;
  const signed = `${delta > 0 ? '+' : ''}${precise(delta)}`.replace(
    /-/g,
    '\u2212',
  );

  return (
    <>
      <Box
        component="span"
        aria-label="to"
        sx={{display: 'inline-flex', mx: 0.5, color: COLOR.textDecorative}}>
        <ArrowRight size={14} />
      </Box>
      <Typography
        component="span"
        variant="subtitle2"
        sx={{fontVariantNumeric: 'tabular-nums'}}>
        {precise(destinationValue).replace(/-/g, '\u2212')}
      </Typography>
      <Typography
        component="span"
        variant="subtitle2"
        sx={{ml: 0.75, fontVariantNumeric: 'tabular-nums', color}}>
        {signed}
      </Typography>
    </>
  );
}
