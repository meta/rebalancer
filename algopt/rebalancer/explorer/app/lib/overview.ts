import type {
  ColumnDescription,
  ExpressionResult,
  RowData,
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

/** How a tuple's objectives moved from A to B: the ladder's status bar. */
export interface TupleMix {
  improved: number;
  regressed: number;
  // Unchanged and still above or below 0 at B.
  unchangedNonzero: number;
  // Unchanged and exactly 0 at B, the ideal.
  zero: number;
}

export function tupleMix(t: TupleSummary): TupleMix {
  const mix = {improved: 0, regressed: 0, unchangedNonzero: 0, zero: 0};
  for (const o of t.objectives) {
    if (o.changed) {
      if (o.dstValue < o.srcValue) {
        mix.improved++;
      } else {
        mix.regressed++;
      }
    } else if (o.dstValue !== 0) {
      mix.unchangedNonzero++;
    } else {
      mix.zero++;
    }
  }
  return mix;
}

/**
 * The change of each objective in every move set, as `getMoveSets` reports it
 * in its `<objective> (Δ)` columns.
 */
export interface MoveSetDeltas {
  names: string[];
  // One entry per move set, in solver order; entry j is the change of names[j].
  sets: number[][];
}

/**
 * Reads the per-move-set changes of `names` from `getMoveSets` rows. Column 0
 * is the 1-based move set number. The server repeats a move set's changes on
 * each of its rows, so the first row of each set is kept.
 */
export function moveSetDeltasFromRows(
  columns: readonly ColumnDescription[],
  rows: readonly RowData[],
  names: readonly string[],
): MoveSetDeltas {
  const cols = names.map(name => {
    const i = columns.findIndex(c => c.name === `${name} (Δ)`);
    if (i < 0) {
      throw new Error(`Move sets have no change column for ${name}`);
    }
    return i;
  });
  const bySet = new Map<number, number[]>();
  for (const row of rows) {
    const set = row.cells[0]?.doubleValue;
    if (set == null || bySet.has(set)) {
      continue;
    }
    bySet.set(
      set,
      cols.map(i => row.cells[i]?.doubleValue ?? 0),
    );
  }
  const sets = Array.from(bySet.entries())
    .sort((a, b) => a[0] - b[0])
    .map(([, deltas]) => deltas);
  return {names: [...names], sets};
}

/**
 * Each tuple's total after every move set: index 0 is the Initial total and
 * index k adds the changes of the tuple's objectives in the first k move
 * sets. Tuples with no objective in `deltas` are left out.
 */
export function tupleStepTotals(
  summary: OverviewSummary,
  deltas: MoveSetDeltas,
): Map<number, number[]> {
  const column = new Map(deltas.names.map((name, j) => [name, j]));
  const out = new Map<number, number[]>();
  for (const t of summary.tuples) {
    const cols = t.objectives
      .map(o => column.get(o.name))
      .filter((j): j is number => j != null);
    if (cols.length === 0) {
      continue;
    }
    let total = t.srcTotal;
    const series = [total];
    for (const set of deltas.sets) {
      for (const j of cols) {
        total += set[j];
      }
      series.push(total);
    }
    out.set(t.tupleIndex, series);
  }
  return out;
}

/** At most `maxPoints` evenly spaced values, the first and last kept. */
export function thinSeries(
  values: readonly number[],
  maxPoints = 120,
): number[] {
  if (values.length <= maxPoints) {
    return [...values];
  }
  const step = (values.length - 1) / (maxPoints - 1);
  const out: number[] = [];
  for (let i = 0; i < maxPoints; i++) {
    out.push(values[Math.round(i * step)]);
  }
  return out;
}
