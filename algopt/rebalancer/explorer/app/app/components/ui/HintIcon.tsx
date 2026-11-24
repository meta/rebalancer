'use client';

import type {ReactNode} from 'react';

import {Box, Tooltip} from '@mui/material';
import {CircleHelp} from 'lucide-react';

import {COLOR, DURATION, focusRing} from '@/lib/ui-tokens';

/**
 * Hints invite: a 15px "?" that explains a label in a tooltip, with a 24px
 * hit area.
 */
export default function HintIcon({
  title,
  label,
}: {
  title: ReactNode;
  /** Accessible name, e.g. "Help: Total". */
  label: string;
}) {
  return (
    <Tooltip title={title}>
      <Box
        component="span"
        role="img"
        aria-label={label}
        tabIndex={0}
        onClick={e => e.stopPropagation()}
        sx={{
          position: 'relative',
          display: 'inline-flex',
          flexShrink: 0,
          color: COLOR.textTertiary,
          cursor: 'help',
          borderRadius: '50%',
          transition: `color ${DURATION.fast}ms`,
          '&::after': {content: '""', position: 'absolute', inset: -5},
          '&:hover, &:focus-visible': {color: COLOR.textPrimary},
          '&:focus-visible': focusRing(),
        }}>
        <CircleHelp size={15} strokeWidth={2} />
      </Box>
    </Tooltip>
  );
}
