import {isRowChanged} from '@/app/components/evaluation/evaluation-penalty';
import {
  joinEvaluationRows,
  type EvaluationRow,
} from '@/app/components/evaluation/evaluation-rows';
import {
  ExpressionType,
  type ExpressionResult,
} from '@/lib/rebalancer-explorer-types';

export type RowKind = 'objective' | 'constraint';

/**
 * Objectives are minimized: lower at B improved. A constraint's value is how
 * far it is broken, so it is fine only at 0.
 */
export type RowStatus =
  | 'improved'
  | 'regressed'
  | 'unchanged'
  | 'violated'
  | 'newlyViolated'
  | 'fixed'
  | 'satisfied';

export const OBJECTIVE_STATUSES: readonly RowStatus[] = [
  'improved',
  'regressed',
  'unchanged',
];
export const CONSTRAINT_STATUSES: readonly RowStatus[] = [
  'violated',
  'newlyViolated',
  'fixed',
  'satisfied',
];

export const STATUS_LABEL: Record<RowStatus, string> = {
  improved: 'Improved',
  regressed: 'Regressed',
  unchanged: 'Unchanged',
  violated: 'Violated',
  newlyViolated: 'Newly violated',
  fixed: 'Fixed',
  satisfied: 'Satisfied',
};

export const STATUS_HELP: Record<RowStatus, string> = {
  improved: 'Objective lower at B than at A.',
  regressed: 'Objective higher at B than at A.',
  unchanged: 'Objective with the same value at A and B.',
  violated: 'Constraint above 0 at both A and B.',
  newlyViolated: 'Constraint at 0 at A and above 0 at B.',
  fixed: 'Constraint above 0 at A and at 0 at B.',
  satisfied: 'Constraint at 0 at both A and B.',
};

/** One constraint or objective, A vs B, with what the list sorts and filters on. */
export interface CORow extends EvaluationRow {
  kind: RowKind;
  // Objective tuple index; null for constraints.
  tupleIndex: number | null;
  status: RowStatus;
  // Position in the evaluation, the order within a tuple.
  order: number;
}

export function rowStatus(kind: RowKind, a: number, b: number): RowStatus {
  if (kind === 'objective') {
    return b < a ? 'improved' : b > a ? 'regressed' : 'unchanged';
  }
  if (b !== 0) {
    return a !== 0 ? 'violated' : 'newlyViolated';
  }
  return a !== 0 ? 'fixed' : 'satisfied';
}

/** Joins A and B by expression id, in A's order. */
export function buildRows(
  src: ExpressionResult[],
  dst: ExpressionResult[],
): CORow[] {
  const tupleById = new Map<number, number | null>();
  const kindById = new Map<number, RowKind>();
  for (const e of src) {
    const kind =
      e.type === ExpressionType.CONSTRAINT ? 'constraint' : 'objective';
    kindById.set(e.id, kind);
    tupleById.set(e.id, kind === 'constraint' ? null : (e.tupleIndex ?? 0));
  }
  return joinEvaluationRows(src, dst).map((row, order) => {
    const kind = kindById.get(row.expressionId) ?? 'objective';
    return {
      ...row,
      kind,
      tupleIndex: tupleById.get(row.expressionId) ?? null,
      status: rowStatus(kind, row.srcValue, row.dstValue),
      order,
    };
  });
}

/** Facet selections; an empty list selects everything. */
export interface COFacets {
  kinds: RowKind[];
  statuses: RowStatus[];
  tuples: number[];
}

export const NO_FACETS: COFacets = {kinds: [], statuses: [], tuples: []};

export interface COQuery {
  search: string;
  facets: COFacets;
  // Hide rows whose value (and shown penalty) is the same at A and B.
  changedOnly: boolean;
  penaltyShown: boolean;
  // The query-builder filters, compiled; null when there are none.
  matches: ((row: CORow) => boolean) | null;
}

function matchesSearch(row: CORow, search: string): boolean {
  if (search === '') {
    return true;
  }
  const s = search.toLowerCase();
  return (
    row.name.toLowerCase().includes(s) ||
    row.description.toLowerCase().includes(s)
  );
}

/**
 * Whether a row passes every filter except `skip` and, when `ignoreChanged`,
 * the changed-only filter.
 */
export function passes(
  row: CORow,
  q: COQuery,
  skip: keyof COFacets | null = null,
  ignoreChanged = false,
): boolean {
  const {kinds, statuses, tuples} = q.facets;
  return (
    (skip === 'kinds' || kinds.length === 0 || kinds.includes(row.kind)) &&
    (skip === 'statuses' ||
      statuses.length === 0 ||
      statuses.includes(row.status)) &&
    (skip === 'tuples' ||
      tuples.length === 0 ||
      (row.tupleIndex != null && tuples.includes(row.tupleIndex))) &&
    matchesSearch(row, q.search) &&
    (q.matches == null || q.matches(row)) &&
    (ignoreChanged || !q.changedOnly || isRowChanged(row, q.penaltyShown))
  );
}

export interface FacetCounts {
  kinds: Map<RowKind, number>;
  statuses: Map<RowStatus, number>;
  tuples: Map<number, number>;
}

function bump<K>(m: Map<K, number>, k: K) {
  m.set(k, (m.get(k) ?? 0) + 1);
}

/**
 * What each facet option would show: counted against every other active
 * filter but not its own facet, so ticking it predicts the result.
 */
export function facetCounts(rows: readonly CORow[], q: COQuery): FacetCounts {
  const counts: FacetCounts = {
    kinds: new Map(),
    statuses: new Map(),
    tuples: new Map(),
  };
  for (const row of rows) {
    if (passes(row, q, 'kinds')) {
      bump(counts.kinds, row.kind);
    }
    if (passes(row, q, 'statuses')) {
      bump(counts.statuses, row.status);
    }
    if (row.tupleIndex != null && passes(row, q, 'tuples')) {
      bump(counts.tuples, row.tupleIndex);
    }
  }
  return counts;
}

export type SortKey =
  'priority' | 'absChange' | 'change' | 'percent' | 'a' | 'b' | 'name';

export type SortDir = 'asc' | 'desc';

export const SORT_LABEL: Record<SortKey, string> = {
  priority: 'Priority',
  absChange: 'Largest change',
  change: 'B − A',
  percent: '% change',
  a: 'A value',
  b: 'B value',
  name: 'Name',
};

/** The direction a sort starts in when it is picked. */
export const SORT_DEFAULT_DIR: Record<SortKey, SortDir> = {
  priority: 'asc',
  absChange: 'desc',
  change: 'asc',
  percent: 'asc',
  a: 'desc',
  b: 'desc',
  name: 'asc',
};

function sortValue(row: CORow, key: SortKey): number | string {
  switch (key) {
    case 'priority':
      return row.order;
    case 'absChange':
      return Math.abs(row.delta);
    case 'change':
      return row.delta;
    case 'percent':
      return row.percentage;
    case 'a':
      return row.srcValue;
    case 'b':
      return row.dstValue;
    case 'name':
      return row.name;
  }
}

/** A stable sort; ties keep the evaluation order. */
export function sortRows(
  rows: readonly CORow[],
  key: SortKey,
  dir: SortDir,
): CORow[] {
  const sign = dir === 'asc' ? 1 : -1;
  return [...rows].sort((x, y) => {
    const a = sortValue(x, key);
    const b = sortValue(y, key);
    const c =
      typeof a === 'string' && typeof b === 'string'
        ? a.localeCompare(b)
        : (a as number) - (b as number);
    return c !== 0 ? sign * c : x.order - y.order;
  });
}

/** Constraints first, then one group per objective tuple index. */
export interface COGroup {
  key: string;
  kind: RowKind;
  tupleIndex: number | null;
  // Rows that pass the kind and tuple facets: the group's universe.
  rows: CORow[];
  // Rows shown, in display order.
  shown: CORow[];
  // Rows hidden only because they are unchanged.
  hiddenUnchanged: number;
}

export function groupKey(row: {kind: RowKind; tupleIndex: number | null}) {
  return row.kind === 'constraint' ? 'constraints' : `t${row.tupleIndex}`;
}

/**
 * The groups that have rows under the kind and tuple facets, with the rows
 * each one shows. A group in `showUnchanged` ignores the changed-only filter.
 */
export function groupRows(
  rows: readonly CORow[],
  q: COQuery,
  sort: SortKey,
  dir: SortDir,
  showUnchanged: ReadonlySet<string>,
): COGroup[] {
  const groups = new Map<string, COGroup>();
  for (const row of rows) {
    const {kinds, tuples} = q.facets;
    if (
      (kinds.length > 0 && !kinds.includes(row.kind)) ||
      (tuples.length > 0 &&
        (row.tupleIndex == null || !tuples.includes(row.tupleIndex)))
    ) {
      continue;
    }
    const key = groupKey(row);
    let g = groups.get(key);
    if (g == null) {
      g = {
        key,
        kind: row.kind,
        tupleIndex: row.tupleIndex,
        rows: [],
        shown: [],
        hiddenUnchanged: 0,
      };
      groups.set(key, g);
    }
    g.rows.push(row);
    if (passes(row, q, null, showUnchanged.has(key))) {
      g.shown.push(row);
    } else if (passes(row, q, null, true)) {
      g.hiddenUnchanged++;
    }
  }
  const out = Array.from(groups.values()).sort(
    (x, y) =>
      (x.tupleIndex ?? -1) - (y.tupleIndex ?? -1) ||
      (x.kind === 'constraint' ? -1 : 1) - (y.kind === 'constraint' ? -1 : 1),
  );
  for (const g of out) {
    g.shown = sortRows(g.shown, sort, dir);
  }
  return out;
}

/** Sums of A, B and the penalties over rows. */
export function totals(rows: readonly CORow[]) {
  let src = 0;
  let dst = 0;
  let srcPenalty = 0;
  let dstPenalty = 0;
  for (const r of rows) {
    src += r.srcValue;
    dst += r.dstValue;
    srcPenalty += r.srcPenalty ?? 0;
    dstPenalty += r.dstPenalty ?? 0;
  }
  return {src, dst, srcPenalty, dstPenalty};
}

export type GroupBy = 'tuple' | 'none';

/** The list's view state, kept in the URL. */
export interface COListState {
  search: string;
  facets: COFacets;
  group: GroupBy;
  sort: SortKey;
  dir: SortDir;
  penaltyShown: boolean;
}

export const DEFAULT_LIST_STATE: COListState = {
  search: '',
  facets: NO_FACETS,
  group: 'tuple',
  sort: 'priority',
  dir: 'asc',
  penaltyShown: true,
};
