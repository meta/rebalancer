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

import RunGateCard from '@/app/components/shell/RunGateCard';
import type {ProblemMetadata} from '@/lib/rebalancer-explorer-types';
import {fetchProblemMetadata} from '@/lib/rebalancer-explorer-api';

import {useRebalancerHandle} from './RebalancerHandleContext';

interface ProblemMetadataContextValue {
  metadata: ProblemMetadata | null;
  loading: boolean;
  error: string | null;
}

const ProblemMetadataContext =
  createContext<ProblemMetadataContextValue | null>(null);

export function ProblemMetadataProvider({
  children,
}: {
  children: React.ReactNode;
}) {
  const {handle} = useRebalancerHandle();

  const [metadata, setMetadata] = useState<ProblemMetadata | null>(null);
  const [loading, setLoading] = useState(handle != null);
  const [error, setError] = useState<string | null>(null);

  // Reset when the run (handle) changes, during render so children never see
  // stale data from the previous run.
  const [prevHandle, setPrevHandle] = useState(handle);
  if (handle !== prevHandle) {
    setPrevHandle(handle);
    setMetadata(null);
    setError(null);
    setLoading(handle != null);
  }

  useEffect(() => {
    if (handle == null) {
      return;
    }

    let cancelled = false;

    fetchProblemMetadata(handle)
      .then(response => {
        if (!cancelled) {
          setMetadata(response.metadata);
          setLoading(false);
        }
      })
      .catch((err: unknown) => {
        if (!cancelled) {
          setError(
            err instanceof Error
              ? err.message
              : 'Failed to fetch problem metadata',
          );
          setLoading(false);
        }
      });

    return () => {
      cancelled = true;
    };
  }, [handle]);

  if (error != null) {
    return (
      <RunGateCard
        step="metadata"
        state="error"
        title="Error"
        status="ERROR"
        statusTone="bad">
        {error}
      </RunGateCard>
    );
  }

  // Spinner only while a run is actually loading; with no run (handle null)
  // there is nothing to load, so don't spin forever.
  if (handle != null && (loading || metadata == null)) {
    return <RunGateCard step="metadata" title="Loading problem metadata..." />;
  }

  return (
    <ProblemMetadataContext.Provider value={{metadata, loading, error}}>
      {children}
    </ProblemMetadataContext.Provider>
  );
}

export function useProblemMetadata(): ProblemMetadataContextValue {
  const context = useContext(ProblemMetadataContext);
  if (context == null) {
    throw new Error(
      'useProblemMetadata must be used within a ProblemMetadataProvider',
    );
  }
  return context;
}
