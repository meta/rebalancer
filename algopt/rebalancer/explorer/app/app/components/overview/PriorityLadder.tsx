'use client';

import type {KeyboardEvent, MouseEvent} from 'react';

import {Box, Button, Link as MuiLink, Paper, Skeleton} from '@mui/material';
import {ChevronRight, Equal, Check, TriangleAlert} from 'lucide-react';
import Link from 'next/link';
import {useRouter} from 'next/navigation';

import PreciseNumber from '@/app/components/evaluation/PreciseNumber';
import {CompositionBar} from '@/app/components/ui/Bars';
import {ChangeIcon, changeTone} from '@/app/components/ui/Delta';
import HintIcon from '@/app/components/ui/HintIcon';
import Pill from '@/app/components/ui/Pill';
import SectionHeader from '@/app/components/ui/SectionHeader';
import {EmptyState, ErrorState} from '@/app/components/ui/StateViews';
import TupleBadge from '@/app/components/ui/TupleBadge';
import {constraintsObjectivesHref} from '@/app/components/overview/links';
import Sparkline from '@/app/components/overview/Sparkline';
import type {Loadable} from '@/app/components/overview/useRunOverviewData';
import {precise} from '@/lib/format';
import {
  profileTrace,
  stillAboveZero,
  type OverviewSummary,
  type TupleSummary,
} from '@/lib/overview';
import type {LocalSearchProfile} from '@/lib/rebalancer-explorer-types';
import {
  COLOR,
  DURATION,
  focusRing,
  radius,
  TABULAR,
  TEXT,
  WEIGHT,
} from '@/lib/ui-tokens';

// Objective names listed per row before "+N more".
const NAMES_SHOWN = 2;
// Objectives listed under "Still above 0" before "+N more".
const STILL_SHOWN = 5;

const DECIDES_HINT =
  'Rebalancer minimizes the objective tuple in index order: the first tuple index whose total differs decides which assignment is better.';

function counts(t: TupleSummary) {
  let better = 0;
  let worse = 0;
  for (const o of t.objectives) {
    if (o.dstValue < o.srcValue) {
      better++;
    } else if (o.dstValue > o.srcValue) {
      worse++;
    }
  }
  return {better, worse, unchanged: t.objectives.length - better - worse};
}

/**
 * The verdict: is Final better than Initial, decided at which tuple index,
 * and by how much.
 */
function Verdict({
  summary,
  maxTupleIndex,
}: {
  summary: OverviewSummary;
  maxTupleIndex: number;
}) {
  const t = summary.tuples.find(
    x => x.tupleIndex === summary.decidingTupleIndex,
  );
  const better = t != null && t.delta < 0;
  const tone =
    t == null
      ? {
          bg: COLOR.bgSubtle,
          border: COLOR.borderDefault,
          icon: COLOR.textTertiary,
        }
      : better
        ? {
            bg: COLOR.positiveBg,
            border: COLOR.positiveBorder,
            icon: COLOR.positive,
          }
        : {
            bg: COLOR.negativeBg,
            border: COLOR.negativeBorder,
            icon: COLOR.negative,
          };
  const Icon = t == null ? Equal : better ? Check : TriangleAlert;
  return (
    <Box
      role="status"
      sx={{
        display: 'flex',
        alignItems: 'flex-start',
        gap: 1.5,
        mx: 2,
        mb: 1.5,
        px: 2,
        py: 1.5,
        borderRadius: radius('lg'),
        border: `1px solid ${tone.border}`,
        bgcolor: tone.bg,
      }}>
      <Box
        component="span"
        aria-hidden
        sx={{display: 'flex', mt: '2px', color: tone.icon}}>
        <Icon size={16} strokeWidth={2.5} />
      </Box>
      <Box sx={{minWidth: 0}}>
        <Box
          sx={{
            display: 'flex',
            alignItems: 'center',
            flexWrap: 'wrap',
            gap: 1,
            fontSize: TEXT.lg,
            fontWeight: WEIGHT.heading,
            color: COLOR.textPrimary,
          }}>
          {t == null ? (
            'Initial and Final have equal totals at every tuple index'
          ) : (
            <>
              {`Final is ${better ? 'better' : 'worse'} than Initial — decided at`}
              <TupleBadge
                tupleIndex={t.tupleIndex}
                maxTupleIndex={maxTupleIndex}
                hint={DECIDES_HINT}
              />
            </>
          )}
        </Box>
        <Box sx={{mt: 0.25, color: COLOR.textSecondary}}>
          {t != null && (
            <>
              {t.objectives.map(o => o.name).join(', ')}{' '}
              <Box component="span" sx={TABULAR}>
                {precise(t.srcTotal)} → <b>{precise(t.dstTotal)}</b>
              </Box>{' '}
              <PreciseNumber value={t.delta} highlight signed />.{' '}
            </>
          )}
          Rebalancer compares these totals in index order, starting at 0: the
          first index whose total differs decides, and a higher index only
          breaks ties.
        </Box>
      </Box>
    </Box>
  );
}

const GRID = {
  display: 'grid',
  gridTemplateColumns:
    '76px minmax(0, 1.6fr) 120px minmax(220px, 1fr) 150px var(--trace-w) 16px',
  columnGap: 1.5,
  alignItems: 'center',
  px: 2,
  '@media (max-width: 760px)': {
    gridTemplateColumns: '76px minmax(0, 1fr) 96px 16px',
    '& .hide-sm': {display: 'none'},
  },
} as const;

/**
 * Ranked rows, one per tuple index in the order the solver compares them:
 * rank pill, objectives, the share that got better or worse, Initial → Final,
 * the change, the local-search trace and a chevron to that index on
 * Constraints & Objectives.
 */
export default function PriorityLadder({
  basePath,
  summary,
  profiles,
  hasFinalAssignment,
}: {
  basePath: string;
  summary: Loadable<OverviewSummary>;
  profiles: Loadable<LocalSearchProfile[]>;
  hasFinalAssignment: boolean;
}) {
  const router = useRouter();
  const s = summary.data;
  const traces = profiles.data ?? [];
  const showTraces = traces.some(p => p.moveTypeEvents.length > 0);
  const coHref = constraintsObjectivesHref(basePath, {dstBase: 'FINAL'});
  const maxTupleIndex =
    s != null && s.tuples.length > 0
      ? s.tuples[s.tuples.length - 1].tupleIndex
      : 0;
  const tupleHref = (tupleIndex: number) =>
    constraintsObjectivesHref(basePath, {dstBase: 'FINAL', tuple: tupleIndex});
  // A row opens its tuple's table on Constraints & Objectives; the objective
  // links inside it keep opening that objective.
  const onRowClick = (e: MouseEvent<HTMLElement>, href: string) => {
    if ((e.target as HTMLElement).closest('a, button') != null) {
      return;
    }
    router.push(href);
  };
  const onRowKeyDown = (e: KeyboardEvent<HTMLElement>, href: string) => {
    if ((e.key === 'Enter' || e.key === ' ') && e.target === e.currentTarget) {
      e.preventDefault();
      router.push(href);
    }
  };
  const still = s != null ? stillAboveZero(s) : [];

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
          <Skeleton key={i} variant="text" height={36} />
        ))}
      </Box>
    );
  } else if (s.tuples.length === 0) {
    body = <EmptyState compact title="This run has no objectives." />;
  } else {
    body = (
      <>
        <Verdict summary={s} maxTupleIndex={maxTupleIndex} />
        <Box
          role="table"
          aria-label="Objective totals by tuple index"
          sx={{'--trace-w': showTraces ? '110px' : '0px', overflowX: 'auto'}}>
          <Box
            role="row"
            sx={{
              ...GRID,
              py: 1,
              bgcolor: COLOR.bgSurface2,
              borderTop: `1px solid ${COLOR.borderDefault}`,
              borderBottom: `1px solid ${COLOR.borderDefault}`,
              color: COLOR.textTertiary,
              fontSize: TEXT.xs,
              fontWeight: WEIGHT.semibold,
              '& .num': {textAlign: 'right'},
            }}>
            <span role="columnheader">Tuple index</span>
            <span role="columnheader">Objectives</span>
            <span role="columnheader">Changed</span>
            <span role="columnheader" className="num hide-sm">
              Initial → Final
            </span>
            <span role="columnheader" className="num hide-sm">
              Final − Initial
            </span>
            <Box
              component="span"
              role="columnheader"
              className="hide-sm"
              sx={{
                display: showTraces ? 'inline-flex' : 'none',
                alignItems: 'center',
                gap: 0.5,
              }}>
              Search trace
              <HintIcon
                title="Objective value over local-search time for this goal index, from the run's local search profile. Local Search shows the full chart."
                label="Help: Search trace"
              />
            </Box>
            <span role="columnheader" aria-label="Open" />
          </Box>
          {s.tuples.map(t => {
            const profile = traces[t.tupleIndex];
            const trace = profile != null ? profileTrace(profile) : [];
            const shown = t.objectives.slice(0, NAMES_SHOWN);
            const more = t.objectives.length - shown.length;
            const decides = t.tupleIndex === s.decidingTupleIndex;
            const href = tupleHref(t.tupleIndex);
            const c = counts(t);
            const tone = changeTone(t.delta);
            return (
              <Box
                key={t.tupleIndex}
                role="row"
                tabIndex={0}
                aria-label={`Tuple index ${t.tupleIndex}: open its table on Constraints & Objectives`}
                onClick={e => onRowClick(e, href)}
                onKeyDown={e => onRowKeyDown(e, href)}
                sx={{
                  ...GRID,
                  py: 1.25,
                  cursor: 'pointer',
                  borderBottom: `1px solid ${COLOR.borderDefault}`,
                  '&:last-of-type': {borderBottom: 0},
                  '&:hover': {bgcolor: COLOR.bgSurface2},
                  '&:focus-visible': {...focusRing(-2), borderRadius: '6px'},
                  '&:hover .go, &:focus-visible .go': {
                    color: COLOR.action,
                    transform: 'translateX(2px)',
                  },
                }}>
                <Box role="cell">
                  <TupleBadge
                    tupleIndex={t.tupleIndex}
                    maxTupleIndex={maxTupleIndex}
                  />
                </Box>
                <Box role="cell" sx={{minWidth: 0}}>
                  <Box
                    sx={{
                      display: 'flex',
                      alignItems: 'center',
                      columnGap: 1,
                      minWidth: 0,
                    }}>
                    {shown.map(o => (
                      <MuiLink
                        key={o.id}
                        component={Link}
                        href={constraintsObjectivesHref(basePath, {
                          dstBase: 'FINAL',
                          name: o.name,
                          expressionId: o.id,
                        })}
                        title={`Open ${o.name} on Constraints & Objectives`}
                        underline="hover"
                        sx={{
                          minWidth: 0,
                          overflow: 'hidden',
                          textOverflow: 'ellipsis',
                          whiteSpace: 'nowrap',
                          fontWeight: WEIGHT.semibold,
                          color: o.changed
                            ? COLOR.textPrimary
                            : COLOR.textSecondary,
                        }}>
                        {o.name}
                      </MuiLink>
                    ))}
                    {more > 0 && (
                      <MuiLink
                        component={Link}
                        href={coHref}
                        sx={{flexShrink: 0, fontSize: TEXT.xs}}>
                        +{more} more
                      </MuiLink>
                    )}
                    {decides && (
                      <Pill tone="info" title={DECIDES_HINT}>
                        decides
                      </Pill>
                    )}
                  </Box>
                  <Box sx={{color: COLOR.textTertiary, fontSize: TEXT.xs}}>
                    {t.changedCount} of {t.objectives.length} changed
                    {c.worse > 0 && (
                      <>
                        {' · '}
                        <Box
                          component="span"
                          sx={{
                            color: COLOR.negativeText,
                            fontWeight: WEIGHT.semibold,
                          }}>
                          {c.worse} higher (worse)
                        </Box>
                      </>
                    )}
                  </Box>
                </Box>
                <Box role="cell">
                  <CompositionBar
                    {...c}
                    width={120}
                    label={`${c.better} lower, ${c.worse} higher, ${c.unchanged} unchanged`}
                  />
                </Box>
                <Box
                  role="cell"
                  className="hide-sm"
                  sx={{
                    textAlign: 'right',
                    whiteSpace: 'nowrap',
                    color: COLOR.textTertiary,
                    ...TABULAR,
                  }}>
                  {precise(t.srcTotal)}
                  <Box
                    component="span"
                    aria-label="to"
                    sx={{mx: 0.5, color: COLOR.textDecorative}}>
                    →
                  </Box>
                  <Box
                    component="b"
                    sx={{
                      color: COLOR.textPrimary,
                      fontWeight: WEIGHT.semibold,
                    }}>
                    {precise(t.dstTotal)}
                  </Box>
                </Box>
                <Box
                  role="cell"
                  className="hide-sm"
                  sx={{
                    display: 'flex',
                    alignItems: 'center',
                    justifyContent: 'flex-end',
                    gap: 0.75,
                  }}>
                  <ChangeIcon delta={t.delta} tone={tone} />
                  <PreciseNumber value={t.delta} highlight signed />
                </Box>
                <Box
                  role="cell"
                  className="hide-sm"
                  sx={{display: showTraces ? 'block' : 'none'}}>
                  <Sparkline
                    width={110}
                    points={trace}
                    label={`Goal index ${t.tupleIndex}: objective over local-search time`}
                  />
                </Box>
                <Box
                  role="cell"
                  className="go"
                  aria-hidden
                  sx={{
                    display: 'flex',
                    color: COLOR.textDecorative,
                    transition: `color ${DURATION.fast}ms, transform ${DURATION.fast}ms`,
                  }}>
                  <ChevronRight size={16} />
                </Box>
              </Box>
            );
          })}
        </Box>
      </>
    );
  }

  return (
    <Paper variant="outlined" sx={{overflow: 'hidden', minWidth: 0}}>
      <SectionHeader
        title="Objectives by tuple index"
        meta="Highest priority first"
        actions={
          <Button
            size="small"
            variant="outlined"
            component={Link}
            href={coHref}>
            Open Constraints & Objectives
          </Button>
        }
      />
      {body}
      {still.length > 0 && (
        <Box
          sx={{
            display: 'flex',
            alignItems: 'center',
            flexWrap: 'wrap',
            columnGap: 1.5,
            rowGap: 0.75,
            px: 2,
            py: 1.25,
            borderTop: `1px solid ${COLOR.borderDefault}`,
            color: COLOR.textTertiary,
            fontSize: TEXT.xs,
          }}>
          <Box
            component="span"
            sx={{display: 'inline-flex', alignItems: 'center', gap: 0.5}}>
            <Box
              component="span"
              sx={{color: COLOR.textSecondary, fontWeight: WEIGHT.semibold}}>
              Still above 0, unchanged
            </Box>
            <HintIcon
              title="Objectives whose value is above 0 at Final and did not change from Initial: the usual place to start when asking why the solver left something unsolved."
              label="Help: Still above 0, unchanged"
            />
          </Box>
          {still.slice(0, STILL_SHOWN).map(o => (
            <Box
              key={o.id}
              component="span"
              sx={{display: 'inline-flex', alignItems: 'center', gap: 0.75}}>
              <TupleBadge
                tupleIndex={o.tupleIndex}
                maxTupleIndex={maxTupleIndex}
                hint={null}
              />
              <MuiLink
                component={Link}
                href={constraintsObjectivesHref(basePath, {
                  dstBase: 'FINAL',
                  name: o.name,
                  expressionId: o.id,
                })}
                title={`Open ${o.name} on Constraints & Objectives`}
                sx={{fontSize: TEXT.sm}}>
                {o.name}
              </MuiLink>
              <Box
                component="span"
                sx={{color: COLOR.textPrimary, fontSize: TEXT.sm, ...TABULAR}}>
                {precise(o.dstValue)}
              </Box>
            </Box>
          ))}
          {still.length > STILL_SHOWN && (
            <MuiLink component={Link} href={coHref} sx={{fontSize: TEXT.sm}}>
              +{still.length - STILL_SHOWN} more
            </MuiLink>
          )}
        </Box>
      )}
    </Paper>
  );
}
