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

import {useCallback, useEffect, useRef, useState} from 'react';

import RunGateCard from '@/app/components/shell/RunGateCard';
import {fetchSandboxStatus} from '@/lib/rebalancer-explorer-api';
import {SandboxStatus} from '@/lib/rebalancer-explorer-types';

import {useRebalancerHandle} from './RebalancerHandleContext';

const POLL_INTERVAL_MS = 3000;

const STATUS_LABELS: Record<SandboxStatus, string> = {
  [SandboxStatus.NOT_LOADED]: 'NOT_LOADED',
  [SandboxStatus.LOADING]: 'LOADING',
  [SandboxStatus.LOADED]: 'LOADED',
};

export function WaitForSandbox({children}: {children: React.ReactNode}) {
  const {
    handle,
    loading: handleLoading,
    error: handleError,
  } = useRebalancerHandle();

  const [status, setStatus] = useState<SandboxStatus | null>(null);
  const [error, setError] = useState<string | null>(null);
  const timeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const clearPolling = useCallback(() => {
    if (timeoutRef.current != null) {
      clearTimeout(timeoutRef.current);
      timeoutRef.current = null;
    }
  }, []);

  useEffect(() => {
    if (handle == null) {
      return;
    }

    let cancelled = false;

    const poll = async () => {
      try {
        const response = await fetchSandboxStatus(handle);
        if (!cancelled) {
          setStatus(response.status);
          setError(null);
          if (
            response.status === SandboxStatus.LOADED ||
            response.status === SandboxStatus.NOT_LOADED
          ) {
            clearPolling();
          } else {
            timeoutRef.current = setTimeout(poll, POLL_INTERVAL_MS);
          }
        }
      } catch (err: unknown) {
        if (!cancelled) {
          setError(
            err instanceof Error
              ? err.message
              : 'Failed to fetch sandbox status',
          );
          clearPolling();
        }
      }
    };

    // Initial fetch
    poll();

    return () => {
      cancelled = true;
      clearPolling();
    };
  }, [handle, clearPolling]);

  if (handleLoading) {
    return <RunGateCard step="handle" title="Loading handle..." />;
  }

  if (handleError) {
    return (
      <RunGateCard
        step="handle"
        state="error"
        title="Error"
        status="ERROR"
        statusTone="bad">
        {handleError}
      </RunGateCard>
    );
  }

  if (error) {
    return (
      <RunGateCard
        step="sandbox"
        state="error"
        title="Error waiting for sandbox"
        status="ERROR"
        statusTone="bad">
        {error}
      </RunGateCard>
    );
  }

  if (status === SandboxStatus.NOT_LOADED) {
    return (
      <RunGateCard
        step="sandbox"
        state="warning"
        title="The sandbox has not loaded"
        status={STATUS_LABELS[status]}
        statusTone="warn">
        The sandbox has either expired or it failed to load. Try refreshing the
        page. Look at the server logs if the problem persists.
      </RunGateCard>
    );
  }

  // Sandbox is loaded
  if (status === SandboxStatus.LOADED) {
    return <>{children}</>;
  }

  // Still polling (LOADING, or the initial null state)
  return (
    <RunGateCard
      step="sandbox"
      title="Waiting for sandbox to load on the server..."
      status={status != null ? STATUS_LABELS[status] : undefined}
    />
  );
}
