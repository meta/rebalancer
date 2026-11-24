'use client';

import {Box, Tooltip} from '@mui/material';

import Pill from '@/app/components/ui/Pill';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {formatRuntime} from '@/lib/format';
import {COLOR, TEXT, type ToneName} from '@/lib/ui-tokens';

/** Tone for a solver end reason; anything not listed is neutral. */
export function endReasonTone(endReason: string): ToneName {
  switch (endReason) {
    case 'HIT_TIME_LIMIT':
    case 'HIT_MOVE_LIMIT':
    case 'HIT_STAGE_MOVE_LIMIT':
    case 'HIT_PLATEAU_TIME':
    case 'HIT_MIN_CYCLE_OBJECTIVE_IMPROVEMENT':
      return 'warn';
    case 'OPTIMAL':
    case 'UNABLE_TO_FIND_MORE_MOVES':
    case 'UNABLE_TO_FIND_IMPROVING_MOVES':
      return 'good';
    case 'NO_SOLUTION_FOUND':
    case 'HIT_EXCEPTION':
      return 'bad';
    default:
      return 'neutral';
  }
}

/**
 * What a solver end reason (Rebalancer's `EndReason` enum) means, in plain
 * words; null for a value this table does not know.
 */
export function endReasonMeaning(endReason: string): string | null {
  switch (endReason) {
    case 'OPTIMAL':
      return 'The solver proved that no better assignment exists for this problem.';
    case 'HIT_TIME_LIMIT':
      return 'Stopped at its time limit. More improving moves may exist.';
    case 'HIT_MOVE_LIMIT':
      return 'Stopped at its move limit. More improving moves may exist.';
    case 'HIT_STAGE_MOVE_LIMIT':
      return 'A stage stopped at its move limit. More improving moves may exist.';
    case 'HIT_STOP_CONSTRAINT':
      return 'Stopped because a configured stop condition was met.';
    case 'HIT_PLATEAU_TIME':
      return 'Stopped after the score stopped improving for the configured plateau time.';
    case 'HIT_MIN_CYCLE_OBJECTIVE_IMPROVEMENT':
      return 'Stopped because a search cycle improved the objective by less than the configured minimum.';
    case 'NO_SOLUTION_FOUND':
      return 'The solver found no solution.';
    case 'HIT_EXCEPTION':
      return 'The solve ended with an error.';
    case 'UNABLE_TO_FIND_MORE_MOVES':
      return 'Converged: the search found no further move that improves the score.';
    case 'UNABLE_TO_FIND_IMPROVING_MOVES':
      return 'The search found no move that improves the score.';
    default:
      return null;
  }
}

/**
 * The run bar's status: the solver end reason as a status pill and the total
 * runtime, verbatim from the problem metadata.
 */
export default function RunStatus() {
  const {metadata} = useProblemMetadata();
  if (metadata == null) {
    return null;
  }
  return (
    <Box
      aria-label="Run status"
      sx={{
        display: 'flex',
        alignItems: 'center',
        gap: 1,
        minWidth: 0,
        whiteSpace: 'nowrap',
        color: COLOR.textTertiary,
        fontSize: TEXT.xs,
      }}>
      <Tooltip
        title={
          endReasonMeaning(metadata.solverEndReason) ?? 'Solver end reason'
        }>
        <span>
          <Pill tone={endReasonTone(metadata.solverEndReason)}>
            {metadata.solverEndReason}
          </Pill>
        </span>
      </Tooltip>
      <Tooltip title={`Total runtime: ${metadata.totalRuntime} s`}>
        <span>{formatRuntime(metadata.totalRuntime)}</span>
      </Tooltip>
    </Box>
  );
}
