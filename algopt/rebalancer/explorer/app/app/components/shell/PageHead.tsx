'use client';

import type {ReactNode} from 'react';

import {Box, Typography} from '@mui/material';

import MigrationStrip from '@/app/components/shell/MigrationStrip';
import {COLOR, SHELL} from '@/lib/ui-tokens';

/**
 * The page's content column: padding 24 / 28 / 48 and at most 1480px wide
 * (1640px, centered, from 1900px), 16px between cards. It starts with the
 * "where things moved" strip until that is dismissed.
 */
export function PageContainer({children}: {children: ReactNode}) {
  return (
    <Box
      sx={{
        width: '100%',
        maxWidth: SHELL.pageMax,
        pt: 3,
        px: 3.5,
        pb: 6,
        display: 'flex',
        flexDirection: 'column',
        gap: 2,
        minWidth: 0,
        '@media (min-width: 1900px)': {mx: 'auto', maxWidth: SHELL.pageMaxWide},
        '@media (max-width: 760px)': {p: 2},
      }}>
      <MigrationStrip />
      {children}
    </Box>
  );
}

/** Page heading: the 20px title, a sub-line, and page actions on the right. */
export default function PageHead({
  title,
  sub,
  actions,
}: {
  title: ReactNode;
  sub?: ReactNode;
  actions?: ReactNode;
}) {
  return (
    <Box
      sx={{
        display: 'flex',
        alignItems: 'flex-end',
        flexWrap: 'wrap',
        gap: 2,
        mb: 0.25,
      }}>
      <Box sx={{minWidth: 0, flex: '1 1 320px'}}>
        <Typography variant="h1" sx={{overflowWrap: 'anywhere'}}>
          {title}
        </Typography>
        {sub != null && (
          <Typography
            variant="body2"
            component="div"
            sx={{mt: 0.25, color: COLOR.textTertiary, maxWidth: 820}}>
            {sub}
          </Typography>
        )}
      </Box>
      {actions != null && (
        <Box sx={{display: 'flex', alignItems: 'center', gap: 1}}>
          {actions}
        </Box>
      )}
    </Box>
  );
}
