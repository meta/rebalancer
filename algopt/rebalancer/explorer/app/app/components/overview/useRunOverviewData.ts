'use client';

import {useEffect, useState} from 'react';

import {summarizeEvaluations, type OverviewSummary} from '@/lib/overview';
import {
  fetchEvaluation,
  fetchLocalSearchProfiles,
  fetchMoveSets,
} from '@/lib/rebalancer-explorer-api';
import {
  AssignmentBase,
  type Handle,
  type LocalSearchProfile,
} from '@/lib/rebalancer-explorer-types';

export interface Loadable<T> {
  data: T | null;
  loading: boolean;
  error: string | null;
}

const INITIAL = {base: AssignmentBase.INITIAL, variableToContainerOverride: {}};
const FINAL = {base: AssignmentBase.FINAL, variableToContainerOverride: {}};

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
    // `load` is a module-level function per call site.
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

async function loadProfiles(handle: Handle): Promise<LocalSearchProfile[]> {
  const response = await fetchLocalSearchProfiles(handle);
  return response.profiles;
}

/**
 * Data for the Summary overview: the Initial vs Final evaluation summary, the
 * number of move sets between them and the local-search profiles. Each part
 * loads and fails on its own, so one slow or failing call never blocks the
 * others. Runs without a final assignment skip the comparisons.
 */
export function useRunOverviewData(
  handle: Handle | null,
  hasFinalAssignment: boolean,
): {
  summary: Loadable<OverviewSummary>;
  moveSetCount: Loadable<number>;
  profiles: Loadable<LocalSearchProfile[]>;
} {
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
  const profiles = useLoad(
    handle,
    true,
    loadProfiles,
    'Failed to load local search profiles',
  );
  return {summary, moveSetCount, profiles};
}
