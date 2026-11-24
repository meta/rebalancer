'use client';

import type {ReactNode} from 'react';

import {Box, Typography} from '@mui/material';

import {COLOR, TEXT} from '@/lib/ui-tokens';

/**
 * Card header: 14px title, a 12px sub-line or meta, and card-level actions
 * (small default buttons) on the right.
 */
export default function SectionHeader({
  title,
  meta,
  actions,
  divider = false,
}: {
  title: ReactNode;
  meta?: ReactNode;
  actions?: ReactNode;
  divider?: boolean;
}) {
  return (
    <Box
      sx={{
        display: 'flex',
        alignItems: 'center',
        flexWrap: 'wrap',
        columnGap: 1.25,
        rowGap: 0.5,
        px: 2,
        pt: 1.5,
        pb: 1.25,
        borderBottom: divider ? `1px solid ${COLOR.borderDefault}` : 0,
      }}>
      <Typography variant="h6" component="h2">
        {title}
      </Typography>
      {meta != null && (
        <Box
          component="span"
          sx={{
            color: COLOR.textTertiary,
            fontSize: TEXT.xs,
            display: 'flex',
            alignItems: 'center',
            gap: 1,
            minWidth: 0,
            '& .MuiTypography-root': {
              fontSize: TEXT.xs,
              color: COLOR.textTertiary,
            },
          }}>
          {meta}
        </Box>
      )}
      {actions != null && (
        <Box
          sx={{
            ml: 'auto',
            display: 'flex',
            alignItems: 'center',
            flexWrap: 'wrap',
            gap: 1,
          }}>
          {actions}
        </Box>
      )}
    </Box>
  );
}
