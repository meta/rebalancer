'use client';

import {
  startTransition,
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from 'react';

import {keyframes} from '@emotion/react';

import {
  Box,
  Button,
  Checkbox,
  FormControlLabel,
  Paper,
  ToggleButton,
  ToggleButtonGroup,
  Tooltip,
} from '@mui/material';
import {ChevronRight} from 'lucide-react';

import EntityFilter from '@/app/components/EntityFilter';
import FilterChips from '@/app/components/ui/FilterChips';
import HintIcon from '@/app/components/ui/HintIcon';
import SectionHeader from '@/app/components/ui/SectionHeader';
import {COLOR, radius, TEXT, WEIGHT} from '@/lib/ui-tokens';
import {
  filterExpressions,
  type TableDescriptor,
} from '@/app/components/evaluation/ConstraintsObjectivesView.tables';
import {
  compileEvaluationFilters,
  matchesEvaluationFilters,
} from '@/app/components/evaluation/evaluation-filters';
import {
  hasAnyPenalty,
  isRowChanged,
  PENALTY_HELP_TEXT,
  withAllPenaltiesShown,
  withTablePenaltyShown,
} from '@/app/components/evaluation/evaluation-penalty';
import EvaluationTable from '@/app/components/evaluation/EvaluationTable';
import {
  describeTables,
  joinEvaluationRows,
  rowsHavePenalty,
} from '@/app/components/evaluation/evaluation-rows';
import type {
  Assignment,
  ColumnDescription,
  EvaluationResult,
  Filter,
  Handle,
} from '@/lib/rebalancer-explorer-types';
import {ColumnType} from '@/lib/rebalancer-explorer-types';
const CARD_SX = {overflow: 'hidden'} as const;

// A table opened from a Summary link flashes an accent ring once.
// A table opened from a link flashes the action focus color once.
const focusFlash = keyframes`
  0%, 40% { box-shadow: 0 0 0 3px color-mix(in srgb, var(--color-action) 45%, transparent); }
  100% { box-shadow: 0 0 0 3px transparent; }
`;

const EVAL_COLUMN_DESCRIPTIONS: ColumnDescription[] = [
  {name: 'Name', type: ColumnType.STRING, primaryKey: false, description: ''},
  {name: 'A', type: ColumnType.DOUBLE, primaryKey: false, description: ''},
  {name: 'B', type: ColumnType.DOUBLE, primaryKey: false, description: ''},
  {name: 'B - A', type: ColumnType.DOUBLE, primaryKey: false, description: ''},
  {name: '%', type: ColumnType.DOUBLE, primaryKey: false, description: ''},
];

interface EvaluationSectionProps {
  srcEvaluation: EvaluationResult;
  dstEvaluation: EvaluationResult;
  tableDescriptors: TableDescriptor[];
  handle: Handle;
  sourceAssignment: Assignment;
  destinationAssignment: Assignment;
  hideAllUnchanged: boolean;
  onHideAllUnchangedChange: (v: boolean) => void;
  evalFilters: Filter[];
  onEvalFiltersChange: (filters: Filter[]) => void;
  /** Short names of A and B for the column headers, e.g. "Initial". */
  srcLabel?: string;
  dstLabel?: string;
  /** Tuple index to scroll to and show once (from a Summary link). */
  focusTupleIndex?: number | null;
}

export default function EvaluationSection({
  srcEvaluation,
  dstEvaluation,
  tableDescriptors,
  handle,
  sourceAssignment,
  destinationAssignment,
  hideAllUnchanged,
  onHideAllUnchangedChange,
  evalFilters,
  onEvalFiltersChange,
  srcLabel,
  dstLabel,
  focusTupleIndex = null,
}: EvaluationSectionProps) {
  const maxTupleIndex = useMemo(
    () => Math.max(0, ...tableDescriptors.map(d => d.tupleIndex)),
    [tableDescriptors],
  );
  // The objective table a Summary link asked for, if it exists.
  const focusLabel = useMemo(() => {
    if (focusTupleIndex == null) {
      return null;
    }
    const desc = tableDescriptors.find(
      d => d.type === 'OBJECTIVE' && d.tupleIndex === focusTupleIndex,
    );
    return desc?.label ?? null;
  }, [tableDescriptors, focusTupleIndex]);
  const [uncheckedByTable, setUncheckedByTable] = useState<number | null>(null);
  // Penalty columns are shown by default; this tracks the tables where the
  // user turned them off.
  const [tablesWithPenaltyHidden, setTablesWithPenaltyHidden] = useState<
    ReadonlySet<number>
  >(() => new Set());

  // Debounce filter applied to tables — prevents re-rendering all rows on every keystroke.
  const [appliedFilters, setAppliedFilters] = useState(evalFilters);
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  useEffect(() => {
    if (debounceRef.current != null) clearTimeout(debounceRef.current);
    debounceRef.current = setTimeout(() => {
      startTransition(() => setAppliedFilters(evalFilters));
    }, 300);
    return () => {
      if (debounceRef.current != null) clearTimeout(debounceRef.current);
    };
  }, [evalFilters]);

  const handleHideAllToggle = useCallback(
    (newState: boolean) => {
      onHideAllUnchangedChange(newState);
      if (newState) {
        setUncheckedByTable(null);
      }
    },
    [onHideAllUnchangedChange],
  );

  // Stable per-table objects so React.memo on EvaluationTable can bail out during filter keystrokes.
  const hideAllUnchangedControls = useMemo(
    () =>
      tableDescriptors.map((_, index) => ({
        hideAllUnchanged,
        setHideAllUnchanged: handleHideAllToggle,
        uncheckedByTable,
        setUncheckedByTable,
        tableIndex: index,
      })),
    [
      tableDescriptors,
      hideAllUnchanged,
      handleHideAllToggle,
      uncheckedByTable,
      setUncheckedByTable,
    ],
  );

  // Stable per-table expression slices — avoids new array literals in JSX on every render.
  const splitExpressions = useMemo(
    () =>
      tableDescriptors.map(desc => ({
        src: filterExpressions(
          srcEvaluation.expressions,
          desc.expressionType,
          desc.tupleIndex,
        ),
        dst: filterExpressions(
          dstEvaluation.expressions,
          desc.expressionType,
          desc.tupleIndex,
        ),
      })),
    [tableDescriptors, srcEvaluation, dstEvaluation],
  );

  const hasPenalty = useMemo(
    () => hasAnyPenalty(srcEvaluation.expressions),
    [srcEvaluation],
  );
  const showAllPenalties = tablesWithPenaltyHidden.size === 0;
  const handleShowAllPenaltiesChange = useCallback(
    (showAll: boolean) => {
      setTablesWithPenaltyHidden(
        withAllPenaltiesShown(showAll, tableDescriptors.length),
      );
    },
    [tableDescriptors],
  );

  // Stable per-table callbacks so React.memo on EvaluationTable can bail out.
  const showPenaltyHandlers = useMemo(
    () =>
      tableDescriptors.map(
        (_, index) => (show: boolean) =>
          setTablesWithPenaltyHidden(prev =>
            withTablePenaltyShown(prev, index, show),
          ),
      ),
    [tableDescriptors],
  );

  // Tables with nothing to show collapse into one line per run of adjacent
  // tables: no row matches the filter, or in "Changed" mode every matching
  // row is unchanged, computed exactly as EvaluationTable does.
  const visibleCounts = useMemo(() => {
    const compiled =
      appliedFilters.length > 0
        ? compileEvaluationFilters(appliedFilters)
        : null;
    return splitExpressions.map((split, index) => {
      const rows = joinEvaluationRows(split.src, split.dst);
      const filtered =
        compiled == null
          ? rows
          : rows.filter(row => matchesEvaluationFilters(row, compiled));
      if (!hideAllUnchanged) {
        return filtered.length;
      }
      const penaltyShown =
        rowsHavePenalty(rows) && !tablesWithPenaltyHidden.has(index);
      return filtered.filter(row => isRowChanged(row, penaltyShown)).length;
    });
  }, [
    splitExpressions,
    appliedFilters,
    hideAllUnchanged,
    tablesWithPenaltyHidden,
  ]);

  // Tables the user expanded from a collapsed line; reset when the mode
  // changes.
  const [expandedTables, setExpandedTables] = useState<ReadonlySet<string>>(
    () => new Set(focusLabel != null ? [focusLabel] : []),
  );

  // Scroll to the linked table once it has rendered, and once more after the
  // cards above it have settled.
  useEffect(() => {
    if (focusLabel == null) {
      return;
    }
    const scroll = () =>
      document
        .querySelector(`[data-tuple-table="${focusTupleIndex}"]`)
        ?.scrollIntoView({block: 'start'});
    const frame = requestAnimationFrame(scroll);
    const timer = setTimeout(scroll, 400);
    return () => {
      cancelAnimationFrame(frame);
      clearTimeout(timer);
    };
    // Only on mount: the link asks once.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  const [prevMode, setPrevMode] = useState(hideAllUnchanged);
  if (prevMode !== hideAllUnchanged) {
    setPrevMode(hideAllUnchanged);
    setExpandedTables(new Set());
  }

  const segments = useMemo(() => {
    const result: Array<{collapsed: boolean; indices: number[]}> = [];
    tableDescriptors.forEach((desc, index) => {
      const collapsed =
        visibleCounts[index] === 0 && !expandedTables.has(desc.label);
      const last = result[result.length - 1];
      if (last != null && last.collapsed && collapsed) {
        last.indices.push(index);
      } else {
        result.push({collapsed, indices: [index]});
      }
    });
    return result;
  }, [tableDescriptors, visibleCounts, expandedTables]);

  const nameOptions = useMemo(() => {
    const names = new Set<string>();
    for (const expr of srcEvaluation.expressions) {
      names.add(expr.name);
    }
    for (const expr of dstEvaluation.expressions) {
      names.add(expr.name);
    }
    return {Name: Array.from(names).sort()};
  }, [srcEvaluation, dstEvaluation]);

  return (
    <Box sx={{display: 'flex', flexDirection: 'column', gap: 2}}>
      <Paper variant="outlined">
        <SectionHeader
          title="Constraints & Objectives"
          meta="Grouped by tuple index, highest priority first"
          actions={
            <>
              {hasPenalty && (
                <FormControlLabel
                  sx={{mr: 0}}
                  control={
                    <Checkbox
                      checked={showAllPenalties}
                      onChange={(_e, checked) =>
                        handleShowAllPenaltiesChange(checked)
                      }
                    />
                  }
                  label={
                    <Box
                      component="span"
                      sx={{
                        display: 'inline-flex',
                        alignItems: 'center',
                        gap: 0.5,
                      }}>
                      Show penalties
                      <HintIcon
                        title={PENALTY_HELP_TEXT}
                        label="Help: Show penalties"
                      />
                    </Box>
                  }
                />
              )}
              <Tooltip title="Changed hides rows where the two assignments have the same value, in every table">
                <ToggleButtonGroup
                  size="small"
                  exclusive
                  aria-label="Rows to show"
                  value={hideAllUnchanged ? 'changed' : 'all'}
                  onChange={(_e, value: 'changed' | 'all' | null) => {
                    if (value != null) {
                      handleHideAllToggle(value === 'changed');
                    }
                  }}>
                  <ToggleButton value="changed">Changed</ToggleButton>
                  <ToggleButton value="all">All</ToggleButton>
                </ToggleButtonGroup>
              </Tooltip>
            </>
          }
        />
        <Box
          sx={{
            px: 2,
            pb: 1.5,
            display: 'flex',
            flexDirection: 'column',
            gap: 1,
          }}>
          <EntityFilter
            columnDescriptions={EVAL_COLUMN_DESCRIPTIONS}
            filters={evalFilters}
            onFiltersChange={onEvalFiltersChange}
            entityName="expression"
            localOptions={nameOptions}
          />
          <FilterChips filters={evalFilters} onChange={onEvalFiltersChange} />
        </Box>
      </Paper>
      {segments.map(segment =>
        segment.collapsed ? (
          // A folded group: tables with nothing to show under the current
          // filter, in one line that opens them.
          <Box
            key={`collapsed-${segment.indices[0]}`}
            sx={{
              display: 'flex',
              alignItems: 'center',
              flexWrap: 'wrap',
              gap: 1.25,
              px: 2,
              py: 1,
              border: `1px solid ${COLOR.borderDefault}`,
              borderRadius: radius('lg'),
              bgcolor: COLOR.bgSurface2,
            }}>
            <Box
              component="span"
              aria-hidden
              sx={{display: 'flex', color: COLOR.textDecorative}}>
              <ChevronRight size={16} />
            </Box>
            <Box component="span" sx={{fontWeight: WEIGHT.semibold}}>
              {describeTables(segment.indices.map(i => tableDescriptors[i]))}
            </Box>
            <Box
              component="span"
              sx={{color: COLOR.textTertiary, fontSize: TEXT.xs}}>
              {`no ${hideAllUnchanged ? 'changed ' : ''}rows${
                appliedFilters.length > 0 ? ' match the filter' : ''
              }`}
            </Box>
            <Button
              size="small"
              variant="text"
              sx={{ml: 'auto'}}
              onClick={() =>
                setExpandedTables(prev => {
                  const next = new Set(prev);
                  for (const i of segment.indices) {
                    next.add(tableDescriptors[i].label);
                  }
                  return next;
                })
              }>
              Show
            </Button>
          </Box>
        ) : (
          segment.indices.map(index => {
            const desc = tableDescriptors[index];
            const focused = desc.label === focusLabel;
            return (
              <Paper
                key={desc.label}
                variant="outlined"
                data-tuple-table={
                  desc.type === 'OBJECTIVE' ? desc.tupleIndex : undefined
                }
                sx={
                  focused
                    ? {
                        ...CARD_SX,
                        scrollMarginTop: 16,
                        animation: `${focusFlash} 2.4s ease-out 0.3s both`,
                      }
                    : CARD_SX
                }>
                <EvaluationTable
                  title={desc.label}
                  srcExpressions={splitExpressions[index].src}
                  dstExpressions={splitExpressions[index].dst}
                  expressionType={desc.type}
                  tupleIndex={
                    desc.type === 'OBJECTIVE' ? desc.tupleIndex : null
                  }
                  hideAllUnchangedControl={hideAllUnchangedControls[index]}
                  handle={handle}
                  sourceAssignment={sourceAssignment}
                  destinationAssignment={destinationAssignment}
                  evaluationFilters={appliedFilters}
                  showPenalty={!tablesWithPenaltyHidden.has(index)}
                  onShowPenaltyChange={showPenaltyHandlers[index]}
                  srcLabel={srcLabel}
                  dstLabel={dstLabel}
                  maxTupleIndex={maxTupleIndex}
                  focused={focused}
                />
              </Paper>
            );
          })
        ),
      )}
    </Box>
  );
}
