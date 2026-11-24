'use client';

import {useEffect, useState} from 'react';

import {
  moveSetDeltasFromRows,
  summarizeEvaluations,
  type MoveSetDeltas,
  type OverviewSummary,
} from '@/lib/overview';
import {fetchEvaluation, fetchMoveSets} from '@/lib/rebalancer-explorer-api';
import {
  AssignmentBase,
  type Handle,
  type ProblemMetadata,
  type RowData,
} from '@/lib/rebalancer-explorer-types';

export interface Loadable<T> {
  data: T | null;
  loading: boolean;
  error: string | null;
}

const INITIAL = {base: AssignmentBase.INITIAL, variableToContainerOverride: {}};
const FINAL = {base: AssignmentBase.FINAL, variableToContainerOverride: {}};

// getMoveSets pages by rows, one per move.
const MOVE_ROWS_PER_PAGE = 500;
// Above this many moves the Summary skips the per-step totals.
const MAX_MOVE_ROWS = 5000;

function errorMessage(err: unknown, fallback: string): string {
  return err instanceof Error ? err.message : fallback;
}

/**
 * Loads one value per handle and ignores responses that arrive after the
 * handle changed or the page unmounted.
 */
function useLoad<T>(
  handle: Handle | null,
  enabled: boolean,
  load: (h: Handle) => Promise<T>,
  fallbackError: string,
): Loadable<T> {
  const [state, setState] = useState<Loadable<T>>({
    data: null,
    loading: enabled,
    error: null,
  });
  useEffect(() => {
    if (handle == null || !enabled) {
      return;
    }
    let cancelled = false;
    setState({data: null, loading: true, error: null});
    load(handle)
      .then(data => {
        if (!cancelled) {
          setState({data, loading: false, error: null});
        }
      })
      .catch((err: unknown) => {
        if (!cancelled) {
          setState({
            data: null,
            loading: false,
            error: errorMessage(err, fallbackError),
          });
        }
      });
    return () => {
      cancelled = true;
    };
    // `load` only reads values that are fixed for a handle.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [handle, enabled]);
  return state;
}

async function loadSummary(handle: Handle): Promise<OverviewSummary> {
  const [src, dst] = await Promise.all([
    fetchEvaluation(handle, INITIAL),
    fetchEvaluation(handle, FINAL),
  ]);
  return summarizeEvaluations(src.result.expressions, dst.result.expressions);
}

// One row is enough: the response carries the total row count.
async function loadMoveSetCount(handle: Handle): Promise<number> {
  const response = await fetchMoveSets(handle, {
    assignmentA: INITIAL,
    assignmentB: FINAL,
    query: {entity: 'move_sets', page: {offset: 0, limit: 1}},
  });
  return response.table.totalCount;
}

/**
 * The change of every objective in every move set from Initial to Final, or
 * null when the run has more than MAX_MOVE_ROWS moves.
 */
async function loadMoveSetDeltas(
  handle: Handle,
  objectiveNames: string[],
): Promise<MoveSetDeltas | null> {
  const page = (offset: number) =>
    fetchMoveSets(handle, {
      assignmentA: INITIAL,
      assignmentB: FINAL,
      query: {entity: 'move_sets', page: {offset, limit: MOVE_ROWS_PER_PAGE}},
      objectiveNames,
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
  const rows: RowData[] = [
    ...first.rows,
    ...rest.flatMap(response => response.table.rows),
  ];
  return moveSetDeltasFromRows(first.columns, rows, objectiveNames);
}

/**
 * Data for the Summary overview: the Initial vs Final evaluation summary, the
 * number of move sets between them and each move set's objective changes.
 * Each part loads and fails on its own, so one slow or failing call never
 * blocks the others. Runs without a final assignment skip the comparisons,
 * and runs saved without per-move-set changes skip the last part.
 */
export function useRunOverviewData(
  handle: Handle | null,
  metadata: ProblemMetadata | null,
): {
  summary: Loadable<OverviewSummary>;
  moveSetCount: Loadable<number>;
  moveSetDeltas: Loadable<MoveSetDeltas | null>;
} {
  const hasFinalAssignment = metadata?.hasFinalAssignment ?? false;
  const objectiveNames = metadata?.objectiveNames ?? [];
  const summary = useLoad(
    handle,
    hasFinalAssignment,
    loadSummary,
    'Failed to evaluate the initial and final assignments',
  );
  const moveSetCount = useLoad(
    handle,
    hasFinalAssignment,
    loadMoveSetCount,
    'Failed to count move sets',
  );
  const moveSetDeltas = useLoad(
    handle,
    hasFinalAssignment &&
      (metadata?.canDisplayObjChangesInMoveSetsTable ?? false) &&
      objectiveNames.length > 0,
    h => loadMoveSetDeltas(h, objectiveNames),
    'Failed to load the objective changes per move set',
  );
  return {summary, moveSetCount, moveSetDeltas};
}
