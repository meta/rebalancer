'use client';

import {useEffect, useMemo, useState} from 'react';

import {fetchEntityData} from '@/lib/rebalancer-explorer-api';
import type {Handle, ProblemMetadata} from '@/lib/rebalancer-explorer-types';

// Each count scans the object table; above this many objects they are not
// fetched implicitly.
export const MAX_OBJECTS_TO_COUNT = 1_000_000;
// Counts in flight at once.
const CONCURRENCY = 4;

const cache = new Map<string, Promise<number>>();
const queue: Array<() => void> = [];
let running = 0;

const EMPTY: ReadonlyMap<string, number | null> = new Map();

function schedule<T>(task: () => Promise<T>): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    const run = () => {
      running++;
      task()
        .then(resolve, reject)
        .finally(() => {
          running--;
          queue.shift()?.();
        });
    };
    if (running < CONCURRENCY) {
      run();
    } else {
      queue.push(run);
    }
  });
}

/** How many objects one partition group has, cached per sandbox. */
export function groupSize(
  handle: Handle,
  objectName: string,
  partition: string,
  group: string,
): Promise<number> {
  const key = JSON.stringify([handle, partition, group]);
  let promise = cache.get(key);
  if (promise == null) {
    promise = schedule(() =>
      fetchEntityData(handle, {
        entity: objectName,
        filter: {rules: [{stringAny: {column: partition, values: [group]}}]},
        page: {offset: 0, limit: 1},
      }).then(r => r.result.totalCount),
    );
    cache.set(key, promise);
    promise.catch(() => cache.delete(key));
  }
  return promise;
}

/**
 * The member counts of `groups` as they arrive (grouped rows carry no
 * count, so each is one filtered count). Only `groups` are in the map: a
 * group still being counted is absent, and a group that is not counted
 * (above MAX_OBJECTS_TO_COUNT objects, or its count failed) is null.
 */
export default function useGroupSizes(
  handle: Handle | null,
  metadata: ProblemMetadata | null,
  partition: string,
  groups: readonly string[],
): ReadonlyMap<string, number | null> {
  const key = JSON.stringify([handle, partition, groups]);
  const [arrived, setArrived] = useState<{
    key: string;
    sizes: ReadonlyMap<string, number | null>;
  }>({key, sizes: EMPTY});
  const tooMany =
    metadata != null && metadata.numObjects > MAX_OBJECTS_TO_COUNT;
  const enabled = handle != null && metadata != null && !tooMany;
  useEffect(() => {
    if (!enabled || handle == null || metadata == null) {
      return;
    }
    let cancelled = false;
    const add = (g: string, n: number | null) => {
      if (cancelled) {
        return;
      }
      setArrived(prev => {
        const sizes = prev.key === key ? prev.sizes : EMPTY;
        return sizes.has(g) && sizes.get(g) === n
          ? prev
          : {key, sizes: new Map(sizes).set(g, n)};
      });
    };
    const [, , gs] = JSON.parse(key) as [unknown, string, string[]];
    for (const g of gs) {
      groupSize(handle, metadata.objectName, partition, g).then(
        n => add(g, n),
        () => add(g, null),
      );
    }
    return () => {
      cancelled = true;
    };
  }, [enabled, handle, metadata, partition, key]);
  const notCounted = useMemo(() => {
    if (!tooMany) {
      return null;
    }
    const [, , gs] = JSON.parse(key) as [unknown, string, string[]];
    return new Map<string, number | null>(gs.map(g => [g, null]));
  }, [tooMany, key]);
  if (notCounted != null) {
    return notCounted;
  }
  return enabled && arrived.key === key ? arrived.sizes : EMPTY;
}
