'use client';

import type {ReactNode} from 'react';

import {Box} from '@mui/material';
import {ArrowDown, ArrowUp} from 'lucide-react';

import HintIcon from '@/app/components/ui/HintIcon';

/** Column header content: label, sort direction and an optional hint. */
export default function HeaderLabel({
  label,
  sorted = false,
  hint,
  hintLabel,
  align = 'left',
}: {
  label: ReactNode;
  sorted?: false | 'asc' | 'desc';
  hint?: string;
  /** Column name for the hint's accessible label. */
  hintLabel?: string;
  align?: 'left' | 'right';
}) {
  return (
    <Box
      component="span"
      sx={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: align === 'right' ? 'flex-end' : 'flex-start',
        gap: 0.5,
      }}>
      <Box component="span" sx={{minWidth: 0, overflowWrap: 'break-word'}}>
        {label}
      </Box>
      {hint != null && (
        <HintIcon
          title={hint}
          label={`Help: ${hintLabel ?? (typeof label === 'string' ? label : 'column')}`}
        />
      )}
      {sorted === 'asc' && <ArrowUp size={14} aria-label="sorted ascending" />}
      {sorted === 'desc' && (
        <ArrowDown size={14} aria-label="sorted descending" />
      )}
    </Box>
  );
}
