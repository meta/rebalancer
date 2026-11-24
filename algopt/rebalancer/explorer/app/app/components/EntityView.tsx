'use client';

import {useCallback, useEffect, useMemo, useRef, useState} from 'react';

import {Box, Paper, Skeleton} from '@mui/material';
import {useSearchParams} from 'next/navigation';

import PageHead, {PageContainer} from '@/app/components/shell/PageHead';
import FilterChips from '@/app/components/ui/FilterChips';
import Tag from '@/app/components/ui/Tag';
import {EmptyState, ErrorState} from '@/app/components/ui/StateViews';
import {
  asArray,
  normalizeFilters,
  parseQs,
  safeInt,
  stringifyQs,
  useUrlStateSync,
} from '@/lib/url-state';

import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {useRebalancerHandle} from '@/lib/contexts/RebalancerHandleContext';
import {formatCount, isTypeaheadColumn} from '@/lib/format';
import {fetchEntityData} from '@/lib/rebalancer-explorer-api';
import type {
  ColumnDescription,
  Filter,
  Result,
} from '@/lib/rebalancer-explorer-types';
import {OrderDirection} from '@/lib/rebalancer-explorer-types';

import EntityFilter from './EntityFilter';
import GroupBySelector from './GroupBySelector';
import EntityTable from './EntityTable';
import MetricDistributionSection from './MetricDistributionSection';
import ShowColumnsSelector from './ShowColumnsSelector';

/**
 * ViewState represents the complete state of the entity view,
 * mirroring the www RebalancerExplorerTable ViewState.
 */
export interface ViewState {
  offset: number;
  limit: number;
  orderColumn: string | null;
  orderDirection: OrderDirection;
  filters: Filter[];
  groupByColumns: string[];
  showColumns: string[];
}

const DEFAULT_PAGE_SIZE = 25;

// --- URL <-> ViewState helpers ---

function viewStateFromSearchParams(searchParams: URLSearchParams): ViewState {
  const parsed = parseQs(searchParams);

  return {
    offset: safeInt(parsed.offset, 0),
    limit: safeInt(parsed.limit, DEFAULT_PAGE_SIZE),
    orderColumn:
      typeof parsed.orderColumn === 'string' ? parsed.orderColumn : null,
    orderDirection: safeInt(parsed.orderDirection, 0) as OrderDirection,
    filters: normalizeFilters(parsed.filters),
    groupByColumns: asArray<string>(parsed.groupBy, []),
    showColumns: asArray<string>(parsed.showColumns, []),
  };
}

function searchParamsFromViewState(viewState: ViewState): string {
  const obj: Record<string, unknown> = {};

  if (viewState.offset !== 0) {
    obj.offset = viewState.offset;
  }
  if (viewState.limit !== DEFAULT_PAGE_SIZE) {
    obj.limit = viewState.limit;
  }
  if (viewState.orderColumn != null) {
    obj.orderColumn = viewState.orderColumn;
  }
  if (viewState.orderDirection !== 0) {
    obj.orderDirection = viewState.orderDirection;
  }
  if (viewState.filters.length > 0) {
    obj.filters = viewState.filters;
  }
  if (viewState.groupByColumns.length > 0) {
    obj.groupBy = viewState.groupByColumns;
  }
  if (viewState.showColumns.length > 0) {
    obj.showColumns = viewState.showColumns;
  }

  return stringifyQs(obj);
}

// --- Component ---

interface EntityViewProps {
  entityName: string;
}

export default function EntityView({entityName}: EntityViewProps) {
  const {
    handle,
    loading: handleLoading,
    error: handleError,
  } = useRebalancerHandle();
  const {metadata} = useProblemMetadata();
  const searchParams = useSearchParams();

  // Initialize ViewState from URL search params
  const initialViewState = useMemo(
    () => viewStateFromSearchParams(searchParams),
    // Only compute initial state once on mount
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [],
  );
  const [viewState, setViewState] = useState<ViewState>(initialViewState);

  // Data state
  const [columnDescriptions, setColumnDescriptions] = useState<
    ColumnDescription[] | null
  >(null);
  const [result, setResult] = useState<Result | null>(null);
  const [dataLoading, setDataLoading] = useState(false);
  const [dataError, setDataError] = useState<string | null>(null);

  // Sync ViewState to URL search params
  useUrlStateSync(() => searchParamsFromViewState(viewState), [viewState]);

  // Map column names to known backend entity names for typeahead lookups.
  // Column names like "src.region_lsst_pool_scope" must be mapped to the actual
  // entity name "region_lsst_pool_scope" that the backend's getTypeahead recognizes.
  const columnTypeaheadEntities = useMemo(() => {
    if (metadata == null || columnDescriptions == null) return undefined;

    const knownEntities = new Set<string>();
    knownEntities.add(metadata.objectName);
    knownEntities.add(metadata.containerName);
    for (const scope of metadata.scopeNames) {
      knownEntities.add(scope);
    }
    for (const partition of metadata.partitionNames) {
      knownEntities.add(partition);
    }

    const entities: Record<string, string> = {};
    for (const col of columnDescriptions) {
      if (!isTypeaheadColumn(col.type)) continue;

      if (knownEntities.has(col.name)) {
        entities[col.name] = col.name;
        continue;
      }

      // Strip prefix (e.g. "src.region_lsst_pool_scope" -> "region_lsst_pool_scope")
      const dotIndex = col.name.indexOf('.');
      if (dotIndex >= 0) {
        const suffix = col.name.substring(dotIndex + 1);
        if (knownEntities.has(suffix)) {
          entities[col.name] = suffix;
        }
      }
    }

    return entities;
  }, [metadata, columnDescriptions]);

  // Partitions have no entity page, so their values are not linked.
  const linkEntities = useMemo(() => {
    if (columnTypeaheadEntities == null || metadata == null) {
      return undefined;
    }
    return Object.fromEntries(
      Object.entries(columnTypeaheadEntities).filter(
        ([, entity]) => !metadata.partitionNames.includes(entity),
      ),
    );
  }, [columnTypeaheadEntities, metadata]);

  // Debounced fetch: fetch entity data on mount, entity change, or viewState change
  const debounceTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const fetchData = useCallback(async () => {
    if (handle == null) {
      return;
    }

    setDataLoading(true);
    setDataError(null);

    try {
      const response = await fetchEntityData(handle, {
        entity: entityName,
        filter:
          viewState.filters.length > 0
            ? {rules: viewState.filters.flatMap(f => f.rules)}
            : undefined,
        order:
          viewState.orderColumn != null
            ? {
                columns: [
                  {
                    name: viewState.orderColumn,
                    direction: viewState.orderDirection,
                  },
                ],
              }
            : undefined,
        page: {
          offset: viewState.offset,
          limit: viewState.limit,
        },
        group:
          viewState.groupByColumns.length > 0
            ? {columns: viewState.groupByColumns}
            : undefined,
      });

      setResult(response.result);

      // Capture column descriptions from the first successful response
      if (columnDescriptions == null && response.result.columns.length > 0) {
        setColumnDescriptions(response.result.columns);
      }
    } catch (err: unknown) {
      setDataError(
        err instanceof Error ? err.message : 'Failed to fetch entity data',
      );
    } finally {
      setDataLoading(false);
    }
  }, [handle, entityName, viewState, columnDescriptions]);

  useEffect(() => {
    if (handle == null) {
      return;
    }

    // Debounce rapid ViewState changes (300ms)
    if (debounceTimerRef.current != null) {
      clearTimeout(debounceTimerRef.current);
    }

    debounceTimerRef.current = setTimeout(() => {
      void fetchData();
    }, 300);

    return () => {
      if (debounceTimerRef.current != null) {
        clearTimeout(debounceTimerRef.current);
      }
    };
  }, [handle, fetchData]);

  // --- Loading / error states ---

  if (handleLoading) {
    return (
      <div className="p-4 space-y-4">
        <Skeleton variant="rectangular" height={48} />
        <Skeleton variant="rectangular" height={400} />
      </div>
    );
  }

  if (handleError != null) {
    return (
      <div className="p-4">
        <ErrorState>{handleError}</ErrorState>
      </div>
    );
  }

  if (handle == null) {
    return (
      <div className="p-4">
        <EmptyState title="No handle available" />
      </div>
    );
  }

  // Which kind of entity this page lists, from metadata names only.
  const kind =
    metadata == null
      ? null
      : entityName === metadata.objectName
        ? 'object'
        : entityName === metadata.containerName
          ? 'container'
          : metadata.scopeNames.includes(entityName)
            ? 'scope'
            : metadata.dynamicDimensionNames.includes(entityName)
              ? 'dynamic dimension'
              : null;
  const isGrouped = viewState.groupByColumns.length > 0;

  return (
    <PageContainer>
      <PageHead
        title={entityName}
        sub={
          <Box
            component="span"
            sx={{display: 'inline-flex', alignItems: 'center', gap: 1}}>
            {kind != null && <Tag>{kind}</Tag>}
            {result != null &&
              `${formatCount(result.totalCount)} ${isGrouped ? 'group' : 'row'}${result.totalCount === 1 ? '' : 's'}`}
          </Box>
        }
      />
      {dataError != null && (
        <ErrorState onClose={() => setDataError(null)}>{dataError}</ErrorState>
      )}

      {/* Filter, group and columns: the page's toolbar. */}
      <Paper variant="outlined">
        <Box
          sx={{
            px: 2,
            py: 1.5,
            display: 'flex',
            flexDirection: 'column',
            gap: 1.5,
          }}>
          {columnDescriptions != null && columnDescriptions.length > 0 && (
            <EntityFilter
              columnDescriptions={columnDescriptions}
              filters={viewState.filters}
              onFiltersChange={filters =>
                setViewState(prev => ({...prev, filters, offset: 0}))
              }
              entityName={entityName}
              columnTypeaheadEntities={columnTypeaheadEntities}
            />
          )}
          <FilterChips
            filters={viewState.filters}
            onChange={filters =>
              setViewState(prev => ({...prev, filters, offset: 0}))
            }
          />
          <Box sx={{display: 'flex', flexWrap: 'wrap', gap: 1.5}}>
            <GroupBySelector
              columnDescriptions={columnDescriptions ?? []}
              groupByColumns={viewState.groupByColumns}
              onGroupByChange={groupByColumns =>
                setViewState(prev => ({
                  ...prev,
                  groupByColumns,
                  offset: 0,
                  orderColumn:
                    groupByColumns.length > 0 ? groupByColumns[0] : null,
                  orderDirection: OrderDirection.ASCENDING,
                }))
              }
            />
            <ShowColumnsSelector
              columnDescriptions={columnDescriptions ?? []}
              groupByColumns={viewState.groupByColumns}
              showColumns={viewState.showColumns}
              onShowColumnsChange={showColumns =>
                setViewState(prev => ({...prev, showColumns}))
              }
            />
          </Box>
        </Box>
      </Paper>

      {dataLoading && result == null ? (
        <div className="space-y-2">
          <Skeleton variant="rectangular" height={48} />
          <Skeleton variant="rectangular" height={400} />
        </div>
      ) : (
        <EntityTable
          result={result}
          viewState={viewState}
          onViewStateChange={setViewState}
          loading={dataLoading}
          entityName={entityName}
          linkEntities={linkEntities}
        />
      )}

      <MetricDistributionSection
        entityName={entityName}
        columnDescriptions={columnDescriptions ?? []}
      />
    </PageContainer>
  );
}
