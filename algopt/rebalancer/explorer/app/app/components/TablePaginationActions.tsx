'use client';

import {useEffect, useState} from 'react';

import {Box, IconButton, TextField, Typography} from '@mui/material';
import {
  ChevronLeft,
  ChevronRight,
  ChevronsLeft,
  ChevronsRight,
} from 'lucide-react';

// Props are the subset of MUI's TablePaginationActionsProps that we use; MUI
// passes all of these to whatever component is given as `ActionsComponent`.
interface TablePaginationActionsProps {
  count: number;
  page: number;
  rowsPerPage: number;
  onPageChange: (
    event: React.MouseEvent<HTMLButtonElement> | null,
    page: number,
  ) => void;
}

/**
 * Pagination actions with first/last-page buttons and a direct "jump to page"
 * input, so large result sets (hundreds of pages) don't require clicking
 * through one page at a time. Drop in as the `ActionsComponent` of MUI's
 * `TablePagination`. Pages are 0-indexed internally (MUI convention) but the
 * jump input is displayed and parsed 1-indexed for the user.
 */
export default function TablePaginationActions({
  count,
  page,
  rowsPerPage,
  onPageChange,
}: TablePaginationActionsProps) {
  const lastPage = Math.max(0, Math.ceil(count / rowsPerPage) - 1);

  // Local draft of the jump input so the user can type freely; committed on
  // Enter or blur. Kept in sync when the page changes from elsewhere.
  const [draft, setDraft] = useState(String(page + 1));
  useEffect(() => {
    setDraft(String(page + 1));
  }, [page]);

  const commitDraft = () => {
    const parsed = parseInt(draft, 10);
    if (Number.isNaN(parsed)) {
      setDraft(String(page + 1));
      return;
    }
    const clamped = Math.min(Math.max(parsed - 1, 0), lastPage);
    if (clamped !== page) {
      onPageChange(null, clamped);
    }
    setDraft(String(clamped + 1));
  };

  return (
    <Box sx={{display: 'flex', alignItems: 'center', flexShrink: 0, ml: 2}}>
      <IconButton
        onClick={event => onPageChange(event, 0)}
        disabled={page <= 0}
        aria-label="First page"
        size="small">
        <ChevronsLeft size={16} />
      </IconButton>
      <IconButton
        onClick={event => onPageChange(event, page - 1)}
        disabled={page <= 0}
        aria-label="Previous page"
        size="small">
        <ChevronLeft size={16} />
      </IconButton>
      <Box sx={{display: 'flex', alignItems: 'center', gap: 0.5, mx: 0.5}}>
        <TextField
          value={draft}
          onChange={event => setDraft(event.target.value)}
          onKeyDown={event => {
            if (event.key === 'Enter') {
              commitDraft();
            }
          }}
          onBlur={commitDraft}
          size="small"
          type="number"
          aria-label="Page number"
          slotProps={{
            htmlInput: {
              min: 1,
              max: lastPage + 1,
              style: {textAlign: 'center'},
            },
          }}
          sx={{
            width: 64,
            '& .MuiInputBase-root': {height: 32, typography: 'body2'},
            '& input': {px: 0.75, py: 0},
          }}
        />
        <Typography
          variant="body2"
          color="text.secondary"
          sx={{whiteSpace: 'nowrap'}}>
          / {lastPage + 1}
        </Typography>
      </Box>
      <IconButton
        onClick={event => onPageChange(event, page + 1)}
        disabled={page >= lastPage}
        aria-label="Next page"
        size="small">
        <ChevronRight size={16} />
      </IconButton>
      <IconButton
        onClick={event => onPageChange(event, lastPage)}
        disabled={page >= lastPage}
        aria-label="Last page"
        size="small">
        <ChevronsRight size={16} />
      </IconButton>
    </Box>
  );
}
