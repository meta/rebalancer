import type {
  Assignments,
  UIAssignment,
} from '@/app/components/evaluation/AssignmentCard';
import {
  CONSTRAINT_STATUSES,
  DEFAULT_LIST_STATE,
  OBJECTIVE_STATUSES,
  SORT_LABEL,
  type COListState,
  type RowKind,
  type RowStatus,
  type SortKey,
} from '@/app/components/evaluation/co-list';
import type {MoveSetsViewState} from '@/app/components/evaluation/MoveSetsTable';
import type {Filter} from '@/lib/rebalancer-explorer-types';
import {OrderDirection} from '@/lib/rebalancer-explorer-types';
import {
  asArray,
  asString,
  normalizeFilters,
  parseQs,
  safeInt,
  stringifyQs,
} from '@/lib/url-state';

export const MS_DEFAULT_PAGE_SIZE = 10;

export interface ParsedURLState {
  srcBase: UIAssignment['base'];
  srcStep: number | null;
  srcOverrides: Array<{variable: string; container: string}>;
  dstBase: UIAssignment['base'] | null;
  dstStep: number | null;
  dstOverrides: Array<{variable: string; container: string}>;
  hideAllUnchanged: boolean;
  evalFilters: Filter[];
  msPartition: string | null;
  msScope: string | null;
  msObjectives: string[];
  /** Whether the URL chose the move table's objectives (even none). */
  msObjectivesSet: boolean;
  msFilters: Filter[];
  msOffset: number;
  msPageSize: number;
  msOrderColumn: string | null;
  msOrderDirection: OrderDirection;
  msMinimized: boolean;
  /** Expression open in the detail pane, if any. */
  detailExpr: number | null;
  detailTab: 'spec' | 'tree';
  /**
   * Tuple index to scroll to and show once, from a Summary link. It is not
   * written back to the URL.
   */
  focusTuple: number | null;
  list: COListState;
}

const KINDS = new Set<string>(['objective', 'constraint']);
const STATUSES = new Set<string>([
  ...OBJECTIVE_STATUSES,
  ...CONSTRAINT_STATUSES,
]);

/** The comma-separated list in `value`, keeping only known items. */
function csv(value: unknown, keep: (item: string) => boolean): string[] {
  const s = asString(value);
  return s == null
    ? []
    : s.split(',').filter(item => item !== '' && keep(item));
}

function parseListState(parsed: Record<string, unknown>): COListState {
  const sort = asString(parsed.sort);
  return {
    search: asString(parsed.q) ?? '',
    facets: {
      kinds: csv(parsed.kind, k => KINDS.has(k)) as RowKind[],
      statuses: csv(parsed.status, k => STATUSES.has(k)) as RowStatus[],
      tuples: csv(parsed.tuple, k => /^\d+$/.test(k)).map(Number),
    },
    group: parsed.group === 'none' ? 'none' : 'tuple',
    sort:
      sort != null && Object.hasOwn(SORT_LABEL, sort)
        ? (sort as SortKey)
        : DEFAULT_LIST_STATE.sort,
    dir: parsed.dir === 'desc' ? 'desc' : 'asc',
    penaltyShown: parsed.penalty !== '0',
  };
}

/** The detail pane state written to the URL; null when it is closed. */
export interface CODetailState {
  expressionId: number;
  tab: 'spec' | 'tree';
}

export const VALID_BASES = new Set(['INITIAL', 'FINAL', 'INTERMEDIATE']);

export function parseCOSearchParams(
  searchParams: URLSearchParams,
): ParsedURLState {
  const parsed = parseQs(searchParams);

  const srcBaseRaw = asString(parsed.srcBase);
  const srcBase = (
    VALID_BASES.has(srcBaseRaw ?? '') ? srcBaseRaw : 'INITIAL'
  ) as UIAssignment['base'];

  const srcStep = parsed.srcStep != null ? safeInt(parsed.srcStep, 0) : null;

  const dstBaseRaw = asString(parsed.dstBase);
  const dstBase =
    dstBaseRaw != null && VALID_BASES.has(dstBaseRaw)
      ? (dstBaseRaw as UIAssignment['base'])
      : null;

  const dstStep = parsed.dstStep != null ? safeInt(parsed.dstStep, 0) : null;

  return {
    srcBase,
    srcStep,
    srcOverrides: asArray<{variable: string; container: string}>(
      parsed.srcOverrides,
      [],
    ),
    dstBase,
    dstStep,
    dstOverrides: asArray<{variable: string; container: string}>(
      parsed.dstOverrides,
      [],
    ),
    hideAllUnchanged: parsed.hideUnchanged !== '0',
    evalFilters: normalizeFilters(parsed.evalFilters),
    msPartition: asString(parsed.msPartition),
    msScope: asString(parsed.msScope),
    msObjectives: asArray<string>(parsed.msObjectives, []),
    msObjectivesSet: parsed.msObjectives !== undefined,
    msFilters: normalizeFilters(parsed.msFilters),
    msOffset: safeInt(parsed.msOffset, 0),
    msPageSize: safeInt(parsed.msPageSize, MS_DEFAULT_PAGE_SIZE),
    msOrderColumn: asString(parsed.msOrderCol),
    msOrderDirection: safeInt(parsed.msOrderDir, 0) as OrderDirection,
    msMinimized: parsed.msMinimized !== '0',
    detailExpr:
      asString(parsed.detailExpr) != null &&
      Number.isFinite(Number(parsed.detailExpr))
        ? Number(parsed.detailExpr)
        : null,
    detailTab: parsed.detailTab === 'tree' ? 'tree' : 'spec',
    focusTuple:
      asString(parsed.focusTuple) != null &&
      Number.isInteger(Number(parsed.focusTuple))
        ? Number(parsed.focusTuple)
        : null,
    list: parseListState(parsed),
  };
}

// Skip serializing overrides to URL when there are too many — the URL becomes
// enormous and router.replace() bogs down the UI.
export const MAX_URL_OVERRIDES = 100;

export function buildCOSearchParams(
  assignments: Assignments,
  hideAllUnchanged: boolean,
  evalFilters: Filter[],
  ms: MoveSetsViewState,
  detail: CODetailState | null = null,
  list: COListState = DEFAULT_LIST_STATE,
): string {
  const obj: Record<string, unknown> = {};

  // Assignment A
  if (assignments.src.base !== 'INITIAL') {
    obj.srcBase = assignments.src.base;
  }
  if (assignments.src.step != null) {
    obj.srcStep = assignments.src.step;
  }
  if (
    assignments.src.overrides.length > 0 &&
    assignments.src.overrides.length <= MAX_URL_OVERRIDES
  ) {
    obj.srcOverrides = assignments.src.overrides;
  }

  // Assignment B — always include base (default is metadata-dependent)
  obj.dstBase = assignments.dst.base;
  if (assignments.dst.step != null) {
    obj.dstStep = assignments.dst.step;
  }
  if (
    assignments.dst.overrides.length > 0 &&
    assignments.dst.overrides.length <= MAX_URL_OVERRIDES
  ) {
    obj.dstOverrides = assignments.dst.overrides;
  }

  // Global
  if (!hideAllUnchanged) {
    obj.hideUnchanged = '0';
  }

  // Evaluation filters
  if (evalFilters.length > 0) {
    obj.evalFilters = evalFilters;
  }

  // MoveSets table
  if (ms.partition != null) {
    obj.msPartition = ms.partition;
  }
  if (ms.scope != null) {
    obj.msScope = ms.scope;
  }
  if (ms.objectives.length > 0) {
    obj.msObjectives = ms.objectives;
  }
  if (ms.filters.length > 0) {
    obj.msFilters = ms.filters;
  }
  if (ms.offset !== 0) {
    obj.msOffset = ms.offset;
  }
  if (ms.pageSize !== MS_DEFAULT_PAGE_SIZE) {
    obj.msPageSize = ms.pageSize;
  }
  if (ms.orderColumn != null) {
    obj.msOrderCol = ms.orderColumn;
  }
  if (ms.orderDirection !== 0) {
    obj.msOrderDir = ms.orderDirection;
  }
  if (!ms.isMinimized) {
    obj.msMinimized = '0';
  }

  // Constraints & objectives list
  if (list.search !== '') {
    obj.q = list.search;
  }
  if (list.facets.kinds.length > 0) {
    obj.kind = list.facets.kinds.join(',');
  }
  if (list.facets.statuses.length > 0) {
    obj.status = list.facets.statuses.join(',');
  }
  if (list.facets.tuples.length > 0) {
    obj.tuple = list.facets.tuples.join(',');
  }
  if (list.group !== 'tuple') {
    obj.group = list.group;
  }
  if (list.sort !== DEFAULT_LIST_STATE.sort) {
    obj.sort = list.sort;
  }
  if (list.dir !== 'asc') {
    obj.dir = list.dir;
  }
  if (!list.penaltyShown) {
    obj.penalty = '0';
  }

  // Detail pane
  if (detail != null) {
    obj.detailExpr = detail.expressionId;
    if (detail.tab === 'tree') {
      obj.detailTab = 'tree';
    }
  }

  return stringifyQs(obj);
}
