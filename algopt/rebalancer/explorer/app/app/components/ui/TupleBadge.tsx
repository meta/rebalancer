'use client';

import {Box, Tooltip} from '@mui/material';

import {tupleColor} from '@/lib/priority-colors';
import {COLOR, CONTROL, radius, TABULAR, TEXT, WEIGHT} from '@/lib/ui-tokens';

const TUPLE_HINT =
  'Objective tuple index. Rebalancer compares the totals index by index, starting at 0: a higher index only matters when every lower index ties. The color is the same for this index on every page.';

/**
 * The rank pill of a tuple index: its number in white on the rank ramp
 * (indigo for index 0 through blue and teal to amber), so ranked things show
 * their order the same way on every page.
 */
export default function TupleBadge({
  tupleIndex,
  maxTupleIndex,
  hint = TUPLE_HINT,
}: {
  tupleIndex: number;
  maxTupleIndex: number;
  hint?: string | null;
}) {
  const badge = (
    <Box
      component="span"
      role="img"
      aria-label={`Tuple index ${tupleIndex}`}
      sx={{
        display: 'inline-grid',
        placeItems: 'center',
        flexShrink: 0,
        minWidth: 30,
        height: CONTROL.xs,
        px: 0.75,
        borderRadius: radius('sm'),
        bgcolor: tupleColor(tupleIndex, maxTupleIndex),
        color: COLOR.inverseText,
        fontSize: TEXT['2xs'],
        fontWeight: WEIGHT.bold,
        lineHeight: 1,
        ...TABULAR,
      }}>
      {tupleIndex}
    </Box>
  );
  if (hint == null) {
    return badge;
  }
  return <Tooltip title={hint}>{badge}</Tooltip>;
}
