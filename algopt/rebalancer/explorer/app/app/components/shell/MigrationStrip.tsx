'use client';

import {Box, IconButton, Tooltip} from '@mui/material';
import {MoveRight, X} from 'lucide-react';

import {setPref, usePref} from '@/lib/prefs';
import {COLOR, radius, SHADOW_SM, TEXT, WEIGHT} from '@/lib/ui-tokens';

const PREF = 'rbx-where-things-moved';

/** Shows the strip again (the keyboard-shortcuts list links here). */
export function showWhereThingsMoved(): void {
  setPref(PREF, 'shown');
}

// Old place → new place, for the things the shell moved.
const MOVES: ReadonlyArray<[string, string]> = [
  ['Breadcrumb', 'Run bar'],
  ['Run status', 'Run bar'],
  ['Page name', 'Page heading'],
  ['A / B summary', 'Compare'],
];

/**
 * "Where things moved": shown once until dismissed, reopened from the
 * keyboard-shortcuts list. Old names in tertiary, new ones in action.
 */
export default function MigrationStrip() {
  // Hidden in the server render; the stored choice applies on the client.
  const open = usePref(PREF, 'dismissed') !== 'dismissed';
  if (!open) {
    return null;
  }
  return (
    <Box
      component="section"
      aria-label="Where things moved"
      sx={{
        border: `1px solid ${COLOR.actionSubtleBorder}`,
        background: `linear-gradient(180deg, ${COLOR.actionSubtle}, ${COLOR.bgSurface} 90%)`,
        borderRadius: radius('lg'),
        boxShadow: SHADOW_SM,
        py: 1.25,
        pl: 1.75,
        pr: 1,
      }}>
      <Box
        sx={{display: 'flex', alignItems: 'center', gap: 1, flexWrap: 'wrap'}}>
        <Box
          component="span"
          aria-hidden
          sx={{display: 'inline-flex', color: COLOR.action}}>
          <MoveRight size={16} />
        </Box>
        <Box component="b" sx={{fontWeight: WEIGHT.semibold}}>
          Same pages, new layout.
        </Box>
        <Box component="span" sx={{color: COLOR.textTertiary}}>
          The header is now the top bar and the run bar below it:
        </Box>
        <Box sx={{flex: 1}} />
        <Tooltip title="Dismiss (? lists it again)">
          <IconButton
            size="small"
            aria-label="Dismiss where things moved"
            onClick={() => setPref(PREF, 'dismissed')}>
            <X size={14} />
          </IconButton>
        </Tooltip>
      </Box>
      <Box
        component="ul"
        sx={{
          listStyle: 'none',
          m: 0,
          mt: 0.5,
          p: 0,
          display: 'flex',
          flexWrap: 'wrap',
          gap: 0.75,
        }}>
        {MOVES.map(([from, to]) => (
          <Box
            component="li"
            key={from}
            sx={{
              display: 'inline-flex',
              alignItems: 'center',
              gap: 0.75,
              height: 26,
              px: 1.25,
              borderRadius: radius('full'),
              border: `1px solid ${COLOR.borderDefault}`,
              bgcolor: COLOR.bgSurface,
              color: COLOR.textSecondary,
              fontSize: TEXT.xs,
            }}>
            <Box component="span" sx={{color: COLOR.textTertiary}}>
              {from}
            </Box>
            <Box
              component="span"
              aria-label="moved to"
              sx={{color: COLOR.textDecorative}}>
              →
            </Box>
            <Box
              component="span"
              sx={{color: COLOR.actionHover, fontWeight: WEIGHT.semibold}}>
              {to}
            </Box>
          </Box>
        ))}
      </Box>
    </Box>
  );
}
