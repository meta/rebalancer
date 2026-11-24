'use client';

import {useEffect, useState} from 'react';

import {
  Box,
  Dialog,
  DialogContent,
  DialogTitle,
  IconButton,
  Link,
  Tooltip,
  Typography,
} from '@mui/material';
import {CircleHelp, X} from 'lucide-react';

import {showWhereThingsMoved} from '@/app/components/shell/MigrationStrip';
import Kbd from '@/app/components/ui/Kbd';
import {COLOR, TEXT} from '@/lib/ui-tokens';

function isMac(): boolean {
  return (
    typeof navigator !== 'undefined' &&
    /Mac|iPhone|iPad/.test(navigator.platform)
  );
}

function isTyping(target: EventTarget | null): boolean {
  const el = target as HTMLElement | null;
  return (
    el != null &&
    (el.isContentEditable ||
      ['INPUT', 'TEXTAREA', 'SELECT'].includes(el.tagName))
  );
}

/**
 * "?" (the key or the top bar's help button) opens the list of keyboard
 * shortcuts of the run pages, and can show "where things moved" again.
 */
export default function ShortcutsDialog() {
  const [open, setOpen] = useState(false);
  const mod = isMac() ? '⌘' : 'Ctrl';

  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (
        e.key === '?' &&
        !e.metaKey &&
        !e.ctrlKey &&
        !e.altKey &&
        !isTyping(e.target)
      ) {
        e.preventDefault();
        setOpen(prev => !prev);
      }
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, []);

  const rows: Array<[string[], string]> = [
    [[`${mod} K`], 'Jump to a page, entity table, metric or objective'],
    [[`${mod} B`], 'Show or hide the sidebar'],
    [
      ['j', 'k'],
      'Next or previous row while a goal or constraint is open in the side pane',
    ],
    [['Esc'], 'Close the side pane or a dialog'],
    [['Tab', 'Enter'], 'Move between rows and links, and open one'],
    [['?'], 'This list'],
  ];

  return (
    <>
      <Tooltip title="Keyboard shortcuts (?)">
        <IconButton
          aria-label="Keyboard shortcuts"
          aria-keyshortcuts="?"
          aria-haspopup="dialog"
          onClick={() => setOpen(true)}>
          <CircleHelp size={18} />
        </IconButton>
      </Tooltip>
      <Dialog
        open={open}
        onClose={() => setOpen(false)}
        maxWidth="xs"
        fullWidth
        aria-labelledby="rbx-shortcuts-title">
        <DialogTitle
          id="rbx-shortcuts-title"
          sx={{display: 'flex', alignItems: 'center', pr: 1}}>
          Keyboard shortcuts
          <IconButton
            size="small"
            aria-label="Close"
            onClick={() => setOpen(false)}
            sx={{ml: 'auto'}}>
            <X size={16} />
          </IconButton>
        </DialogTitle>
        <DialogContent sx={{pt: 0}}>
          <Box
            component="dl"
            sx={{
              m: 0,
              display: 'grid',
              gridTemplateColumns: 'auto 1fr',
              columnGap: 2,
              rowGap: 1.25,
              alignItems: 'center',
            }}>
            {rows.map(([keys, what]) => (
              <Box key={what} sx={{display: 'contents'}}>
                <Box
                  component="dt"
                  sx={{display: 'flex', gap: 0.5, justifyContent: 'flex-end'}}>
                  {keys.map(k => (
                    <Kbd key={k}>{k}</Kbd>
                  ))}
                </Box>
                <Typography
                  component="dd"
                  variant="body2"
                  sx={{m: 0, color: COLOR.textSecondary}}>
                  {what}
                </Typography>
              </Box>
            ))}
          </Box>
          <Typography
            variant="body2"
            sx={{
              mt: 2,
              pt: 1.5,
              borderTop: `1px solid ${COLOR.borderDefault}`,
              fontSize: TEXT.xs,
              color: COLOR.textTertiary,
            }}>
            Looking for the breadcrumb or the run status?{' '}
            <Link
              component="button"
              type="button"
              sx={{fontSize: 'inherit', verticalAlign: 'baseline'}}
              onClick={() => {
                setOpen(false);
                showWhereThingsMoved();
              }}>
              Show where things moved
            </Link>
          </Typography>
        </DialogContent>
      </Dialog>
    </>
  );
}
