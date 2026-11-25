'use client';

import {useCallback, useEffect, useMemo, useRef, useState} from 'react';

import {Box, Skeleton} from '@mui/material';
import {useSearchParams} from 'next/navigation';

import EntityFacets from '@/app/components/entity/EntityFacets';
import EntityList from '@/app/components/entity/EntityList';
import FlowsCard from '@/app/components/entity/FlowsCard';
import {
  searchParamsFromViewState,
  viewStateFromSearchParams,
  type ViewState,
} from '@/app/components/entity/entity-view-state';
import {useRunMoves} from '@/app/components/entity/useRunChanges';
import PageHead, {PageContainer} from '@/app/components/shell/PageHead';
import type {ExtraChip} from '@/app/components/ui/FilterChips';
import {EmptyState, ErrorState} from '@/app/components/ui/StateViews';
import Tag from '@/app/components/ui/Tag';
import UpdatingOverlay from '@/app/components/UpdatingOverlay';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {useRebalancerHandle} from '@/lib/contexts/RebalancerHandleContext';
import {
  buildFields,
  fieldWidth,
  fitToWidth,
  inDisplayOrder,
  isPickable,
  normalizeShown,
  profileFields,
  rankFields,
  searchRule,
  type EntityField,
} from '@/lib/entity-fields';
import {isTypeaheadColumn} from '@/lib/format';
import {fetchEntityData} from '@/lib/rebalancer-explorer-api';
import type {
  ColumnDescription,
  FilterRule,
  ProblemMetadata,
  Result,
  RowData,
} from '@/lib/rebalancer-explorer-types';
import {flows, itemMoves, type Flow} from '@/lib/run-changes';
import {useUrlStateSync} from '@/lib/url-state';

import MetricDistributionSection from './MetricDistributionSection';

type EntityKind = 'object' | 'container' | 'scope' | 'dynamic dimension';

// An object's 0/1 column that marks it fixed in place.
const MOVABLE_COLUMN = 'is_movable';

function kindOf(
  metadata: ProblemMetadata | null,
  entityName: string,
): EntityKind | null {
  if (metadata == null) {
    return null;
  }
  if (entityName === metadata.objectName) {
    return 'object';
  }
  if (entityName === metadata.containerName) {
    return 'container';
  }
  if (metadata.scopeNames.includes(entityName)) {
    return 'scope';
  }
  if (metadata.dynamicDimensionNames.includes(entityName)) {
    return 'dynamic dimension';
  }
  return null;
}

function kindHelp(kind: EntityKind, m: ProblemMetadata): string {
  switch (kind) {
    case 'object':
      return `What the solver places. Each sits in one ${m.containerName}; a move changes which.`;
    case 'container':
      return `Where ${m.objectName} rows go. Its utilization is the sum of its ${m.objectName} dimensions.`;
    case 'scope':
      return `A grouping of ${m.containerName} rows. Each item's utilization sums its ${m.containerName} rows.`;
    case 'dynamic dimension':
      return `A value looked up per ${m.objectName} and scope item, so it changes when the ${m.objectName} moves.`;
  }
}

/**
 * The automatic columns, best first. An object's movable flag shows as a
 * pill, and its scope assignments follow from its container, so they come
 * last.
 */
function autoOrder(
  kind: EntityKind | null,
  metadata: ProblemMetadata | null,
  fields: readonly EntityField[],
  rows: readonly RowData[],
): string[] {
  const object = kind === 'object';
  return rankFields(
    fields,
    profileFields(fields, rows),
    rows.length,
    new Set(object ? [MOVABLE_COLUMN] : []),
    new Set(
      object
        ? fields
            .filter(
              f => f.kind === 'assignment' && f.key !== metadata?.containerName,
            )
            .map(f => f.key)
        : [],
    ),
  );
}

// Width of the list card before it is measured.
const DEFAULT_LIST_WIDTH = 1100;
// The card's horizontal padding and borders.
const LIST_CHROME = 4;
const MOVES_WIDTH = 116;
const CHANGE_WIDTH = 232;

function useElementWidth() {
  const [el, setEl] = useState<HTMLElement | null>(null);
  const [width, setWidth] = useState<number | null>(null);
  useEffect(() => {
    if (el == null) {
      return;
    }
    const observer = new ResizeObserver(entries => {
      const w = Math.round(entries[0]?.contentRect.width ?? 0);
      setWidth(prev => (prev === w ? prev : w));
    });
    observer.observe(el);
    return () => observer.disconnect();
  }, [el]);
  return {ref: setEl, width};
}

export default function EntityView({entityName}: {entityName: string}) {
  const {
    handle,
    runId,
    loading: handleLoading,
    error: handleError,
  } = useRebalancerHandle();
  const {metadata} = useProblemMetadata();
  const searchParams = useSearchParams();

  const [viewState, setViewState] = useState<ViewState>(() =>
    viewStateFromSearchParams(searchParams),
  );
  // The entity's own columns, from the first result: the filter editor's
  // fields and the typeahead lookups.
  const [columnDescriptions, setColumnDescriptions] = useState<
    ColumnDescription[] | null
  >(null);
  const [data, setData] = useState<{result: Result; key: string} | null>(null);
  // The automatic columns' order, fixed per query so that paging and
  // sorting do not move columns around.
  const [ranked, setRanked] = useState<{key: string; keys: string[]} | null>(
    null,
  );
  const [dataLoading, setDataLoading] = useState(false);
  const [dataError, setDataError] = useState<string | null>(null);
  const {ref: listRef, width: listWidth} = useElementWidth();

  useUrlStateSync(() => searchParamsFromViewState(viewState), [viewState]);

  const kind = kindOf(metadata, entityName);
  const {moves, loading: movesLoading} = useRunMoves(
    handle,
    metadata,
    kind === 'object' || kind === 'container' || kind === 'scope',
  );
  // The scope (or the container) whose items the moves are counted on.
  const moveKey =
    kind === 'container'
      ? (metadata?.containerName ?? null)
      : kind === 'scope'
        ? entityName
        : null;
  const itemMoveCounts = useMemo(
    () =>
      moves?.moved != null && moveKey != null
        ? itemMoves(moves.moved, moveKey)
        : null,
    [moves, moveKey],
  );

  // Flows between containers (objects and containers) or the scope's items.
  const flowKey =
    kind === 'object' ? (metadata?.containerName ?? null) : moveKey;
  const flowList = useMemo(
    () =>
      moves?.moved != null && flowKey != null
        ? flows(moves.moved, flowKey)
        : [],
    [moves, flowKey],
  );

  // The rows the changed view keeps: moved objects, or the items that moved
  // objects left or entered.
  const movedNames = useMemo(() => {
    if (moves?.moved == null || moves.count === 0) {
      return null;
    }
    if (kind === 'object') {
      return Array.from(moves.moved.keys());
    }
    return itemMoveCounts != null && itemMoveCounts.size > 0
      ? Array.from(itemMoveCounts.keys())
      : null;
  }, [moves, kind, itemMoveCounts]);
  // On by default when the run has changed rows.
  const movedOnly = (viewState.movedOnly ?? true) && movedNames != null;
  const waitingForMoves = viewState.movedOnly !== false && movesLoading;

  // A scope's item count, for "All N".
  const [scopeTotal, setScopeTotal] = useState<{
    entity: string;
    n: number;
  } | null>(null);
  useEffect(() => {
    if (handle == null || kind !== 'scope') {
      return;
    }
    let cancelled = false;
    fetchEntityData(handle, {entity: entityName, page: {offset: 0, limit: 1}})
      .then(({result}) => {
        if (!cancelled) {
          setScopeTotal({entity: entityName, n: result.totalCount});
        }
      })
      .catch(() => {});
    return () => {
      cancelled = true;
    };
  }, [handle, kind, entityName]);

  const pkColumn =
    columnDescriptions?.find(c => c.primaryKey)?.name ??
    (kind === 'dynamic dimension' && metadata != null
      ? metadata.objectName
      : entityName);
  const search = viewState.search.trim();

  // A new query (not a new page or order) picks the automatic columns again.
  const queryKey = JSON.stringify([
    viewState.filters,
    viewState.groupByColumns,
    search,
    movedOnly,
  ]);

  // The changed items, busiest first, when nothing else decides the order:
  // the page's names are picked here and the server returns just those rows.
  const byActivity = useMemo(() => {
    if (
      !movedOnly ||
      itemMoveCounts == null ||
      movedNames == null ||
      viewState.orderColumn != null ||
      viewState.filters.length > 0 ||
      search !== '' ||
      viewState.groupByColumns.length > 0
    ) {
      return null;
    }
    const activity = (n: string) => {
      const m = itemMoveCounts.get(n);
      return m == null ? 0 : m.out + m.in;
    };
    return [...movedNames].sort(
      (a, b) => activity(b) - activity(a) || a.localeCompare(b),
    );
  }, [
    movedOnly,
    itemMoveCounts,
    movedNames,
    viewState.orderColumn,
    viewState.filters,
    search,
    viewState.groupByColumns,
  ]);

  const seqRef = useRef(0);
  const fetchData = useCallback(async () => {
    if (handle == null || waitingForMoves) {
      return;
    }
    const seq = ++seqRef.current;
    setDataLoading(true);
    setDataError(null);
    const rules: FilterRule[] = viewState.filters.flatMap(f => f.rules);
    if (search !== '') {
      rules.push(searchRule(pkColumn, search));
    }
    const pageNames =
      byActivity?.slice(viewState.offset, viewState.offset + viewState.limit) ??
      null;
    if (pageNames != null) {
      rules.push({stringAny: {column: pkColumn, values: pageNames}});
    } else if (movedOnly && movedNames != null) {
      rules.push({stringAny: {column: pkColumn, values: movedNames}});
    }
    try {
      const response = await fetchEntityData(handle, {
        entity: entityName,
        filter: rules.length > 0 ? {rules} : undefined,
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
        page:
          pageNames != null
            ? {offset: 0, limit: viewState.limit}
            : {offset: viewState.offset, limit: viewState.limit},
        group:
          viewState.groupByColumns.length > 0
            ? {columns: viewState.groupByColumns}
            : undefined,
      });
      if (seq !== seqRef.current) {
        return;
      }
      let result = response.result;
      if (pageNames != null && byActivity != null) {
        const pk = result.columns.findIndex(c => c.primaryKey);
        const rank = new Map(pageNames.map((n, i) => [n, i]));
        const at = (r: RowData) =>
          rank.get(r.cells[pk]?.stringValue ?? '') ?? pageNames.length;
        result = {
          ...result,
          rows: [...result.rows].sort((x, y) => at(x) - at(y)),
          totalCount: byActivity.length,
        };
      }
      setData({result, key: queryKey});
      if (viewState.groupByColumns.length === 0) {
        setColumnDescriptions(prev =>
          prev == null && result.columns.length > 0 ? result.columns : prev,
        );
      }
      setRanked(prev => {
        if (prev?.key === queryKey) {
          return prev;
        }
        return {
          key: queryKey,
          keys: autoOrder(
            kind,
            metadata,
            buildFields(result.columns),
            result.rows,
          ),
        };
      });
    } catch (err: unknown) {
      if (seq === seqRef.current) {
        setDataError(
          err instanceof Error ? err.message : 'Failed to fetch entity data',
        );
      }
    } finally {
      if (seq === seqRef.current) {
        setDataLoading(false);
      }
    }
  }, [
    handle,
    waitingForMoves,
    byActivity,
    viewState,
    search,
    pkColumn,
    movedOnly,
    movedNames,
    entityName,
    queryKey,
    kind,
    metadata,
  ]);

  // A page opened grouped still needs the entity's own columns.
  const needsSchema =
    columnDescriptions == null && viewState.groupByColumns.length > 0;
  useEffect(() => {
    if (handle == null || !needsSchema) {
      return;
    }
    let cancelled = false;
    fetchEntityData(handle, {entity: entityName, page: {offset: 0, limit: 1}})
      .then(({result}) => {
        if (!cancelled && result.columns.length > 0) {
          setColumnDescriptions(prev => prev ?? result.columns);
        }
      })
      .catch(() => {
        // The filter editor stays hidden; the list still works.
      });
    return () => {
      cancelled = true;
    };
  }, [handle, needsSchema, entityName]);

  // Typing and clicking settle for 300 ms before a fetch.
  useEffect(() => {
    if (handle == null) {
      return;
    }
    const timer = setTimeout(() => void fetchData(), 300);
    return () => clearTimeout(timer);
  }, [handle, fetchData]);

  // Column → the entity its values name, for typeahead lookups: "src.region"
  // looks up "region".
  const columnTypeaheadEntities = useMemo(() => {
    if (metadata == null || columnDescriptions == null) {
      return undefined;
    }
    const known = new Set([
      metadata.objectName,
      metadata.containerName,
      ...metadata.scopeNames,
      ...metadata.partitionNames,
    ]);
    const entities: Record<string, string> = {};
    for (const col of columnDescriptions) {
      if (!isTypeaheadColumn(col.type)) {
        continue;
      }
      const dot = col.name.indexOf('.');
      const name = known.has(col.name)
        ? col.name
        : dot >= 0
          ? col.name.substring(dot + 1)
          : null;
      if (name != null && known.has(name)) {
        entities[col.name] = name;
      }
    }
    return entities;
  }, [metadata, columnDescriptions]);

  // Partitions have no entity page, so their values are not linked.
  const linkEntities = useMemo(
    () =>
      columnTypeaheadEntities == null || metadata == null
        ? {}
        : Object.fromEntries(
            Object.entries(columnTypeaheadEntities).filter(
              ([, entity]) => !metadata.partitionNames.includes(entity),
            ),
          ),
    [columnTypeaheadEntities, metadata],
  );

  const result = data?.result ?? null;
  const rowCount = result?.rows.length ?? 0;
  const fields = useMemo(
    () => (result != null ? buildFields(result.columns) : []),
    [result],
  );
  const profiles = useMemo(
    () => profileFields(fields, result?.rows ?? []),
    [fields, result],
  );
  const grouped = viewState.groupByColumns.length > 0;
  const byKey = useMemo(() => new Map(fields.map(f => [f.key, f])), [fields]);

  // The row's label: its name, or the grouped-by columns and their count.
  const labels = useMemo((): EntityField[] => {
    if (grouped) {
      const keys = new Set(normalizeShown(viewState.groupByColumns, fields));
      return fields.filter(f => keys.has(f.key) || f.kind === 'count');
    }
    const name =
      fields.find(f => f.primaryKey) ?? fields.find(f => f.kind === 'name');
    return name != null ? [name] : [];
  }, [grouped, viewState.groupByColumns, fields]);
  const labelKeys = new Set(labels.map(f => f.key));
  const pickable = fields.filter(f => isPickable(f) && !labelKeys.has(f.key));

  const containerField =
    kind === 'object' && !grouped && metadata != null
      ? (fields.find(f => f.key === metadata.containerName && f.a != null) ??
        null)
      : null;
  const movableField =
    kind === 'object' && !grouped
      ? (fields.find(f => f.key === MOVABLE_COLUMN && f.numeric) ?? null)
      : null;
  const locked = new Set(containerField != null ? [containerField.key] : []);

  const pickableKeys = new Set(pickable.map(f => f.key));
  const explicit = normalizeShown(viewState.showColumns, fields).filter(k =>
    pickableKeys.has(k),
  );
  const auto = explicit.length === 0;

  const moveCounts = !grouped ? itemMoveCounts : null;
  const showChange =
    (kind === 'container' || kind === 'scope') &&
    fields.some(f => f.kind === 'utilization' && f.a != null);

  let shownKeys: Set<string>;
  if (auto) {
    const order =
      ranked?.key === queryKey
        ? ranked.keys
        : autoOrder(kind, metadata, fields, result?.rows ?? []);
    const widthOf = (k: string) => {
      const f = byKey.get(k);
      return f != null ? fieldWidth(f) : 0;
    };
    const fixed =
      labels.reduce(
        (s, f) => s + (f.kind === 'count' ? 96 : grouped ? 200 : 240),
        0,
      ) +
      (moveCounts != null ? MOVES_WIDTH : 0) +
      (showChange ? CHANGE_WIDTH : 0) +
      Array.from(locked).reduce((s, k) => s + widthOf(k), 0);
    const budget = (listWidth ?? DEFAULT_LIST_WIDTH) - LIST_CHROME - fixed;
    shownKeys = new Set([
      ...locked,
      ...fitToWidth(
        order.filter(k => pickableKeys.has(k) && !locked.has(k)),
        widthOf,
        budget,
      ),
    ]);
  } else {
    shownKeys = new Set([...locked, ...explicit]);
  }
  // An object's own container leads; then the rest by kind.
  const columns = [
    ...(containerField != null ? [containerField] : []),
    ...inDisplayOrder(
      pickable.filter(f => f !== containerField),
      shownKeys,
    ),
  ];

  const setShown = (keys: string[]) =>
    setViewState(prev => ({...prev, showColumns: keys}));
  const toggleField = (key: string) => {
    const current = columns.map(f => f.key).filter(k => !locked.has(k));
    setShown(
      current.includes(key)
        ? current.filter(k => k !== key)
        : [...current, key],
    );
  };
  const showField = (key: string) => {
    const current = columns.map(f => f.key).filter(k => !locked.has(k));
    if (!current.includes(key)) {
      setShown([...current, key]);
    }
  };

  // A flow narrows the list: objects by their two containers, items by name.
  // Picking the flow shown clears it.
  const flowRules = (f: Flow): FilterRule[] =>
    kind === 'object'
      ? [
          {stringAny: {column: `src.${flowKey}`, values: [f.src]}},
          {stringAny: {column: `dst.${flowKey}`, values: [f.dst]}},
        ]
      : [{stringAny: {column: pkColumn, values: [f.src, f.dst]}}];
  const isPicked = (f: Flow) =>
    viewState.filters.length === 1 &&
    JSON.stringify(viewState.filters[0].rules) === JSON.stringify(flowRules(f));
  const pickFlow = (f: Flow) =>
    setViewState(prev => ({
      ...prev,
      filters: isPicked(f) ? [] : [{rules: flowRules(f)}],
      offset: 0,
    }));

  const clearAll = () =>
    setViewState(prev => ({
      ...prev,
      filters: [],
      search: '',
      movedOnly: false,
      offset: 0,
    }));
  const extraChips: ExtraChip[] = [
    ...(search !== ''
      ? [
          {
            key: 'search',
            label: `${pkColumn} contains “${search}”`,
            onRemove: () =>
              setViewState(prev => ({...prev, search: '', offset: 0})),
          },
        ]
      : []),
    ...(movedOnly
      ? [
          {
            key: 'moved',
            label: kind === 'object' ? 'Moved' : 'Touched by moves',
            onRemove: () =>
              setViewState(prev => ({...prev, movedOnly: false, offset: 0})),
          },
        ]
      : []),
  ];

  if (handleLoading) {
    return (
      <PageContainer>
        <Skeleton variant="rectangular" height={48} />
        <Skeleton variant="rectangular" height={400} />
      </PageContainer>
    );
  }
  if (handleError != null) {
    return (
      <PageContainer>
        <ErrorState>{handleError}</ErrorState>
      </PageContainer>
    );
  }
  if (handle == null || runId == null) {
    return (
      <PageContainer>
        <EmptyState title="No handle available" />
      </PageContainer>
    );
  }

  const rowNoun = kind === 'dynamic dimension' ? 'values' : entityName;
  const show =
    movedNames != null && moves != null && metadata != null
      ? kind === 'object'
        ? {
            label: 'Moved',
            help: `${metadata.objectName} rows whose ${metadata.containerName} differs between the Initial and the Final assignment. Shown by default; All lists every row.`,
            movedCount: moves.count,
            total: metadata.numObjects,
          }
        : {
            label: 'Touched',
            help:
              kind === 'container'
                ? `${metadata.containerName} rows that a move leaves or enters, Initial → Final. Shown by default; All lists every row.`
                : `${entityName} items that ${metadata.objectName} rows moved into or out of, Initial → Final; a move inside one item does not change it. Shown by default; All lists every item.`,
            movedCount: movedNames.length,
            total:
              kind === 'container'
                ? metadata.numContainers
                : scopeTotal?.entity === entityName
                  ? scopeTotal.n
                  : null,
          }
      : null;

  return (
    <PageContainer>
      <PageHead
        title={entityName}
        sub={
          kind != null && metadata != null ? (
            <Box
              component="span"
              sx={{display: 'inline-flex', alignItems: 'center', gap: 1}}>
              <Tag>{kind}</Tag>
              {kindHelp(kind, metadata)}
            </Box>
          ) : undefined
        }
      />
      {dataError != null && (
        <ErrorState onClose={() => setDataError(null)}>{dataError}</ErrorState>
      )}
      <Box
        sx={{
          display: 'grid',
          gridTemplateColumns: '250px minmax(0, 1fr)',
          gap: 2,
          alignItems: 'start',
          '@media (max-width: 1180px)': {gridTemplateColumns: '1fr'},
        }}>
        <Box
          sx={{
            position: 'sticky',
            top: 120,
            maxHeight: 'calc(100vh - 136px)',
            overflowY: 'auto',
            '@media (max-width: 1180px)': {
              position: 'static',
              maxHeight: 'none',
            },
          }}>
          <EntityFacets
            nameLabel={pkColumn}
            search={viewState.search}
            onSearchChange={value =>
              setViewState(prev => ({...prev, search: value, offset: 0}))
            }
            show={
              show != null
                ? {
                    ...show,
                    moved: movedOnly,
                    // The changed rows are the default, so choosing them
                    // drops the parameter.
                    onChange: moved =>
                      setViewState(prev => ({
                        ...prev,
                        movedOnly: moved ? null : false,
                        offset: 0,
                      })),
                  }
                : null
            }
            fields={pickable}
            shown={shownKeys}
            locked={locked}
            auto={auto}
            onToggle={toggleField}
            onReset={() => setShown([])}
            profiles={profiles}
            rowCount={rowCount}
          />
        </Box>
        <Box
          ref={listRef}
          sx={{position: 'relative', minWidth: 0}}
          aria-busy={dataLoading}>
          {movedOnly && !grouped && flowKey != null && metadata != null && (
            <FlowsCard
              flows={flowList}
              itemEntity={flowKey}
              itemsLabel={
                kind === 'scope' ? `${flowKey} items` : `${flowKey} rows`
              }
              objectNoun={metadata.objectName}
              onPick={pickFlow}
              isPicked={isPicked}
            />
          )}
          {result == null ? (
            <Skeleton variant="rounded" height={480} />
          ) : (
            <>
              {dataLoading && <UpdatingOverlay />}
              <EntityList
                entityName={entityName}
                nameLabel={rowNoun}
                result={result}
                fields={fields}
                columns={columns}
                labels={labels}
                profiles={profiles}
                viewState={viewState}
                onViewStateChange={setViewState}
                rowEntity={
                  !grouped && kind !== 'dynamic dimension' ? entityName : null
                }
                handle={handle}
                runId={runId}
                linkEntities={linkEntities}
                objectStatus={
                  kind === 'object' && !grouped
                    ? {container: containerField, movable: movableField}
                    : null
                }
                moves={moveCounts}
                movesOrdered={byActivity != null}
                showChange={showChange}
                columnDescriptions={columnDescriptions ?? []}
                columnTypeaheadEntities={columnTypeaheadEntities}
                extraChips={extraChips}
                onClearAll={clearAll}
                onShowField={showField}
              />
            </>
          )}
        </Box>
      </Box>
      <MetricDistributionSection
        entityName={entityName}
        columnDescriptions={columnDescriptions ?? []}
      />
    </PageContainer>
  );
}
