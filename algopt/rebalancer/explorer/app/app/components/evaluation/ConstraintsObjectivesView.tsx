'use client';

import {
  useCallback,
  useDeferredValue,
  useEffect,
  useMemo,
  useRef,
  useState,
} from 'react';

import {useSearchParams} from 'next/navigation';

import {
  useDetailPane,
  useDetailPaneActions,
} from '@/app/components/detail-pane/DetailPaneContext';
import {Box} from '@mui/material';

import UpdatingOverlay from '@/app/components/UpdatingOverlay';
import PageHead, {PageContainer} from '@/app/components/shell/PageHead';
import {ErrorState, LoadingState} from '@/app/components/ui/StateViews';
import {assignmentShortLabel, toThriftAssignment} from '@/lib/assignment';
import {mergeOverrides} from '@/lib/move-selection-utils';
import {useUrlStateSync} from '@/lib/url-state';

import AssignmentCard from '@/app/components/evaluation/AssignmentCard';
import type {Assignments} from '@/app/components/evaluation/AssignmentCard';
import {
  buildRows,
  facetCounts,
  STATUS_LABEL,
  type COListState,
  type COQuery,
  type RowKind,
} from '@/app/components/evaluation/co-list';
import COFacets from '@/app/components/evaluation/COFacets';
import COList from '@/app/components/evaluation/COList';
import {
  buildCOSearchParams,
  parseCOSearchParams,
  type CODetailState,
} from '@/app/components/evaluation/ConstraintsObjectivesView.url';
import {
  compileEvaluationFilters,
  matchesEvaluationFilters,
} from '@/app/components/evaluation/evaluation-filters';
import {hasAnyPenalty} from '@/app/components/evaluation/evaluation-penalty';
import {joinEvaluationRows} from '@/app/components/evaluation/evaluation-rows';
import {
  getFilterRuleColumn,
  pruneFiltersByColumns,
} from '@/app/components/evaluation/MoveSetsTable.filters';
import MoveSetsTable, {
  type MoveSetsViewState,
} from '@/app/components/evaluation/MoveSetsTable';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {changedObjectiveNames} from '@/lib/overview';
import {useRebalancerHandle} from '@/lib/contexts/RebalancerHandleContext';
import {fetchEvaluation} from '@/lib/rebalancer-explorer-api';
import type {EvaluationResult, Filter} from '@/lib/rebalancer-explorer-types';
import {ExpressionType} from '@/lib/rebalancer-explorer-types';

// Change columns the moves table starts with, at most.
const MAX_DEFAULT_MOVE_OBJECTIVES = 4;

// ---------------------------------------------------------------------------
// Main component
// ---------------------------------------------------------------------------

export default function ConstraintsObjectivesView() {
  const {handle} = useRebalancerHandle();
  const {metadata} = useProblemMetadata();
  const searchParams = useSearchParams();

  // Parse URL state once on mount
  const urlState = useMemo(
    () => parseCOSearchParams(searchParams),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [],
  );

  // Assignments state - initialized from URL, falling back to metadata defaults.
  // ProblemMetadataProvider gates the run UI until metadata loads, so it is
  // available here on the first render.
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

  // Evaluation results
  const [srcEvaluation, setSrcEvaluation] = useState<EvaluationResult | null>(
    null,
  );
  const [dstEvaluation, setDstEvaluation] = useState<EvaluationResult | null>(
    null,
  );
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  // Changed or all rows — URL-synced
  const [hideAllUnchanged, setHideAllUnchanged] = useState(
    urlState.hideAllUnchanged,
  );

  // Evaluation filters — URL-synced, edited in the list's filter editor
  const [evalFilters, setEvalFilters] = useState<Filter[]>(
    urlState.evalFilters,
  );

  // Search, facets, grouping, sort and penalties of the list — URL-synced
  const [listState, setListState] = useState<COListState>(urlState.list);

  // MoveSets table view state — initialized from URL
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

  // ---- Pin to Initial → Final ----
  const [pinToInitialFinal, setPinToInitialFinal] = useState(false);

  // ---- Add moves to assignment callback ----
  const handleAddMovesToAssignment = useCallback(
    (
      moves: Array<{variable: string; container: string}>,
      target: 'src' | 'dst',
    ) => {
      setAssignments(prev => {
        const assignment = prev[target];
        const merged = mergeOverrides(assignment.overrides, moves);
        if (merged.length === assignment.overrides.length) {
          return prev;
        }
        const updated = {...assignment, overrides: merged};
        if (target === 'src') {
          return {src: updated, dst: prev.dst};
        }
        return {src: prev.src, dst: updated};
      });
    },
    [],
  );

  // Stable reference for pinned assignments — never changes, so downstream
  // consumers (MoveSetsTable) won't re-fetch when the user adds overrides.
  const pinnedAssignments: Assignments = useMemo(
    () => ({
      src: {base: 'INITIAL' as const, overrides: [], step: null},
      dst: {base: 'FINAL' as const, overrides: [], step: null},
    }),
    [],
  );

  // Compute effective assignments for the MoveSets table query.
  // When pinned, always show Initial→Final. Otherwise strip overrides
  // (the moves table shows base moves, not override-affected ones).
  const moveSetsQueryAssignments: Assignments = useMemo(() => {
    if (pinToInitialFinal) {
      return pinnedAssignments;
    }
    return {
      src: {...assignments.src, overrides: []},
      dst: {...assignments.dst, overrides: []},
    };
  }, [pinToInitialFinal, pinnedAssignments, assignments]);

  // When metadata loads, validate URL-derived moveSetsState. If the loaded
  // problem doesn't support per-move objective changes, wipe any URL-provided
  // objectives and prune filters referencing now-defunct objective columns.
  const [hasValidatedMoveSets, setHasValidatedMoveSets] = useState(false);
  if (!hasValidatedMoveSets && metadata != null) {
    setHasValidatedMoveSets(true);
    const isObjSelectorEnabled =
      metadata.canDisplayObjChangesInMoveSetsTable ?? false;
    if (!isObjSelectorEnabled && moveSetsState.objectives.length > 0) {
      setMoveSetsState(prev => {
        const colsToRemove = new Set<string>();
        for (const f of prev.filters) {
          for (const rule of f.rules) {
            const col = getFilterRuleColumn(rule);
            if (col != null && prev.objectives.some(obj => col.includes(obj))) {
              colsToRemove.add(col);
            }
          }
        }
        return {
          ...prev,
          objectives: [],
          offset: 0,
          filters:
            colsToRemove.size > 0
              ? pruneFiltersByColumns(prev.filters, colsToRemove)
              : prev.filters,
        };
      });
    }
  }

  // The moves table starts with a change column for each objective that
  // changed between A and B (in tuple order), so every move shows what it
  // did. A URL that chose the objectives, even none, keeps its choice.
  const [defaultObjectivesDone, setDefaultObjectivesDone] = useState(
    urlState.msObjectivesSet,
  );
  if (
    !defaultObjectivesDone &&
    metadata != null &&
    srcEvaluation != null &&
    dstEvaluation != null
  ) {
    setDefaultObjectivesDone(true);
    if (
      (metadata.canDisplayObjChangesInMoveSetsTable ?? false) &&
      moveSetsState.objectives.length === 0
    ) {
      const changed = changedObjectiveNames(
        srcEvaluation.expressions,
        dstEvaluation.expressions,
        MAX_DEFAULT_MOVE_OBJECTIVES,
      );
      if (changed.length > 0) {
        setMoveSetsState(prev => ({...prev, objectives: changed}));
      }
    }
  }

  const {open: openPane} = useDetailPaneActions();

  // Fetch evaluations when assignments or handle change. Only the latest
  // request may update state, so a slow earlier response cannot overwrite a
  // newer one.
  const requestId = useRef(0);
  const doFetch = useCallback(async () => {
    if (handle == null) {
      return;
    }

    const id = ++requestId.current;
    setLoading(true);
    setError(null);

    try {
      const [srcResp, dstResp] = await Promise.all([
        fetchEvaluation(handle, toThriftAssignment(assignments.src)),
        fetchEvaluation(handle, toThriftAssignment(assignments.dst)),
      ]);
      if (id !== requestId.current) {
        return;
      }
      setSrcEvaluation(srcResp.result);
      setDstEvaluation(dstResp.result);
    } catch (err: unknown) {
      if (id !== requestId.current) {
        return;
      }
      setError(
        err instanceof Error ? err.message : 'Failed to fetch evaluation',
      );
      setSrcEvaluation(null);
      setDstEvaluation(null);
    } finally {
      if (id === requestId.current) {
        setLoading(false);
      }
    }
  }, [handle, assignments]);

  useEffect(() => {
    doFetch();
  }, [doFetch]);

  const rows = useMemo(
    () =>
      srcEvaluation != null && dstEvaluation != null
        ? buildRows(srcEvaluation.expressions, dstEvaluation.expressions)
        : [],
    [srcEvaluation, dstEvaluation],
  );

  // Memoize thrift-format assignments so EvaluationTable receives stable
  // object references (prevents unnecessary column recalculations).
  const sourceAssignment = useMemo(
    () => toThriftAssignment(assignments.src),
    [assignments.src],
  );
  const srcLabel = assignmentShortLabel(assignments.src);
  const dstLabel = assignmentShortLabel(assignments.dst);
  const maxTupleIndex = useMemo(
    () => Math.max(0, ...rows.map(r => r.tupleIndex ?? 0)),
    [rows],
  );
  const destinationAssignment = useMemo(
    () => toThriftAssignment(assignments.dst),
    [assignments.dst],
  );

  // Typing in the search or filter editor never blocks on re-filtering the
  // list.
  const search = useDeferredValue(listState.search);
  const appliedFilters = useDeferredValue(evalFilters);
  const hasPenalty = useMemo(
    () => hasAnyPenalty(srcEvaluation?.expressions ?? []),
    [srcEvaluation],
  );
  const query: COQuery = useMemo(() => {
    const compiled =
      appliedFilters.length > 0
        ? compileEvaluationFilters(appliedFilters)
        : null;
    return {
      search,
      facets: listState.facets,
      changedOnly: hideAllUnchanged,
      penaltyShown: hasPenalty && listState.penaltyShown,
      matches:
        compiled == null
          ? null
          : row => matchesEvaluationFilters(row, compiled),
    };
  }, [
    search,
    appliedFilters,
    listState.facets,
    listState.penaltyShown,
    hideAllUnchanged,
    hasPenalty,
  ]);
  const counts = useMemo(() => facetCounts(rows, query), [rows, query]);
  const kinds = useMemo(() => {
    const present = new Set(rows.map(r => r.kind));
    return (['objective', 'constraint'] as RowKind[]).filter(k =>
      present.has(k),
    );
  }, [rows]);
  const tuples = useMemo(() => {
    const names = new Map<number, string[]>();
    for (const r of rows) {
      if (r.tupleIndex != null) {
        const list = names.get(r.tupleIndex) ?? [];
        list.push(r.name);
        names.set(r.tupleIndex, list);
      }
    }
    return Array.from(names.entries())
      .sort((a, b) => a[0] - b[0])
      .map(([tupleIndex, n]) => ({tupleIndex, names: n}));
  }, [rows]);
  const pane = useMemo(
    () =>
      handle == null
        ? null
        : {
            handle,
            sourceAssignment,
            destinationAssignment,
            srcLabel,
            dstLabel,
            maxTupleIndex,
          },
    [
      handle,
      sourceAssignment,
      destinationAssignment,
      srcLabel,
      dstLabel,
      maxTupleIndex,
    ],
  );
  const {facets} = listState;
  const extraChips = [
    ...(listState.search !== ''
      ? [
          {
            key: 'q',
            label: `Search: ${listState.search}`,
            onRemove: () => setListState(prev => ({...prev, search: ''})),
          },
        ]
      : []),
    ...facets.kinds.map(k => ({
      key: `kind:${k}`,
      label: `Kind: ${k === 'objective' ? 'Objective' : 'Constraint'}`,
      onRemove: () =>
        setListState(prev => ({
          ...prev,
          facets: {
            ...prev.facets,
            kinds: prev.facets.kinds.filter(x => x !== k),
          },
        })),
    })),
    ...facets.statuses.map(st => ({
      key: `status:${st}`,
      label: `Status: ${STATUS_LABEL[st]}`,
      onRemove: () =>
        setListState(prev => ({
          ...prev,
          facets: {
            ...prev.facets,
            statuses: prev.facets.statuses.filter(x => x !== st),
          },
        })),
    })),
    ...facets.tuples.map(t => ({
      key: `tuple:${t}`,
      label: `Tuple index: ${t}`,
      onRemove: () =>
        setListState(prev => ({
          ...prev,
          facets: {
            ...prev.facets,
            tuples: prev.facets.tuples.filter(x => x !== t),
          },
        })),
    })),
  ];
  const clearAllFilters = useCallback(() => {
    setListState(prev => ({
      ...prev,
      search: '',
      facets: {kinds: [], statuses: [], tuples: []},
    }));
    setEvalFilters([]);
  }, []);

  // The detail pane shows values for the A/B pair it was opened with; close
  // it when either assignment changes.
  const {close: closePane} = useDetailPaneActions();

  // A link with `detailExpr` opens that expression's pane once the first
  // evaluation arrives; an id that is not in the results is ignored.
  const pendingDetail = useRef(
    urlState.detailExpr != null
      ? {expressionId: urlState.detailExpr, tab: urlState.detailTab}
      : null,
  );
  useEffect(() => {
    const pending = pendingDetail.current;
    if (
      pending == null ||
      handle == null ||
      srcEvaluation == null ||
      dstEvaluation == null
    ) {
      return;
    }
    pendingDetail.current = null;
    const src = srcEvaluation.expressions.find(
      e => e.id === pending.expressionId,
    );
    const dst = dstEvaluation.expressions.find(
      e => e.id === pending.expressionId,
    );
    if (src == null || dst == null) {
      return;
    }
    const [row] = joinEvaluationRows([src], [dst]);
    const isConstraint = src.type === ExpressionType.CONSTRAINT;
    openPane({
      kind: 'expression',
      tab: pending.tab,
      payload: {
        ...row,
        expressionType: isConstraint ? 'CONSTRAINT' : 'OBJECTIVE',
        tupleIndex: isConstraint ? null : (src.tupleIndex ?? 0),
        handle,
        sourceAssignment,
        destinationAssignment,
        srcLabel,
        dstLabel,
        maxTupleIndex,
      },
    });
  }, [
    handle,
    srcEvaluation,
    dstEvaluation,
    sourceAssignment,
    destinationAssignment,
    openPane,
    srcLabel,
    dstLabel,
    maxTupleIndex,
  ]);
  const isFirstAssignmentRender = useRef(true);
  useEffect(() => {
    if (isFirstAssignmentRender.current) {
      isFirstAssignmentRender.current = false;
      return;
    }
    closePane();
  }, [sourceAssignment, destinationAssignment, closePane]);

  return (
    <PageContainer>
      <PageHead
        title={'Constraints & Objectives'}
        sub="A is the baseline and B the assignment compared with it. Objectives are minimized in tuple index order, 0 first; constraints must stay at 0."
      />
      <COUrlSync
        assignments={assignments}
        hideAllUnchanged={hideAllUnchanged}
        evalFilters={evalFilters}
        moveSetsState={moveSetsState}
        listState={listState}
      />
      <AssignmentCard
        assignments={assignments}
        onAssignmentsChange={setAssignments}
      />

      {/* Move Sets table */}
      {handle != null && (
        <MoveSetsTable
          assignments={moveSetsQueryAssignments}
          viewState={moveSetsState}
          onViewStateChange={setMoveSetsState}
          enableRowSelection
          onAddMovesToAssignment={handleAddMovesToAssignment}
          pinToInitialFinal={pinToInitialFinal}
          onPinToInitialFinalChange={setPinToInitialFinal}
        />
      )}

      {/* First load only; later updates dim the current results instead. */}
      {loading && srcEvaluation == null && (
        <LoadingState label="Evaluating assignments..." />
      )}

      {/* Error alert */}
      {error != null && <ErrorState>{error}</ErrorState>}

      {/* Constraints & objectives: facets beside one grouped list */}
      {error == null &&
        pane != null &&
        srcEvaluation != null &&
        dstEvaluation != null && (
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
              <COFacets
                search={listState.search}
                onSearchChange={value =>
                  setListState(prev => ({...prev, search: value}))
                }
                changedOnly={hideAllUnchanged}
                onChangedOnlyChange={setHideAllUnchanged}
                total={rows.length}
                facets={listState.facets}
                onFacetsChange={next =>
                  setListState(prev => ({...prev, facets: next}))
                }
                counts={counts}
                kinds={kinds}
                tuples={tuples}
                maxTupleIndex={maxTupleIndex}
              />
            </Box>
            <Box sx={{position: 'relative', minWidth: 0}} aria-busy={loading}>
              {loading && (
                <UpdatingOverlay label="Evaluating assignments..." floating />
              )}
              {/* Rows show the previous A/B until the new results arrive, so
                  they cannot open the pane meanwhile. */}
              <div inert={loading}>
                <COList
                  rows={rows}
                  query={query}
                  group={listState.group}
                  onGroupChange={group =>
                    setListState(prev => ({...prev, group}))
                  }
                  sort={listState.sort}
                  dir={listState.dir}
                  onSortChange={(sort, dir) =>
                    setListState(prev => ({...prev, sort, dir}))
                  }
                  hasPenalty={hasPenalty}
                  onPenaltyShownChange={penaltyShown =>
                    setListState(prev => ({...prev, penaltyShown}))
                  }
                  evalFilters={evalFilters}
                  onEvalFiltersChange={setEvalFilters}
                  extraChips={extraChips}
                  onClearAll={clearAllFilters}
                  pane={pane}
                  focusTupleIndex={urlState.focusTuple}
                />
              </div>
            </Box>
          </Box>
        )}
    </PageContainer>
  );
}

/**
 * Keeps all C&O state, including the open expression pane, in the shareable
 * URL. Only this component re-renders when the pane opens, switches or
 * closes, not the whole view.
 */
function COUrlSync({
  assignments,
  hideAllUnchanged,
  evalFilters,
  moveSetsState,
  listState,
}: {
  assignments: Assignments;
  hideAllUnchanged: boolean;
  evalFilters: Filter[];
  moveSetsState: MoveSetsViewState;
  listState: COListState;
}) {
  const {content} = useDetailPane();
  const expressionId =
    content?.kind === 'expression' ? content.payload.expressionId : null;
  const tab = content?.kind === 'expression' ? content.tab : null;
  useUrlStateSync(() => {
    const detail: CODetailState | null =
      expressionId != null && tab != null ? {expressionId, tab} : null;
    return buildCOSearchParams(
      assignments,
      hideAllUnchanged,
      evalFilters,
      moveSetsState,
      detail,
      listState,
    );
  }, [
    assignments,
    hideAllUnchanged,
    evalFilters,
    moveSetsState,
    expressionId,
    tab,
    listState,
  ]);
  return null;
}
