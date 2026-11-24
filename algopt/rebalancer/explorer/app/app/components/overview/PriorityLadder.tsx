'use client';

import {Box, Link as MuiLink, Paper, Skeleton, Tooltip} from '@mui/material';
import Link from 'next/link';

import ChartLegend from '@/app/components/ui/ChartLegend';
import Delta, {changeTone} from '@/app/components/ui/Delta';
import SectionHeader from '@/app/components/ui/SectionHeader';
import {EmptyState, ErrorState} from '@/app/components/ui/StateViews';
import TupleBadge from '@/app/components/ui/TupleBadge';
import {constraintsObjectivesHref} from '@/app/components/overview/links';
import Sparkline from '@/app/components/overview/Sparkline';
import type {Loadable} from '@/app/components/overview/useRunOverviewData';
import {
  formatCompact,
  formatPercentChange,
  formatSignedCompact,
  precise,
} from '@/lib/format';
import {
  thinSeries,
  tupleMix,
  tupleStepTotals,
  type MoveSetDeltas,
  type OverviewSummary,
  type TupleMix,
  type TupleSummary,
} from '@/lib/overview';
import {tupleColor} from '@/lib/priority-colors';
import {
  COLOR,
  focusRing,
  radius,
  TABULAR,
  TEXT,
  TEXT_DISABLED,
  WEIGHT,
} from '@/lib/ui-tokens';

// Tuple rows shown before "All N priorities".
const MAX_ROWS = 18;
// Objective names in a row's label before "+N".
const NAMES_SHOWN = 2;

const MIX: ReadonlyArray<{key: keyof TupleMix; label: string; color: string}> =
  [
    {key: 'improved', label: 'Improved', color: COLOR.positive},
    {key: 'regressed', label: 'Regressed', color: COLOR.negative},
    {
      key: 'unchangedNonzero',
      label: 'Unchanged, non-zero',
      color: COLOR.caution,
    },
    {key: 'zero', label: 'Zero (ideal)', color: COLOR.borderStrong},
  ];

const MIX_TIP: Record<keyof TupleMix, string> = {
  improved: 'improved',
  regressed: 'regressed',
  unchangedNonzero: 'unchanged but non-zero',
  zero: 'at zero (ideal)',
};

const ROW_GRID = {
  display: 'grid',
  gridTemplateColumns: '56px minmax(0, 1.4fr) minmax(0, 1fr) 150px 110px',
  columnGap: 1.5,
  alignItems: 'center',
  px: 2,
  '@media (max-width: 760px)': {
    gridTemplateColumns: '56px minmax(0, 1fr) 120px',
    '& .hide-sm': {display: 'none'},
  },
} as const;

function plural(n: number, one: string): string {
  return `${n} ${n === 1 ? one : `${one}s`}`;
}

/** The share of a tuple's objectives in each status, as one 8px bar. */
function MixBar({mix, total}: {mix: TupleMix; total: number}) {
  return (
    <Box
      role="img"
      aria-label={MIX.map(m => `${mix[m.key]} ${MIX_TIP[m.key]}`).join(', ')}
      sx={{
        display: 'flex',
        height: 8,
        borderRadius: radius('full'),
        overflow: 'hidden',
        bgcolor: COLOR.bgSubtle,
      }}>
      {MIX.filter(m => mix[m.key] > 0).map(m => (
        <Tooltip key={m.key} title={`${mix[m.key]} ${MIX_TIP[m.key]}`}>
          <Box
            sx={{
              width: `${(100 * mix[m.key]) / total}%`,
              height: '100%',
              bgcolor: m.color,
            }}
          />
        </Tooltip>
      ))}
    </Box>
  );
}

/** Initial → Final in short form, and the change with its percentage. */
function Values({t}: {t: TupleSummary}) {
  const tone = changeTone(t.delta);
  const pct = formatPercentChange(t.delta, t.srcTotal);
  return (
    <Tooltip
      title={`Initial ${precise(t.srcTotal)} → Final ${precise(t.dstTotal)}, change ${precise(t.delta)}`}>
      <Box
        sx={{
          textAlign: 'right',
          fontSize: TEXT.xs,
          whiteSpace: 'nowrap',
          ...TABULAR,
        }}>
        <Box component="span" sx={{color: COLOR.textTertiary}}>
          {formatCompact(t.srcTotal)}
        </Box>{' '}
        <Box component="span" aria-label="to" sx={{color: TEXT_DISABLED}}>
          →
        </Box>{' '}
        <Box component="b" sx={{fontWeight: WEIGHT.bold}}>
          {formatCompact(t.dstTotal)}
        </Box>
        <Box>
          <Delta tone={tone}>
            {tone === 'zero' ? 'no change' : formatSignedCompact(t.delta)}
            {pct !== '' && (
              <Box
                component="span"
                sx={{fontWeight: WEIGHT.medium, opacity: 0.85}}>
                {` (${pct})`}
              </Box>
            )}
          </Delta>
        </Box>
      </Box>
    </Tooltip>
  );
}

/**
 * The tuple's total after each move set, or "flat" when Initial and Final
 * are equal.
 */
function Trend({
  t,
  color,
  totals,
  loading,
}: {
  t: TupleSummary;
  color: string;
  totals: number[] | undefined;
  loading: boolean;
}) {
  if (t.delta === 0) {
    return (
      <Box component="span" sx={{color: TEXT_DISABLED, fontSize: TEXT.xs}}>
        flat
      </Box>
    );
  }
  if (loading) {
    return <Skeleton variant="rounded" width={104} height={26} />;
  }
  if (totals == null) {
    return null;
  }
  return (
    <Sparkline
      values={thinSeries(totals)}
      color={color}
      label={`Tuple index ${t.tupleIndex}: total after each of ${totals.length - 1} move sets`}
    />
  );
}

/**
 * The priority ladder: one row per objective tuple index, in the order the
 * solver compares them, with the status mix of its objectives, Initial →
 * Final and the total after each move set. A row opens its tuple on
 * Constraints & Objectives.
 */
export default function PriorityLadder({
  basePath,
  summary,
  moveSetDeltas,
  hasFinalAssignment,
}: {
  basePath: string;
  summary: Loadable<OverviewSummary>;
  moveSetDeltas: Loadable<MoveSetDeltas | null>;
  hasFinalAssignment: boolean;
}) {
  const s = summary.data;
  const maxTupleIndex =
    s != null && s.tuples.length > 0
      ? s.tuples[s.tuples.length - 1].tupleIndex
      : 0;
  const stepTotals =
    s != null && moveSetDeltas.data != null
      ? tupleStepTotals(s, moveSetDeltas.data)
      : null;

  let body;
  if (!hasFinalAssignment) {
    body = (
      <EmptyState title="No final assignment">
        This run has no final assignment, so there is nothing to compare.
        Constraints & Objectives evaluates the initial assignment on its own.
      </EmptyState>
    );
  } else if (summary.error != null) {
    body = (
      <ErrorState sx={{m: 2, mt: 0}} title="Could not evaluate the assignments">
        {summary.error}
      </ErrorState>
    );
  } else if (s == null) {
    body = (
      <Box sx={{px: 2, pb: 1.5}}>
        {[0, 1, 2, 3].map(i => (
          <Skeleton key={i} variant="text" height={44} />
        ))}
      </Box>
    );
  } else if (s.tuples.length === 0) {
    body = <EmptyState compact title="This run has no objectives." />;
  } else {
    body = (
      <Box>
        {s.tuples.slice(0, MAX_ROWS).map(t => {
          const names = t.objectives.map(o => o.name);
          const more = names.length - NAMES_SHOWN;
          const color = tupleColor(t.tupleIndex, maxTupleIndex);
          return (
            <Box
              key={t.tupleIndex}
              component={Link}
              href={constraintsObjectivesHref(basePath, {
                dstBase: 'FINAL',
                tuple: t.tupleIndex,
              })}
              sx={{
                ...ROW_GRID,
                py: '9px',
                color: 'inherit',
                textDecoration: 'none',
                borderBottom: `1px solid ${COLOR.borderDefault}`,
                '&:last-of-type': {borderBottom: 0},
                '&:hover': {bgcolor: COLOR.bgSurface2},
                '&:focus-visible': focusRing(-2),
              }}>
              <Box>
                <TupleBadge
                  tupleIndex={t.tupleIndex}
                  maxTupleIndex={maxTupleIndex}
                />
              </Box>
              <Box sx={{minWidth: 0}}>
                <Box
                  title={names.join(', ')}
                  sx={{
                    fontWeight: WEIGHT.semibold,
                    overflow: 'hidden',
                    textOverflow: 'ellipsis',
                    whiteSpace: 'nowrap',
                  }}>
                  {names.slice(0, NAMES_SHOWN).join(', ')}
                  {more > 0 && ` +${more}`}
                </Box>
                <Box sx={{color: COLOR.textTertiary, fontSize: TEXT.xs}}>
                  {plural(names.length, 'objective')}
                  {t.changedCount > 0 && ` · ${t.changedCount} changed`}
                </Box>
              </Box>
              <Box className="hide-sm">
                <MixBar mix={tupleMix(t)} total={names.length} />
              </Box>
              <Values t={t} />
              <Box className="hide-sm">
                <Trend
                  t={t}
                  color={color}
                  totals={stepTotals?.get(t.tupleIndex)}
                  loading={moveSetDeltas.loading}
                />
              </Box>
            </Box>
          );
        })}
      </Box>
    );
  }

  return (
    <Paper variant="outlined" sx={{overflow: 'hidden', minWidth: 0}}>
      <SectionHeader
        title="Priority ladder"
        meta="Objectives are minimized in strict priority order"
      />
      {body}
      {s != null && s.tuples.length > 0 && (
        <Box
          sx={{
            display: 'flex',
            alignItems: 'center',
            gap: 1,
            px: 2,
            py: 1.25,
            borderTop: `1px solid ${COLOR.borderDefault}`,
            color: COLOR.textTertiary,
            fontSize: TEXT.xs,
          }}>
          <ChartLegend
            payload={MIX.map(m => ({value: m.label, color: m.color}))}
            sx={{p: 0}}
          />
          {s.tuples.length > MAX_ROWS && (
            <MuiLink
              component={Link}
              href={constraintsObjectivesHref(basePath, {dstBase: 'FINAL'})}
              sx={{ml: 'auto', fontSize: TEXT.xs}}>
              All {s.tuples.length} priorities →
            </MuiLink>
          )}
        </Box>
      )}
    </Paper>
  );
}
