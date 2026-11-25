'use client';

import {useState} from 'react';

import {Box, Button, Typography} from '@mui/material';

import EntityLink from '@/app/components/detail-pane/EntityLink';
import {formatCount} from '@/lib/format';
import type {Flow} from '@/lib/run-changes';
import {
  COLOR,
  focusRing,
  radius,
  SHADOW,
  TABULAR,
  TEXT,
  WEIGHT,
} from '@/lib/ui-tokens';

const PREVIEW = 4;

/**
 * Where the moved objects went, as flows between the items of one scope (or
 * containers), most objects first. Each item opens its panel; a row narrows
 * the list below to that flow.
 */
export default function FlowsCard({
  flows,
  itemEntity,
  itemsLabel,
  objectNoun,
  onPick,
  isPicked,
}: {
  flows: readonly Flow[];
  itemEntity: string;
  // "container rows", "reservation items".
  itemsLabel: string;
  objectNoun: string;
  onPick: (flow: Flow) => void;
  isPicked: (flow: Flow) => boolean;
}) {
  const [showAll, setShowAll] = useState(false);
  if (flows.length === 0) {
    return null;
  }
  const moved = flows.reduce((n, f) => n + f.count, 0);
  const max = flows[0].count;
  const shown = showAll ? flows : flows.slice(0, PREVIEW);
  return (
    <Box
      component="section"
      aria-label="Where they moved"
      sx={{
        mb: 2,
        border: `1px solid ${COLOR.borderDefault}`,
        borderRadius: radius('lg'),
        bgcolor: COLOR.bgSurface,
        boxShadow: SHADOW.sm,
      }}>
      <Box
        sx={{
          display: 'flex',
          alignItems: 'baseline',
          flexWrap: 'wrap',
          columnGap: 1.25,
          px: 2,
          pt: 1.5,
          pb: 1,
        }}>
        <Typography variant="h6" component="h2">
          Where they moved
        </Typography>
        <Typography
          component="span"
          sx={{fontSize: TEXT.xs, color: COLOR.textTertiary}}>
          {`${formatCount(moved)} ${objectNoun} moved between ${itemsLabel}, Initial → Final · ${formatCount(flows.length)} ${flows.length === 1 ? 'flow' : 'flows'} · pick one to filter the list`}
        </Typography>
      </Box>
      <Box
        sx={{
          px: 1,
          pb: 1,
          display: 'grid',
          gridTemplateColumns:
            'minmax(0, max-content) 16px minmax(0, max-content) minmax(80px, 240px) 48px 1fr',
          columnGap: 1,
        }}>
        {shown.map(f => (
          <Box
            key={`${f.src}\u0000${f.dst}`}
            role="button"
            tabIndex={0}
            aria-pressed={isPicked(f)}
            aria-label={`List the ${formatCount(f.count)} that moved from ${f.src} to ${f.dst}`}
            onClick={() => onPick(f)}
            onKeyDown={e => {
              if (e.key === 'Enter' || e.key === ' ') {
                e.preventDefault();
                onPick(f);
              }
            }}
            sx={{
              gridColumn: '1 / -1',
              display: 'grid',
              gridTemplateColumns: 'subgrid',
              alignItems: 'center',
              px: 1,
              py: 0.75,
              borderRadius: radius('md'),
              fontSize: TEXT.sm,
              cursor: 'pointer',
              bgcolor: isPicked(f) ? COLOR.actionSubtle : undefined,
              '&:hover': {
                bgcolor: isPicked(f) ? COLOR.actionSubtle : COLOR.bgSurface2,
              },
              '&:focus-visible': focusRing(-2),
            }}>
            <Box component="span" sx={{minWidth: 0}}>
              {f.src !== '' ? (
                <EntityLink entity={itemEntity} id={f.src} />
              ) : (
                '—'
              )}
            </Box>
            <Box
              component="span"
              aria-hidden
              sx={{color: COLOR.textDecorative, textAlign: 'center'}}>
              →
            </Box>
            <Box component="span" sx={{minWidth: 0}}>
              {f.dst !== '' ? (
                <EntityLink entity={itemEntity} id={f.dst} />
              ) : (
                '—'
              )}
            </Box>
            <Box
              aria-hidden
              sx={{
                ml: 2,
                height: 6,
                borderRadius: radius('full'),
                bgcolor: COLOR.bgSubtle,
                overflow: 'hidden',
              }}>
              <Box
                sx={{
                  width: `${Math.max(2, (100 * f.count) / max)}%`,
                  height: '100%',
                  borderRadius: radius('full'),
                  bgcolor: COLOR.borderStrong,
                }}
              />
            </Box>
            <Box
              component="span"
              sx={{
                textAlign: 'right',
                fontWeight: WEIGHT.semibold,
                ...TABULAR,
              }}>
              {formatCount(f.count)}
            </Box>
          </Box>
        ))}
        {flows.length > PREVIEW && (
          <Button
            size="small"
            onClick={() => setShowAll(v => !v)}
            sx={{
              gridColumn: '1 / -1',
              justifySelf: 'start',
              ml: 0.5,
              mt: 0.25,
            }}>
            {showAll
              ? 'Show fewer'
              : `Show all ${formatCount(flows.length)} flows`}
          </Button>
        )}
      </Box>
    </Box>
  );
}
