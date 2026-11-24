'use client';

import {useEffect, useMemo, useRef, useState} from 'react';

import {
  Box,
  Dialog,
  IconButton,
  InputBase,
  Tooltip,
  Typography,
} from '@mui/material';
import {Search} from 'lucide-react';
import {useParams, useRouter} from 'next/navigation';

import Kbd from '@/app/components/ui/Kbd';
import {
  buildCommandItems,
  filterCommandItems,
  type CommandGroup,
  type CommandItem,
} from '@/lib/command-items';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {decodeRunId, encodeRunId} from '@/lib/run-id';
import {
  COLOR,
  CONTROL,
  DURATION,
  EYEBROW_TEXT,
  radius,
  TEXT,
  WEIGHT,
} from '@/lib/ui-tokens';

// Result group headings, in the order the results list them.
const GROUP_LABEL: Record<CommandGroup, string> = {
  Page: 'Pages',
  Entity: 'Entity tables',
  Metric: 'Metrics',
  Objective: 'Objectives',
};

function isMac(): boolean {
  return (
    typeof navigator !== 'undefined' &&
    /Mac|iPhone|iPad/.test(navigator.platform)
  );
}

/**
 * "Jump to" for a run: Ctrl/⌘ K (or the header button) opens a search over
 * every page, entity table, metric collection and objective of the run, built
 * from the metadata with no RPC. Arrow keys move, Enter opens, Esc closes.
 * It adds a route to pages the sidebar already reaches; nothing moves.
 */
export default function CommandPalette() {
  const {metadata} = useProblemMetadata();
  const router = useRouter();
  const params = useParams<{runId: string}>();
  const [open, setOpen] = useState(false);
  const [query, setQuery] = useState('');
  const [active, setActive] = useState(0);
  const listRef = useRef<HTMLUListElement>(null);
  const inputRef = useRef<HTMLInputElement>(null);
  const shortcut = isMac() ? '⌘K' : 'Ctrl K';

  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key.toLowerCase() === 'k' && (e.metaKey || e.ctrlKey)) {
        e.preventDefault();
        setOpen(prev => !prev);
      }
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, []);

  const basePath = `/run/${encodeRunId(decodeRunId(params.runId))}`;
  const items = useMemo(
    () => (metadata == null ? [] : buildCommandItems(basePath, metadata)),
    [basePath, metadata],
  );
  // Grouped under their headings; within a group the filter's order (labels
  // that start with the query first) stays.
  const results = useMemo(() => {
    const found = filterCommandItems(items, query);
    const order = Object.keys(GROUP_LABEL);
    return [...found].sort(
      (a, b) => order.indexOf(a.group) - order.indexOf(b.group),
    );
  }, [items, query]);

  const close = () => {
    setOpen(false);
    setQuery('');
    setActive(0);
  };
  const go = (item: CommandItem | undefined) => {
    if (item == null) {
      return;
    }
    close();
    router.push(item.href);
  };

  useEffect(() => {
    listRef.current
      ?.querySelector(`[data-index="${active}"]`)
      ?.scrollIntoView({block: 'nearest'});
  }, [active]);

  if (metadata == null) {
    return null;
  }

  return (
    <>
      {/* Search box of the top bar: a labeled button with its shortcut, so the
          search is found without knowing the shortcut; an icon on phones. */}
      <Tooltip title="Jump to a page, entity table, metric or objective">
        <Box
          component="button"
          type="button"
          aria-label="Jump to"
          aria-keyshortcuts="Control+K Meta+K"
          onClick={() => setOpen(true)}
          sx={{
            display: 'flex',
            alignItems: 'center',
            gap: 1,
            flexShrink: 1,
            minWidth: 0,
            width: 340,
            maxWidth: '32vw',
            height: CONTROL.md,
            px: 1.25,
            border: `1px solid ${COLOR.borderDefault}`,
            borderRadius: radius('md'),
            bgcolor: COLOR.bgSurface2,
            color: COLOR.textTertiary,
            font: 'inherit',
            cursor: 'text',
            textAlign: 'left',
            transition: `border-color ${DURATION.fast}ms`,
            '&:hover': {borderColor: COLOR.borderStrong},
            '&:focus-visible': {
              outline: `2px solid ${COLOR.action}`,
              outlineOffset: 1,
            },
            '@media (max-width: 1180px)': {width: 200},
            '@media (max-width: 760px)': {display: 'none'},
          }}>
          <Search size={15} style={{flexShrink: 0}} />
          <Box
            component="span"
            sx={{
              minWidth: 0,
              whiteSpace: 'nowrap',
              overflow: 'hidden',
              textOverflow: 'ellipsis',
            }}>
            Jump to a page, table, metric or objective…
          </Box>
          <Box sx={{flex: 1}} />
          <Kbd>{shortcut}</Kbd>
        </Box>
      </Tooltip>
      <Tooltip title={`Jump to a page, table or objective (${shortcut})`}>
        <IconButton
          aria-label="Jump to"
          aria-keyshortcuts="Control+K Meta+K"
          onClick={() => setOpen(true)}
          sx={{
            display: 'none',
            '@media (max-width: 760px)': {display: 'grid'},
          }}>
          <Search size={18} />
        </IconButton>
      </Tooltip>
      <Dialog
        open={open}
        onClose={close}
        fullWidth
        maxWidth="sm"
        aria-label="Jump to"
        PaperProps={{
          sx: {alignSelf: 'flex-start', mt: '12vh', width: 'min(680px, 94vw)'},
        }}
        // Focus again once open: a closing detail pane returns focus to its
        // opener at the end of its own transition, after autoFocus ran.
        TransitionProps={{onEntered: () => inputRef.current?.focus()}}>
        <Box
          sx={{
            display: 'flex',
            alignItems: 'center',
            gap: 1.25,
            px: 2,
            py: 1.5,
            borderBottom: `1px solid ${COLOR.borderDefault}`,
          }}>
          <Box sx={{color: COLOR.textTertiary, display: 'flex'}}>
            <Search size={16} />
          </Box>
          <InputBase
            sx={{fontSize: TEXT.lg}}
            autoFocus
            inputRef={inputRef}
            fullWidth
            placeholder="Jump to a page, entity table, metric or objective"
            value={query}
            onChange={e => {
              setQuery(e.target.value);
              setActive(0);
            }}
            onKeyDown={e => {
              if (e.key === 'ArrowDown') {
                e.preventDefault();
                setActive(i => Math.min(i + 1, results.length - 1));
              } else if (e.key === 'ArrowUp') {
                e.preventDefault();
                setActive(i => Math.max(i - 1, 0));
              } else if (e.key === 'Enter') {
                e.preventDefault();
                go(results[active]);
              }
            }}
            inputProps={{
              'aria-label': 'Search',
              role: 'combobox',
              'aria-expanded': true,
              'aria-controls': 'rbx-command-list',
              'aria-activedescendant':
                results.length > 0 ? `rbx-command-${active}` : undefined,
            }}
          />
          <Kbd>Esc</Kbd>
        </Box>
        <Box
          component="ul"
          id="rbx-command-list"
          role="listbox"
          ref={listRef}
          sx={{
            listStyle: 'none',
            m: 0,
            p: 0.75,
            maxHeight: '56vh',
            overflowY: 'auto',
          }}>
          {results.length === 0 && (
            <Typography
              component="li"
              variant="body2"
              sx={{px: 1.5, py: 2, color: COLOR.textTertiary}}>
              Nothing matches “{query}”.
            </Typography>
          )}
          {results.map((item, i) => (
            <Box component="li" key={`${item.group}:${item.label}`} role="none">
              {(i === 0 || results[i - 1].group !== item.group) && (
                <Box
                  role="presentation"
                  sx={{...EYEBROW_TEXT, px: 1.5, pt: 1.25, pb: 0.5}}>
                  {GROUP_LABEL[item.group]}
                </Box>
              )}
              <Box
                id={`rbx-command-${i}`}
                data-index={i}
                role="option"
                aria-selected={i === active}
                onMouseMove={() => setActive(i)}
                onClick={() => go(item)}
                sx={{
                  display: 'flex',
                  alignItems: 'center',
                  gap: 1.25,
                  minWidth: 0,
                  px: 1.5,
                  py: 1,
                  borderRadius: radius('md'),
                  cursor: 'pointer',
                  bgcolor: i === active ? COLOR.actionSubtle : 'transparent',
                }}>
                <Box
                  component="span"
                  title={item.label}
                  sx={{
                    minWidth: 0,
                    fontWeight: WEIGHT.semibold,
                    overflow: 'hidden',
                    textOverflow: 'ellipsis',
                    whiteSpace: 'nowrap',
                  }}>
                  {item.label}
                </Box>
                {item.detail != null && (
                  <Box
                    component="span"
                    sx={{
                      ml: 'auto',
                      maxWidth: '45%',
                      flexShrink: 0,
                      color: COLOR.textTertiary,
                      fontSize: TEXT.xs,
                      overflow: 'hidden',
                      textOverflow: 'ellipsis',
                      whiteSpace: 'nowrap',
                    }}>
                    {item.detail}
                  </Box>
                )}
              </Box>
            </Box>
          ))}
        </Box>
        <Box
          sx={{
            display: 'flex',
            alignItems: 'center',
            flexWrap: 'wrap',
            gap: 1.75,
            px: 1.75,
            py: 1,
            borderTop: `1px solid ${COLOR.borderDefault}`,
            color: COLOR.textTertiary,
            fontSize: TEXT.xs,
            '& .k': {display: 'inline-flex', alignItems: 'center', gap: 0.75},
          }}>
          <span className="k">
            <Kbd>↑↓</Kbd> navigate
          </span>
          <span className="k">
            <Kbd>↵</Kbd> open
          </span>
          <span className="k">
            <Kbd>esc</Kbd> close
          </span>
          <Box component="span" className="k" sx={{ml: 'auto'}}>
            <Kbd>{isMac() ? '⌘B' : 'Ctrl B'}</Kbd> sidebar · <Kbd>?</Kbd> all
            shortcuts
          </Box>
        </Box>
      </Dialog>
    </>
  );
}
