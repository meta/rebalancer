'use client';

import {useCallback, useEffect, useMemo, useRef, useState} from 'react';

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
import {buildTableDescriptors} from '@/app/components/evaluation/ConstraintsObjectivesView.tables';
import {
  buildCOSearchParams,
  parseCOSearchParams,
  type CODetailState,
} from '@/app/components/evaluation/ConstraintsObjectivesView.url';
import {joinEvaluationRows} from '@/app/components/evaluation/evaluation-rows';
import EvaluationSection from '@/app/components/evaluation/EvaluationSection';
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

  // Hide unchanged — URL-synced state; per-table control lives in EvaluationSection
  const [hideAllUnchanged, setHideAllUnchanged] = useState(
    urlState.hideAllUnchanged,
  );

  // Evaluation filters — URL-synced, passed down to EvaluationSection
  const [evalFilters, setEvalFilters] = useState<Filter[]>(
    urlState.evalFilters,
  );

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

  // Build table descriptors from src expressions
  const tableDescriptors = useMemo(
    () => buildTableDescriptors(srcEvaluation?.expressions ?? []),
    [srcEvaluation],
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
    () => Math.max(0, ...tableDescriptors.map(d => d.tupleIndex)),
    [tableDescriptors],
  );
  const destinationAssignment = useMemo(
    () => toThriftAssignment(assignments.dst),
    [assignments.dst],
  );

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
        sub={
          'Evaluate constraints and objectives w.r.t. different assignments: A is the baseline, B the assignment compared with it.'
        }
      />
      <COUrlSync
        assignments={assignments}
        hideAllUnchanged={hideAllUnchanged}
        evalFilters={evalFilters}
        moveSetsState={moveSetsState}
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

      {/* Constraints & Objectives section */}
      {error == null &&
        handle != null &&
        srcEvaluation != null &&
        dstEvaluation != null && (
          <Box sx={{position: 'relative'}} aria-busy={loading}>
            {loading && (
              <UpdatingOverlay label="Evaluating assignments..." floating />
            )}
            {/* Rows show the previous A/B until the new results arrive, so
                they cannot open the pane meanwhile. */}
            <div inert={loading}>
              <EvaluationSection
                srcEvaluation={srcEvaluation}
                dstEvaluation={dstEvaluation}
                tableDescriptors={tableDescriptors}
                handle={handle}
                sourceAssignment={sourceAssignment}
                destinationAssignment={destinationAssignment}
                hideAllUnchanged={hideAllUnchanged}
                onHideAllUnchangedChange={setHideAllUnchanged}
                evalFilters={evalFilters}
                onEvalFiltersChange={setEvalFilters}
                srcLabel={srcLabel}
                dstLabel={dstLabel}
                focusTupleIndex={urlState.focusTuple}
              />
            </div>
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
}: {
  assignments: Assignments;
  hideAllUnchanged: boolean;
  evalFilters: Filter[];
  moveSetsState: MoveSetsViewState;
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
    );
  }, [
    assignments,
    hideAllUnchanged,
    evalFilters,
    moveSetsState,
    expressionId,
    tab,
  ]);
  return null;
}
