import type {ExpressionResult} from '@/lib/rebalancer-explorer-types';

export const PENALTY_HELP_TEXT =
  'Penalty: a tie-breaker local search compares only when objective values ' +
  'are equal, to reward progress the objective does not count yet. ' +
  'A penalty-only move changes no objective, only penalties, to escape a ' +
  'local optimum.';

/** True when any expression carries a separated local-search penalty. */
export function hasAnyPenalty(expressions: ExpressionResult[]): boolean {
  return expressions.some(expr => expr.penaltyValue != null);
}

/**
 * Whether a row survives "hide unchanged": its value changed, or its penalty
 * changed while the penalty columns are shown.
 */
export function isRowChanged(
  row: {delta: number; penaltyDelta: number},
  isPenaltyShown: boolean,
): boolean {
  return row.delta !== 0 || (isPenaltyShown && row.penaltyDelta !== 0);
}
