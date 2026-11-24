'use client';

import {useCallback, useEffect, useRef, useState, type ReactNode} from 'react';

import {Box, Paper, Popper, Typography} from '@mui/material';

import CopyButton from '@/app/components/ui/CopyButton';

const SHOW_DELAY_MS = 350;
const HIDE_DELAY_MS = 150;

function isClipped(el: HTMLElement): boolean {
  return el.scrollWidth > el.clientWidth || el.scrollHeight > el.clientHeight;
}

/**
 * One hover card for a whole table. Cells mark clipped text with
 * `data-overflow` (the full value in `data-full`, or the element's text);
 * when such text is actually cut off, hovering it (or focusing its cell
 * from the keyboard) shows the full value with a copy button. No per-cell
 * tooltip components.
 */
export default function CellHoverCard({children}: {children: ReactNode}) {
  const [target, setTarget] = useState<{el: HTMLElement; text: string} | null>(
    null,
  );
  const showTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const hideTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  const clearTimers = () => {
    if (showTimer.current != null) {
      clearTimeout(showTimer.current);
      showTimer.current = null;
    }
    if (hideTimer.current != null) {
      clearTimeout(hideTimer.current);
      hideTimer.current = null;
    }
  };

  useEffect(() => clearTimers, []);

  const cancelHide = () => {
    if (hideTimer.current != null) {
      clearTimeout(hideTimer.current);
      hideTimer.current = null;
    }
  };

  const scheduleHide = useCallback(() => {
    if (showTimer.current != null) {
      clearTimeout(showTimer.current);
      showTimer.current = null;
    }
    hideTimer.current ??= setTimeout(() => {
      hideTimer.current = null;
      setTarget(null);
    }, HIDE_DELAY_MS);
  }, []);

  const onMouseOver = useCallback(
    (e: React.MouseEvent) => {
      const el = (e.target as HTMLElement).closest<HTMLElement>(
        '[data-overflow]',
      );
      if (el == null) {
        scheduleHide();
        return;
      }
      cancelHide();
      if (target?.el === el) {
        return;
      }
      if (showTimer.current != null) {
        clearTimeout(showTimer.current);
      }
      showTimer.current = setTimeout(() => {
        showTimer.current = null;
        if (isClipped(el)) {
          setTarget({el, text: el.dataset.full ?? el.textContent ?? ''});
        }
      }, SHOW_DELAY_MS);
    },
    [scheduleHide, target],
  );

  // Keyboard users get the card when they tab onto a clipped cell.
  const onFocus = (e: React.FocusEvent) => {
    const focused = e.target as HTMLElement;
    if (!focused.matches(':focus-visible')) {
      return;
    }
    const el =
      focused.closest<HTMLElement>('[data-overflow]') ??
      focused.querySelector<HTMLElement>('[data-overflow]');
    clearTimers();
    setTarget(
      el != null && isClipped(el)
        ? {el, text: el.dataset.full ?? el.textContent ?? ''}
        : null,
    );
  };

  const onKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === 'Escape' && target != null) {
      // Esc closes the card first, not an open side pane behind it.
      e.stopPropagation();
      clearTimers();
      setTarget(null);
    }
  };

  return (
    <Box
      onMouseOver={onMouseOver}
      onMouseLeave={scheduleHide}
      onFocus={onFocus}
      onBlur={scheduleHide}
      onKeyDown={onKeyDown}>
      {children}
      <Popper
        open={target != null}
        anchorEl={target?.el}
        placement="bottom-start"
        sx={{zIndex: theme => theme.zIndex.tooltip}}
        modifiers={[{name: 'offset', options: {offset: [0, 4]}}]}>
        <Paper
          elevation={8}
          // The card is portaled, but React events still bubble to the
          // table's handlers above; keep them from hiding the card.
          onMouseOver={e => {
            e.stopPropagation();
            cancelHide();
          }}
          onFocus={e => {
            e.stopPropagation();
            cancelHide();
          }}
          onBlur={e => e.stopPropagation()}
          onMouseLeave={scheduleHide}
          sx={{
            display: 'flex',
            alignItems: 'flex-start',
            gap: 0.5,
            maxWidth: 520,
            px: 1.5,
            py: 1,
          }}>
          <Typography
            variant="body2"
            sx={{overflowWrap: 'anywhere', whiteSpace: 'pre-wrap'}}>
            {target?.text}
          </Typography>
          {target != null && <CopyButton value={target.text} />}
        </Paper>
      </Popper>
    </Box>
  );
}
