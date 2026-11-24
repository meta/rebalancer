'use client';

import type {AutocompleteRenderGroupParams} from '@mui/material';
import {Box} from '@mui/material';

// Renders the column-type heading in the Group by / Columns to show pickers as a
// small bold gray label, so it reads as a heading above its column names rather
// than as another option.
export function renderColumnTypeGroup(params: AutocompleteRenderGroupParams) {
  return (
    <li key={params.key}>
      <Box
        sx={{
          position: 'sticky',
          top: 0,
          zIndex: 1,
          bgcolor: 'background.paper',
          px: 2,
          pt: 1,
          pb: 0.5,
          typography: 'overline',
          lineHeight: 1.5,
        }}>
        {params.group}
      </Box>
      {/* Indent the column names a step to the right of the type heading. */}
      <Box component="ul" sx={{p: 0, pl: 2}}>
        {params.children}
      </Box>
    </li>
  );
}
