'use client';

import {useEffect, useId, useRef, useState} from 'react';

import {Box, Typography} from '@mui/material';

import {useClipStoreContext, useIsClipped} from './useClipObserver';

const WRAP_STYLES = {
  overflowWrap: 'anywhere',
  wordBreak: 'break-word',
} as const;

export default function CollapsibleText({
  text,
  clampLines,
}: {
  text: string;
  /**
   * When set, the collapsed state wraps text and clamps to this many lines
   * instead of truncating to a single line. Use in unconstrained containers
   * (e.g. the tree viewer) where wrapping is preferred over horizontal scroll.
   */
  clampLines?: number;
}) {
  const [expanded, setExpanded] = useState(false);
  const [localClipped, setLocalClipped] = useState(false);
  const textRef = useRef<HTMLElement | null>(null);
  const id = useId();
  // Tables share one observer (see useClipObserver); text elsewhere
  // measures itself.
  const store = useClipStoreContext();
  const useShared = store != null;
  const sharedClipped = useIsClipped(useShared ? store : null, id);

  useEffect(() => {
    const el = textRef.current;
    if (el == null || !useShared || store == null) {
      return;
    }
    // Re-registering on a new text re-measures it: a sorted row can show a
    // different text at the same width, which ResizeObserver would miss.
    return store.observe(el, id);
  }, [useShared, store, id, text]);

  // Show the toggle only when the collapsed text is actually clipped.
  useEffect(() => {
    const el = textRef.current;
    if (el == null || useShared) {
      return;
    }
    const measure = () => {
      setLocalClipped(
        clampLines != null
          ? el.scrollHeight > el.clientHeight
          : el.scrollWidth > el.clientWidth,
      );
    };
    measure();
    const observer = new ResizeObserver(measure);
    observer.observe(el);
    return () => observer.disconnect();
  }, [text, clampLines, expanded, useShared]);

  const clipped = useShared ? sharedClipped : localClipped;

  const collapsedSx =
    clampLines != null
      ? ({
          display: '-webkit-box',
          WebkitLineClamp: clampLines,
          WebkitBoxOrient: 'vertical',
          overflow: 'hidden',
        } as const)
      : ({
          overflow: 'hidden',
          textOverflow: 'ellipsis',
          whiteSpace: 'nowrap',
        } as const);

  return (
    <Box>
      <Typography
        ref={node => {
          textRef.current = node;
        }}
        variant="body2"
        sx={{
          ...WRAP_STYLES,
          ...(expanded ? {} : collapsedSx),
        }}>
        {text}
      </Typography>
      {(clipped || expanded) && (
        <Box
          component="span"
          data-copy-skip
          onClick={e => {
            e.stopPropagation();
            setExpanded(prev => !prev);
          }}
          sx={{
            cursor: 'pointer',
            typography: 'caption',
            color: 'primary.main',
            '&:hover': {textDecoration: 'underline'},
          }}>
          {expanded ? 'Show less' : 'Show more'}
        </Box>
      )}
    </Box>
  );
}
