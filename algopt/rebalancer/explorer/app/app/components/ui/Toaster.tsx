'use client';

import {useEffect, useState} from 'react';

import {Snackbar} from '@mui/material';

import {TOAST_EVENT, TOAST_MS} from '@/lib/toast';
import {Z} from '@/lib/ui-tokens';

/** Shows the messages of `showToast`, bottom center. */
export default function Toaster() {
  const [toast, setToast] = useState<{message: string; key: number} | null>(
    null,
  );
  useEffect(() => {
    const onToast = (e: Event) =>
      setToast({message: (e as CustomEvent<string>).detail, key: Date.now()});
    window.addEventListener(TOAST_EVENT, onToast);
    return () => window.removeEventListener(TOAST_EVENT, onToast);
  }, []);
  return (
    <Snackbar
      key={toast?.key}
      open={toast != null}
      message={toast?.message}
      autoHideDuration={TOAST_MS}
      onClose={() => setToast(null)}
      anchorOrigin={{vertical: 'bottom', horizontal: 'center'}}
      sx={{zIndex: Z.toast}}
      slotProps={{
        content: {role: 'status', sx: {minWidth: 0, flexGrow: 0}},
      }}
    />
  );
}
