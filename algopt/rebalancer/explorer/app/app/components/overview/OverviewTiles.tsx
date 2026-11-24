'use client';

import {Box, Skeleton, Tooltip} from '@mui/material';

import {
  endReasonMeaning,
  endReasonTone,
} from '@/app/components/shell/RunStatus';
import KpiCard, {KpiValue, type KpiTone} from '@/app/components/ui/KpiCard';
import type {Loadable} from '@/app/components/overview/useRunOverviewData';
import {formatCount, formatRuntime} from '@/lib/format';
import type {OverviewSummary} from '@/lib/overview';
import type {ProblemMetadata} from '@/lib/rebalancer-explorer-types';
import {
  COLOR,
  MONO_TEXT,
  TEXT,
  TRACKING,
  type ToneName,
  WEIGHT,
} from '@/lib/ui-tokens';

/**
 * The end reason as a word KPI; the verbatim `EndReason` value stays on the
 * card. Null for a value this table does not know.
 */
export function endReasonWords(endReason: string): string | null {
  switch (endReason) {
    case 'OPTIMAL':
      return 'Optimal';
    case 'HIT_TIME_LIMIT':
      return 'Time limit';
    case 'HIT_MOVE_LIMIT':
      return 'Move limit';
    case 'HIT_STAGE_MOVE_LIMIT':
      return 'Stage move limit';
    case 'HIT_STOP_CONSTRAINT':
      return 'Stop condition';
    case 'HIT_PLATEAU_TIME':
      return 'Plateau';
    case 'HIT_MIN_CYCLE_OBJECTIVE_IMPROVEMENT':
      return 'Small improvement';
    case 'NO_SOLUTION_FOUND':
      return 'No solution';
    case 'HIT_EXCEPTION':
      return 'Error';
    case 'UNABLE_TO_FIND_MORE_MOVES':
      return 'Converged';
    case 'UNABLE_TO_FIND_IMPROVING_MOVES':
      return 'No improving move';
    default:
      return null;
  }
}

function kpiTone(tone: ToneName | null | undefined): KpiTone | null {
  return tone === 'good' || tone === 'warn' || tone === 'bad' ? tone : null;
}

/** A word KPI: 20px / 650. */
function WordValue({children}: {children: React.ReactNode}) {
  return (
    <Box
      component="span"
      sx={{
        fontSize: TEXT['2xl'],
        fontWeight: WEIGHT.heading,
        letterSpacing: TRACKING['2xl'],
        lineHeight: 1.6,
        color: COLOR.textPrimary,
        overflowWrap: 'anywhere',
      }}>
      {children}
    </Box>
  );
}

function Unit({children}: {children: React.ReactNode}) {
  return (
    <Box
      component="span"
      sx={{
        fontSize: TEXT.sm,
        fontWeight: WEIGHT.medium,
        color: COLOR.textTertiary,
      }}>
      {children}
    </Box>
  );
}

function Pending({error}: {error: string | null}) {
  if (error != null) {
    return <Unit>Not available</Unit>;
  }
  return <Skeleton variant="text" width={96} height={32} />;
}

/** Four headline numbers: outcome, move sets, constraints and objectives. */
export default function OverviewTiles({
  metadata,
  summary,
  moveSetCount,
  coHref,
}: {
  metadata: ProblemMetadata;
  summary: Loadable<OverviewSummary>;
  moveSetCount: Loadable<number>;
  coHref: string;
}) {
  const hasFinal = metadata.hasFinalAssignment;
  const s = summary.data;
  const reason = metadata.solverEndReason;
  const meaning = endReasonMeaning(reason);
  const noFinal = <Unit>No final assignment</Unit>;
  return (
    <Box
      sx={{
        display: 'grid',
        gridTemplateColumns: 'repeat(4, minmax(0, 1fr))',
        gap: 2,
        '@media (max-width: 1180px)': {
          gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
        },
        '@media (max-width: 760px)': {gridTemplateColumns: '1fr'},
      }}>
      <KpiCard
        label="Outcome"
        hint={`${meaning != null ? `${meaning} ` : ''}Solver end reason, total runtime and solver type, from the run metadata.`}
        tone={kpiTone(endReasonTone(reason))}
        sub={
          <>
            <Tooltip title="Solver end reason (EndReason)">
              <Box
                component="span"
                sx={{
                  ...MONO_TEXT,
                  fontSize: TEXT['2xs'],
                  display: 'block',
                  overflow: 'hidden',
                  textOverflow: 'ellipsis',
                  whiteSpace: 'nowrap',
                }}>
                {reason}
              </Box>
            </Tooltip>
            {`${formatRuntime(metadata.totalRuntime)} · ${metadata.solverType}`}
          </>
        }>
        <WordValue>{endReasonWords(reason) ?? reason}</WordValue>
      </KpiCard>

      <KpiCard
        label="Move sets"
        hint="Move sets between the initial and final assignments, the rows of the Moves table on Constraints & Objectives."
        href={hasFinal ? coHref : undefined}
        sub={
          metadata.numSteps != null
            ? `${formatCount(metadata.numSteps)} solver steps`
            : 'Initial → Final'
        }>
        {!hasFinal ? (
          noFinal
        ) : moveSetCount.data != null ? (
          <KpiValue>{formatCount(moveSetCount.data)}</KpiValue>
        ) : (
          <Pending error={moveSetCount.error} />
        )}
      </KpiCard>

      <KpiCard
        label="Constraints"
        hint="Constraints whose value is nonzero in the final assignment."
        href={hasFinal ? coHref : undefined}
        tone={
          hasFinal && s != null
            ? s.constraints.nonzeroDst > 0
              ? 'bad'
              : 'good'
            : null
        }
        sub={
          s != null
            ? `${formatCount(s.constraints.nonzeroSrc)} nonzero at Initial`
            : undefined
        }>
        {!hasFinal ? (
          noFinal
        ) : s != null ? (
          <>
            <KpiValue
              color={
                s.constraints.nonzeroDst > 0
                  ? COLOR.negativeText
                  : COLOR.positiveText
              }>
              {formatCount(s.constraints.nonzeroDst)}
            </KpiValue>
            <Unit>nonzero of {formatCount(s.constraints.total)} at Final</Unit>
          </>
        ) : (
          <Pending error={summary.error} />
        )}
      </KpiCard>

      <KpiCard
        label="Objectives"
        hint="Objectives whose value changed between Initial and Final. Rebalancer minimizes every objective, so lower is better. A higher value is allowed only when an objective at a lower tuple index improved first."
        href={hasFinal ? coHref : undefined}
        tone={
          hasFinal && s != null
            ? s.objectives.worse > 0
              ? 'warn'
              : s.objectives.improved > 0
                ? 'good'
                : null
            : null
        }
        sub={
          s != null
            ? `${formatCount(s.objectives.unchanged)} unchanged of ${formatCount(s.objectives.total)}`
            : undefined
        }>
        {!hasFinal ? (
          noFinal
        ) : s != null ? (
          <>
            <KpiValue color={COLOR.positiveText}>
              {formatCount(s.objectives.improved)}
            </KpiValue>
            <Unit>lower (better)</Unit>
            <KpiValue
              color={s.objectives.worse > 0 ? COLOR.negativeText : undefined}>
              {formatCount(s.objectives.worse)}
            </KpiValue>
            <Unit>higher (worse)</Unit>
          </>
        ) : (
          <Pending error={summary.error} />
        )}
      </KpiCard>
    </Box>
  );
}
