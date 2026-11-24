/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

'use client';

import {createContext, useContext, useEffect, useState} from 'react';

import type {Handle} from '@/lib/rebalancer-explorer-types';
import {fetchHandle} from '@/lib/rebalancer-explorer-api';

interface RebalancerHandleContextValue {
  runId: string | null;
  handle: Handle | null;
  loading: boolean;
  error: string | null;
}

const RebalancerHandleContext = createContext<RebalancerHandleContextValue>({
  runId: null,
  handle: null,
  loading: true,
  error: null,
});

export function RebalancerHandleProvider({
  runId,
  children,
}: {
  runId: string;
  children: React.ReactNode;
}) {
  const [handle, setHandle] = useState<Handle | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;

    setHandle(null);
    setLoading(true);
    setError(null);

    fetchHandle(runId)
      .then(response => {
        if (!cancelled) {
          setHandle(response.handle);
          setLoading(false);
        }
      })
      .catch((err: unknown) => {
        if (!cancelled) {
          setError(
            err instanceof Error ? err.message : 'Failed to fetch handle',
          );
          setLoading(false);
        }
      });

    return () => {
      cancelled = true;
    };
  }, [runId]);

  return (
    <RebalancerHandleContext.Provider value={{runId, handle, loading, error}}>
      {children}
    </RebalancerHandleContext.Provider>
  );
}

export function useRebalancerHandle(): RebalancerHandleContextValue {
  return useContext(RebalancerHandleContext);
}
