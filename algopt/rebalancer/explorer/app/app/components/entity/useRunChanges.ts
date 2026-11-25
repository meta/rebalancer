'use client';

import {useEffect, useState} from 'react';

import {
  joinEvaluationRows,
  type EvaluationRow,
} from '@/app/components/evaluation/evaluation-rows';
import {
  fetchEntityData,
  fetchEvaluation,
  fetchMoveSets,
  fetchMovesBetween,
} from '@/lib/rebalancer-explorer-api';
import {
  AssignmentBase,
  ExpressionType,
  type Handle,
  type ProblemMetadata,
  type RowData,
} from '@/lib/rebalancer-explorer-types';
import {
  moveSetsFromTable,
  movedObjectsFromRows,
  type MovedObject,
  type RunMoveSets,
} from '@/lib/run-changes';

// Above this many moved objects, filtering by name would send too large a
// request, so the moved views are off.
export const MAX_MOVED_FOR_FILTER = 5000;
// getMoveSets pages by rows, one per moved object; above this many rows the
// move sets are not indexed.
export const MAX_MOVE_ROWS = 5000;
const MOVE_ROWS_PER_PAGE = 500;

export const INITIAL = {
  base: AssignmentBase.INITIAL,
  variableToContainerOverride: {},
};
export const FINAL = {
  base: AssignmentBase.FINAL,
  variableToContainerOverride: {},
};

export interface RunMoves {
  // Moved objects, Initial → Final; null above MAX_MOVED_FOR_FILTER.
  moved: Map<string, MovedObject> | null;
  count: number;
}

/** An objective as the two evaluations report it, Initial → Final. */
export interface ObjectiveInfo extends EvaluationRow {
  tupleIndex: number;
}

export interface RunObjectives {
  byName: Map<string, ObjectiveInfo>;
  maxTupleIndex: number;
}

/** Whether the run has moves to index: a final assignment of its objects. */
function hasMoves(metadata: ProblemMetadata | null): boolean {
  return (
    metadata != null &&
    metadata.hasFinalAssignment &&
    metadata.variableName === metadata.objectName
  );
}

async function loadMoves(
  handle: Handle,
  metadata: ProblemMetadata,
): Promise<RunMoves> {
  const response = await fetchMovesBetween(handle, INITIAL, FINAL);
  const destinations = response.variableToContainer;
  const names = Object.keys(destinations);
  if (names.length > MAX_MOVED_FOR_FILTER) {
    return {moved: null, count: names.length};
  }
  let columns: Parameters<typeof movedObjectsFromRows>[0] = [];
  let rows: RowData[] = [];
  if (names.length > 0) {
    const {result} = await fetchEntityData(handle, {
      entity: metadata.objectName,
      filter: {
        rules: [{stringAny: {column: metadata.objectName, values: names}}],
      },
      page: {offset: 0, limit: names.length},
    });
    columns = result.columns;
    rows = result.rows;
  }
  return {
    moved: movedObjectsFromRows(
      columns,
      rows,
      destinations,
      metadata.containerName,
      metadata.partitionNames,
    ),
    count: names.length,
  };
}

async function loadMoveSets(
  handle: Handle,
  metadata: ProblemMetadata,
): Promise<RunMoveSets | null> {
  const objectiveNames = metadata.canDisplayObjChangesInMoveSetsTable
    ? metadata.objectiveNames
    : [];
  const page = (offset: number) =>
    fetchMoveSets(handle, {
      assignmentA: INITIAL,
      assignmentB: FINAL,
      query: {entity: 'move_sets', page: {offset, limit: MOVE_ROWS_PER_PAGE}},
      ...(objectiveNames.length > 0 ? {objectiveNames} : {}),
    });
  const first = (await page(0)).table;
  if (first.totalCount > MAX_MOVE_ROWS) {
    return null;
  }
  const offsets = [];
  for (
    let offset = MOVE_ROWS_PER_PAGE;
    offset < first.totalCount;
    offset += MOVE_ROWS_PER_PAGE
  ) {
    offsets.push(offset);
  }
  const rest = await Promise.all(offsets.map(page));
  return moveSetsFromTable(
    first.columns,
    [...first.rows, ...rest.flatMap(r => r.table.rows)],
    objectiveNames,
  );
}

async function loadObjectives(handle: Handle): Promise<RunObjectives> {
  const [src, dst] = await Promise.all([
    fetchEvaluation(handle, INITIAL),
    fetchEvaluation(handle, FINAL),
  ]);
  const objectives = src.result.expressions.filter(
    e => e.type === ExpressionType.OBJECTIVE,
  );
  const tupleById = new Map(objectives.map(e => [e.id, e.tupleIndex ?? 0]));
  const byName = new Map<string, ObjectiveInfo>();
  let maxTupleIndex = 0;
  for (const row of joinEvaluationRows(objectives, dst.result.expressions)) {
    const tupleIndex = tupleById.get(row.expressionId) ?? 0;
    maxTupleIndex = Math.max(maxTupleIndex, tupleIndex);
    if (!byName.has(row.name)) {
      byName.set(row.name, {...row, tupleIndex});
    }
  }
  return {byName, maxTupleIndex};
}

/**
 * One load per sandbox and kind, shared by every page and panel: the value,
 * or null while loading, when disabled, or after a failure (`failed`).
 */
function useSandboxLoad<T>(
  cache: Map<string, Promise<T>>,
  handle: Handle | null,
  metadata: ProblemMetadata | null,
  enabled: boolean,
  load: (h: Handle, m: ProblemMetadata) => Promise<T>,
): {value: T | null; loading: boolean; failed: boolean} {
  const key =
    enabled && handle != null && metadata != null
      ? JSON.stringify(handle)
      : null;
  const [state, setState] = useState<{
    key: string;
    value: T | null;
    failed: boolean;
  } | null>(null);

  useEffect(() => {
    if (key == null || handle == null || metadata == null) {
      return;
    }
    let cancelled = false;
    let promise = cache.get(key);
    if (promise == null) {
      promise = load(handle, metadata);
      cache.set(key, promise);
      promise.catch(() => cache.delete(key));
    }
    promise.then(
      value => {
        if (!cancelled) {
          setState({key, value, failed: false});
        }
      },
      () => {
        if (!cancelled) {
          setState({key, value: null, failed: true});
        }
      },
    );
    return () => {
      cancelled = true;
    };
    // `load` reads only values that are fixed for a sandbox.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [key, handle, metadata, cache]);

  const current = state != null && state.key === key ? state : null;
  return {
    value: current?.value ?? null,
    loading: key != null && current == null,
    failed: current?.failed ?? false,
  };
}

const movesCache = new Map<string, Promise<RunMoves>>();
const moveSetsCache = new Map<string, Promise<RunMoveSets | null>>();
const objectivesCache = new Map<string, Promise<RunObjectives>>();

/**
 * The objects that move between the Initial and the Final assignment, with
 * where each sat. Null while loading, and when the run has no final
 * assignment or its variables are not its objects.
 */
export function useRunMoves(
  handle: Handle | null,
  metadata: ProblemMetadata | null,
  enabled = true,
): {moves: RunMoves | null; loading: boolean} {
  const {value, loading, failed} = useSandboxLoad(
    movesCache,
    handle,
    metadata,
    enabled && hasMoves(metadata),
    loadMoves,
  );
  // Without moves the page still works; the moved views stay off.
  return {moves: failed ? {moved: null, count: 0} : value, loading};
}

/**
 * Every move set Initial → Final with its objects and objective changes.
 * Null while loading, without moves, above MAX_MOVE_ROWS rows, or on a
 * failure (`failed`).
 */
export function useRunMoveSets(
  handle: Handle | null,
  metadata: ProblemMetadata | null,
  enabled = true,
): {moveSets: RunMoveSets | null; loading: boolean; failed: boolean} {
  const {value, loading, failed} = useSandboxLoad(
    moveSetsCache,
    handle,
    metadata,
    enabled && hasMoves(metadata),
    loadMoveSets,
  );
  return {moveSets: value, loading, failed};
}

/** The run's objectives, Initial → Final, by name. */
export function useRunObjectives(
  handle: Handle | null,
  metadata: ProblemMetadata | null,
  enabled = true,
): {objectives: RunObjectives | null; loading: boolean} {
  const {value, loading} = useSandboxLoad(
    objectivesCache,
    handle,
    metadata,
    enabled && (metadata?.hasFinalAssignment ?? false),
    h => loadObjectives(h),
  );
  return {objectives: value, loading};
}
