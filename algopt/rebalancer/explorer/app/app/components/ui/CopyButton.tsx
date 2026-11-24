'use client';

import {useState} from 'react';

import {IconButton, Tooltip} from '@mui/material';
import {Check, Copy} from 'lucide-react';

import {showToast} from '@/lib/toast';

/** Icon button that copies `value`, confirmed with a check and a toast. */
export default function CopyButton({
  value,
  label = 'Copy',
  size = 14,
  toast = 'Copied',
}: {
  value: string;
  label?: string;
  size?: number;
  toast?: string;
}) {
  const [copied, setCopied] = useState(false);

  const handleCopy = async () => {
    // navigator.clipboard is undefined in non-secure contexts.
    if (navigator.clipboard == null) {
      return;
    }
    try {
      await navigator.clipboard.writeText(value);
      setCopied(true);
      showToast(toast);
    } catch {
      // Write failed; keep the current state.
    }
  };

  return (
    <Tooltip title={copied ? 'Copied' : label}>
      <IconButton
        size="small"
        aria-label={label}
        onClick={handleCopy}
        onMouseLeave={() => setCopied(false)}
        onBlur={() => setCopied(false)}>
        {copied ? <Check size={size} /> : <Copy size={size} />}
      </IconButton>
    </Tooltip>
  );
}
