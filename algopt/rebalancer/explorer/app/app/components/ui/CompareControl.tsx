'use client';

import {Box, ButtonBase, Tooltip} from '@mui/material';

import {COLOR, CONTROL, DURATION, radius, TEXT, WEIGHT} from '@/lib/ui-tokens';

function Dot({color}: {color: string}) {
  return (
    <Box
      component="span"
      aria-hidden
      sx={{
        width: 8,
        height: 8,
        borderRadius: '50%',
        flexShrink: 0,
        bgcolor: color,
      }}
    />
  );
}

/**
 * The one Compare control of a page: the baseline side (grey, left) and the
 * current side (blue, right). With `onEdit` it opens the page's comparison
 * editor.
 */
export default function CompareControl({
  baseline,
  current,
  onEdit,
  editLabel = 'Edit comparison',
}: {
  baseline: string;
  current: string;
  onEdit?: () => void;
  editLabel?: string;
}) {
  const sides = (
    <>
      <Box
        component="span"
        sx={{
          px: 1.25,
          height: '100%',
          display: 'flex',
          alignItems: 'center',
          color: COLOR.textTertiary,
          fontSize: TEXT.xs,
          borderRight: `1px solid ${COLOR.borderDefault}`,
        }}>
        Compare
      </Box>
      <Box
        component="span"
        sx={{
          display: 'flex',
          alignItems: 'center',
          gap: 0.75,
          px: 1.25,
          minWidth: 0,
          fontWeight: WEIGHT.semibold,
        }}>
        <Dot color={COLOR.baseline} />
        <Box
          component="span"
          sx={{overflow: 'hidden', textOverflow: 'ellipsis', minWidth: 0}}>
          {baseline}
        </Box>
        <Box
          component="span"
          aria-label="to"
          sx={{color: COLOR.textDecorative, fontWeight: WEIGHT.regular}}>
          →
        </Box>
        <Dot color={COLOR.current} />
        <Box
          component="span"
          sx={{overflow: 'hidden', textOverflow: 'ellipsis', minWidth: 0}}>
          {current}
        </Box>
      </Box>
    </>
  );
  const frame = {
    display: 'inline-flex',
    alignItems: 'center',
    height: CONTROL.md,
    maxWidth: '100%',
    border: `1px solid ${COLOR.borderDefault}`,
    borderRadius: radius('md'),
    bgcolor: COLOR.bgSurface,
    overflow: 'hidden',
    whiteSpace: 'nowrap',
    fontSize: TEXT.sm,
    color: COLOR.textPrimary,
  } as const;
  if (onEdit == null) {
    return (
      <Box data-shell="compare" aria-label="Comparison" sx={frame}>
        {sides}
      </Box>
    );
  }
  return (
    <Tooltip title={editLabel}>
      <ButtonBase
        data-shell="compare"
        aria-label={`Comparison: ${baseline} to ${current}. ${editLabel}`}
        onClick={onEdit}
        sx={{
          ...frame,
          font: 'inherit',
          transition: `border-color ${DURATION.fast}ms`,
          '&:hover': {borderColor: COLOR.borderStrong},
        }}>
        {sides}
      </ButtonBase>
    </Tooltip>
  );
}
