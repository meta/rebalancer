'use client';

import {IconButton, Tooltip} from '@mui/material';
import {Link2} from 'lucide-react';

import {showToast} from '@/lib/toast';

/**
 * Copies the page's address. Filters, tabs, the open panel and the comparison
 * are in the URL, so the link shares exactly what is on screen.
 */
export default function ShareButton() {
  const copy = async () => {
    if (navigator.clipboard == null) {
      return;
    }
    try {
      await navigator.clipboard.writeText(window.location.href);
      showToast('Link copied');
    } catch {
      // Write failed; nothing to confirm.
    }
  };
  return (
    <Tooltip title="Copy a link to this view">
      <IconButton aria-label="Copy a link to this view" onClick={copy}>
        <Link2 size={18} />
      </IconButton>
    </Tooltip>
  );
}
