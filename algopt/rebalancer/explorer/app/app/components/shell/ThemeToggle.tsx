'use client';

import {IconButton, Tooltip} from '@mui/material';
import {useColorScheme} from '@mui/material/styles';
import {Moon, Sun} from 'lucide-react';

/** Light or dark: the design language's two themes, remembered per browser. */
export default function ThemeToggle() {
  const {mode, systemMode, setMode} = useColorScheme();
  const resolved = mode === 'system' ? systemMode : mode;
  const dark = resolved === 'dark';
  const label = dark ? 'Switch to light mode' : 'Switch to dark mode';
  return (
    <Tooltip title={label}>
      <IconButton
        aria-label={label}
        aria-pressed={dark}
        onClick={() => setMode(dark ? 'light' : 'dark')}>
        {dark ? <Sun size={18} /> : <Moon size={18} />}
      </IconButton>
    </Tooltip>
  );
}
