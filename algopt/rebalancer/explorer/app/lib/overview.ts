import type {
  ExpressionResult,
  LocalSearchProfile,
} from './rebalancer-explorer-types';
import {ExpressionType} from './rebalancer-explorer-types';

/** One objective of a tuple, with its value at A and at B. */
export interface TupleObjective {
  id: number;
  name: string;
  changed: boolean;
  srcValue: number;
  dstValue: number;
}

/** One objective tuple index, Initial (A) vs Final (B). */
export interface TupleSummary {
  tupleIndex: number;
  // Objectives in this tuple, in evaluation order.
  objectives: TupleObjective[];
  srcTotal: number;
  dstTotal: number;
  // dstTotal - srcTotal; exactly 0 when no objective in the tuple changed.
  delta: number;
  changedCount: number;
}

export interface ExpressionCounts {
  total: number;
  // Rebalancer minimizes: lower at B is an improvement.
  improved: number;
  worse: number;
  unchanged: number;
}

export interface ConstraintCounts {
  total: number;
  nonzeroSrc: number;
  nonzeroDst: number;
}

export interface OverviewSummary {
  tuples: TupleSummary[];
  objectives: ExpressionCounts;
  constraints: ConstraintCounts;
  // First tuple index whose total differs between A and B, or null on a tie.
  decidingTupleIndex: number | null;
}

/**
 * Per-tuple totals and counts for two evaluations. Expressions are joined by
 * id; one missing from either side is skipped. A tuple whose objectives are
 * all bit-identical gets a delta of exactly 0, so summation order cannot make
 * an unchanged tuple look changed.
 */
export function summarizeEvaluations(
  src: readonly ExpressionResult[],
  dst: readonly ExpressionResult[],
): OverviewSummary {
  const dstById = new Map<number, ExpressionResult>();
  for (const e of dst) {
    dstById.set(e.id, e);
  }

  const byTuple = new Map<number, TupleSummary>();
  const objectives: ExpressionCounts = {
    total: 0,
    improved: 0,
    worse: 0,
    unchanged: 0,
  };
  const constraints: ConstraintCounts = {
    total: 0,
    nonzeroSrc: 0,
    nonzeroDst: 0,
  };

  for (const a of src) {
    const b = dstById.get(a.id);
    if (b == null) {
      continue;
    }
    if (a.type === ExpressionType.CONSTRAINT) {
      constraints.total++;
      if (a.value !== 0) {
        constraints.nonzeroSrc++;
      }
      if (b.value !== 0) {
        constraints.nonzeroDst++;
      }
      continue;
    }
    const tupleIndex = a.tupleIndex ?? 0;
    let tuple = byTuple.get(tupleIndex);
    if (tuple == null) {
      tuple = {
        tupleIndex,
        objectives: [],
        srcTotal: 0,
        dstTotal: 0,
        delta: 0,
        changedCount: 0,
      };
      byTuple.set(tupleIndex, tuple);
    }
    const changed = a.value !== b.value;
    tuple.objectives.push({
      id: a.id,
      name: a.name,
      changed,
      srcValue: a.value,
      dstValue: b.value,
    });
    tuple.srcTotal += a.value;
    tuple.dstTotal += b.value;
    objectives.total++;
    if (!changed) {
      objectives.unchanged++;
    } else {
      tuple.changedCount++;
      if (b.value < a.value) {
        objectives.improved++;
      } else {
        objectives.worse++;
      }
    }
  }

  const tuples = Array.from(byTuple.values()).sort(
    (x, y) => x.tupleIndex - y.tupleIndex,
  );
  let decidingTupleIndex: number | null = null;
  for (const t of tuples) {
    t.delta = t.changedCount === 0 ? 0 : t.dstTotal - t.srcTotal;
    if (decidingTupleIndex == null && t.delta !== 0) {
      decidingTupleIndex = t.tupleIndex;
    }
  }
  return {tuples, objectives, constraints, decidingTupleIndex};
}

/**
 * Names of the objectives whose value differs between two evaluations, in
 * tuple order (then evaluation order), at most `limit` of them.
 */
export function changedObjectiveNames(
  src: readonly ExpressionResult[],
  dst: readonly ExpressionResult[],
  limit: number,
): string[] {
  const dstById = new Map<number, ExpressionResult>();
  for (const e of dst) {
    dstById.set(e.id, e);
  }
  const changed: Array<{name: string; tupleIndex: number; order: number}> = [];
  src.forEach((a, order) => {
    const b = dstById.get(a.id);
    if (
      a.type === ExpressionType.CONSTRAINT ||
      b == null ||
      a.value === b.value
    ) {
      return;
    }
    changed.push({name: a.name, tupleIndex: a.tupleIndex ?? 0, order});
  });
  changed.sort((x, y) => x.tupleIndex - y.tupleIndex || x.order - y.order);
  return Array.from(new Set(changed.map(c => c.name))).slice(0, limit);
}

/**
 * Objectives that are still above 0 at B and did not change, in tuple order:
 * where to look first when asking why the solver left something unsolved.
 */
export function stillAboveZero(
  summary: OverviewSummary,
): Array<TupleObjective & {tupleIndex: number}> {
  const out: Array<TupleObjective & {tupleIndex: number}> = [];
  for (const t of summary.tuples) {
    for (const o of t.objectives) {
      if (!o.changed && o.dstValue > 0) {
        out.push({...o, tupleIndex: t.tupleIndex});
      }
    }
  }
  return out;
}

/**
 * Objective value against cumulative search time for one local-search
 * profile, thinned to at most `maxPoints` points (first and last kept).
 */
export function profileTrace(
  profile: LocalSearchProfile,
  maxPoints = 120,
): Array<{t: number; v: number}> {
  const points: Array<{t: number; v: number}> = [];
  let t = 0;
  for (const event of profile.moveTypeEvents) {
    if (points.length === 0) {
      points.push({t, v: event.initialValue});
    }
    t += event.duration;
    points.push({t, v: event.finalValue});
  }
  if (points.length <= maxPoints) {
    return points;
  }
  const step = (points.length - 1) / (maxPoints - 1);
  const out: Array<{t: number; v: number}> = [];
  for (let i = 0; i < maxPoints; i++) {
    out.push(points[Math.round(i * step)]);
  }
  return out;
}
