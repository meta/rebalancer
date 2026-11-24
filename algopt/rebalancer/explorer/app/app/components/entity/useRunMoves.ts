'use client';

import {useEffect, useState} from 'react';

import {
  fetchEntityData,
  fetchMovesBetween,
} from '@/lib/rebalancer-explorer-api';
import {
  AssignmentBase,
  type Handle,
  type ProblemMetadata,
} from '@/lib/rebalancer-explorer-types';

// Above this many moved objects, filtering by name would send too large a
// request, so the "Moved" view is off.
export const MAX_MOVED_FOR_FILTER = 5000;

export interface RunMoves {
  // Moved objects, Initial → Final, with their two containers.
  moved: Map<string, {src: string; dst: string}> | null;
  count: number;
}

// One load per sandbox: the object and container pages share it.
const cache = new Map<string, Promise<RunMoves>>();

async function load(
  handle: Handle,
  metadata: ProblemMetadata,
): Promise<RunMoves> {
  const response = await fetchMovesBetween(
    handle,
    {base: AssignmentBase.INITIAL, variableToContainerOverride: {}},
    {base: AssignmentBase.FINAL, variableToContainerOverride: {}},
  );
  const names = Object.keys(response.variableToContainer);
  if (names.length > MAX_MOVED_FOR_FILTER) {
    return {moved: null, count: names.length};
  }
  const src = new Map<string, string>();
  if (names.length > 0) {
    const {result} = await fetchEntityData(handle, {
      entity: metadata.objectName,
      filter: {
        rules: [{stringAny: {column: metadata.objectName, values: names}}],
      },
      page: {offset: 0, limit: names.length},
    });
    const name = result.columns.findIndex(c => c.primaryKey);
    const from = result.columns.findIndex(
      c => c.name === `src.${metadata.containerName}`,
    );
    if (name >= 0 && from >= 0) {
      for (const row of result.rows) {
        src.set(
          row.cells[name]?.stringValue ?? '',
          row.cells[from]?.stringValue ?? '',
        );
      }
    }
  }
  return {
    moved: new Map(
      names.map(n => [
        n,
        {src: src.get(n) ?? '', dst: response.variableToContainer[n]},
      ]),
    ),
    count: names.length,
  };
}

/**
 * The objects that move between the Initial and the Final assignment. Null
 * while loading, and when the run has no final assignment or its variables
 * are not its objects.
 */
export default function useRunMoves(
  handle: Handle | null,
  metadata: ProblemMetadata | null,
  enabled: boolean,
): {moves: RunMoves | null; loading: boolean} {
  const usable =
    enabled &&
    handle != null &&
    metadata != null &&
    metadata.hasFinalAssignment &&
    metadata.variableName === metadata.objectName;
  const key = usable ? JSON.stringify(handle) : null;
  const [state, setState] = useState<{key: string; moves: RunMoves} | null>(
    null,
  );

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
      moves => {
        if (!cancelled) {
          setState({key, moves});
        }
      },
      () => {
        // Without moves the page still works; the "Moved" view stays off.
        if (!cancelled) {
          setState({key, moves: {moved: null, count: 0}});
        }
      },
    );
    return () => {
      cancelled = true;
    };
  }, [key, handle, metadata]);

  const moves = state != null && state.key === key ? state.moves : null;
  return {moves, loading: key != null && moves == null};
}
