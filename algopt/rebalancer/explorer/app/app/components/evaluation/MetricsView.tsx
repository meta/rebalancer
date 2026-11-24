'use client';

import {useMemo, useState} from 'react';

import {useParams, useRouter, useSearchParams} from 'next/navigation';

import AssignmentCard from '@/app/components/evaluation/AssignmentCard';
import type {Assignments} from '@/app/components/evaluation/AssignmentCard';
import MetricsTable from '@/app/components/evaluation/MetricsTable';
import PageHead, {PageContainer} from '@/app/components/shell/PageHead';
import {
  getDefaultMetricsViewState,
  type MetricsViewState,
} from '@/app/components/evaluation/MetricsTable.state';
import {
  buildMetricsSearchParams,
  parseMetricsSearchParams,
} from '@/app/components/evaluation/MetricsView.url';
import MoveSetsTable, {
  type MoveSetsViewState,
} from '@/app/components/evaluation/MoveSetsTable';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {useRebalancerHandle} from '@/lib/contexts/RebalancerHandleContext';
import {findMetricNameBySlug, slugifyMetricName} from '@/lib/metric-slug';
import {decodeRunId, encodeRunId} from '@/lib/run-id';
import {useUrlStateSync} from '@/lib/url-state';

export default function MetricsView() {
  const {runId: rawRunId, metricName: metricSlug} = useParams<{
    runId: string;
    metricName: string;
  }>();
  const runId = decodeRunId(rawRunId ?? '');
  const router = useRouter();
  const {metadata} = useProblemMetadata();
  const {handle} = useRebalancerHandle();
  const searchParams = useSearchParams();

  // Slug → canonical name lookup. Empty until metadata loads, which gates the
  // backend query in MetricsTable via its own `isValid` check.
  const metricName = useMemo(
    () =>
      findMetricNameBySlug(
        metricSlug ?? '',
        metadata?.metricCollectionNames ?? [],
      ) ?? '',
    [metricSlug, metadata?.metricCollectionNames],
  );

  // Parse URL state once on mount
  const urlState = useMemo(
    () => parseMetricsSearchParams(searchParams),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [],
  );

  // ---------------------------------------------------------------------------
  // Assignment state - initialized from URL, falling back to metadata defaults.
  // ProblemMetadataProvider gates the run UI until metadata loads, so it is
  // available here on the first render.
  // ---------------------------------------------------------------------------

  const [assignments, setAssignments] = useState<Assignments>(() => ({
    src: {
      base: urlState.srcBase,
      overrides: urlState.srcOverrides,
      step: urlState.srcStep,
    },
    dst: {
      base:
        urlState.dstBase ??
        (metadata?.hasFinalAssignment ? 'FINAL' : 'INITIAL'),
      overrides: urlState.dstOverrides,
      step: urlState.dstStep,
    },
  }));

  // ---------------------------------------------------------------------------
  // View state — initialized from URL, reset on metric name change
  // ---------------------------------------------------------------------------

  const [viewState, setViewState] = useState<MetricsViewState>(() => ({
    offset: urlState.offset,
    pageSize: urlState.pageSize,
    orderBy:
      urlState.orderColumn != null
        ? {column: urlState.orderColumn, direction: urlState.orderDirection}
        : null,
    filters: urlState.filters,
    groupBy: urlState.groupBy,
    showColumns: urlState.showColumns,
  }));

  // Reset view state when metric name changes via soft navigation
  const [prevMetricName, setPrevMetricName] = useState(metricName);
  if (metricName !== prevMetricName) {
    setPrevMetricName(metricName);
    if (prevMetricName !== '') {
      setViewState(getDefaultMetricsViewState());
    }
  }

  // ---------------------------------------------------------------------------
  // MoveSets table view state — initialized from URL
  // ---------------------------------------------------------------------------

  const [moveSetsState, setMoveSetsState] = useState<MoveSetsViewState>(() => ({
    partition: urlState.msPartition,
    scope: urlState.msScope,
    objectives: urlState.msObjectives,
    filters: urlState.msFilters,
    offset: urlState.msOffset,
    pageSize: urlState.msPageSize,
    orderColumn: urlState.msOrderColumn,
    orderDirection: urlState.msOrderDirection,
    isMinimized: urlState.msMinimized,
  }));

  // ---------------------------------------------------------------------------
  // URL sync
  // ---------------------------------------------------------------------------

  useUrlStateSync(
    () => buildMetricsSearchParams(assignments, viewState, moveSetsState),
    [assignments, viewState, moveSetsState],
  );

  // ---------------------------------------------------------------------------
  // Metric name selector
  // ---------------------------------------------------------------------------

  const metricCollectionNames = metadata?.metricCollectionNames ?? [];

  const handleMetricNameChange = (
    _event: React.SyntheticEvent,
    value: string | null,
  ) => {
    if (value != null) {
      router.push(
        `/run/${encodeRunId(runId)}/metrics/${slugifyMetricName(value)}`,
      );
    }
  };

  // ---------------------------------------------------------------------------
  // Render
  // ---------------------------------------------------------------------------

  return (
    <PageContainer>
      <PageHead
        title={metricName ?? metricSlug}
        sub="Evaluate metrics (e.g., container utilizations) w.r.t. different assignments."
      />
      <AssignmentCard
        assignments={assignments}
        onAssignmentsChange={setAssignments}
      />

      {handle != null && (
        <MoveSetsTable
          assignments={assignments}
          viewState={moveSetsState}
          onViewStateChange={setMoveSetsState}
        />
      )}

      <MetricsTable
        metricName={metricName}
        viewState={viewState}
        onViewStateChange={setViewState}
        assignments={assignments}
        metricCollectionNames={metricCollectionNames}
        onMetricNameChange={handleMetricNameChange}
      />
    </PageContainer>
  );
}
