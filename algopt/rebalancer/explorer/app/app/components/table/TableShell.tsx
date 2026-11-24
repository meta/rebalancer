'use client';

import type {ReactNode} from 'react';

import {Box, type SxProps, type Theme} from '@mui/material';

import CellHoverCard from './CellHoverCard';
import {TABLE_SX} from './tableSx';

/**
 * Scroll container plus `<table>` with the shared table look. Below the
 * table's `minWidth` it scrolls sideways inside its card, never the page.
 */
export default function TableShell({
  children,
  minWidth = 640,
  fixedLayout = false,
  tableSx,
  label,
}: {
  children: ReactNode;
  minWidth?: number;
  fixedLayout?: boolean;
  tableSx?: SxProps<Theme>;
  label?: string;
}) {
  return (
    <CellHoverCard>
      <Box sx={{overflowX: 'auto', maxWidth: '100%'}}>
        <Box
          component="table"
          aria-label={label}
          sx={[
            TABLE_SX,
            {minWidth, tableLayout: fixedLayout ? 'fixed' : 'auto'},
            ...(Array.isArray(tableSx) ? tableSx : [tableSx]),
          ]}>
          {children}
        </Box>
      </Box>
    </CellHoverCard>
  );
}
