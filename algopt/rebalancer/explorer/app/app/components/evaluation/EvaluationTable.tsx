'use client';

import {memo, useCallback, useMemo, useState} from 'react';

import {
  Box,
  Button,
  Checkbox,
  FormControlLabel,
  IconButton,
  Tooltip,
  Typography,
} from '@mui/material';
import {
  createColumnHelper,
  flexRender,
  getCoreRowModel,
  getSortedRowModel,
  useReactTable,
} from '@tanstack/react-table';
import {
  ArrowDown,
  ArrowUp,
  ChevronRight,
  FileText,
  ListTree,
} from 'lucide-react';
import type {Column, SortingState} from '@tanstack/react-table';

import CollapsibleText from '@/app/components/evaluation/CollapsibleText';
import {
  compileEvaluationFilters,
  matchesEvaluationFilters,
} from '@/app/components/evaluation/evaluation-filters';
import {isRowChanged} from '@/app/components/evaluation/evaluation-penalty';
import {
  calculatePercentageChange,
  formatPercentChange,
  joinEvaluationRows,
  rowsHavePenalty,
  type EvaluationRow,
} from '@/app/components/evaluation/evaluation-rows';
import CopyCellAffordance from '@/app/components/CopyCellAffordance';
import PreciseNumber from '@/app/components/evaluation/PreciseNumber';
import {useDetailPaneActions} from '@/app/components/detail-pane/DetailPaneContext';
import useCopyOnClick from '@/app/components/useCopyOnClick';
import HintIcon from '@/app/components/ui/HintIcon';
import HeaderLabel from '@/app/components/table/HeaderLabel';
import TableShell from '@/app/components/table/TableShell';
import {CLAMP_2_SX} from '@/app/components/table/tableSx';
import {
  ClipStoreContext,
  useClipStore,
} from '@/app/components/evaluation/useClipObserver';
import {ChangeIcon, changeTone} from '@/app/components/ui/Delta';
import {EmptyState} from '@/app/components/ui/StateViews';
import TupleBadge from '@/app/components/ui/TupleBadge';
import {COLOR, DURATION, TEXT, WEIGHT} from '@/lib/ui-tokens';
import type {
  Assignment,
  ExpressionResult,
  Filter,
  Handle,
} from '@/lib/rebalancer-explorer-types';

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export type HideAllUnchangedControl = {
  hideAllUnchanged: boolean;
  setHideAllUnchanged: (v: boolean) => void;
  uncheckedByTable: number | null;
  setUncheckedByTable: (v: number | null) => void;
  tableIndex: number;
};

type EvaluationTableProps = {
  title: string;
  srcExpressions: ExpressionResult[];
  dstExpressions: ExpressionResult[];
  expressionType: 'CONSTRAINT' | 'OBJECTIVE';
  /** Objective tuple index; null for constraints. */
  tupleIndex: number | null;
  hideAllUnchangedControl: HideAllUnchangedControl;
  handle: Handle;
  sourceAssignment: Assignment;
  destinationAssignment: Assignment;
  evaluationFilters?: Filter[];
  showPenalty: boolean;
  onShowPenaltyChange: (show: boolean) => void;
  /** Short names of A and B for the column headers, e.g. "Initial". */
  srcLabel?: string;
  dstLabel?: string;
  /** The run's largest tuple index, for the tuple index color. */
  maxTupleIndex?: number;
  /** Opened from a link to this tuple: shows unchanged rows too. */
  focused?: boolean;
};

// Penalty value, or a muted "-" when the objective has no penalty.
function PenaltyValue({value}: {value: number | null}) {
  if (value == null) {
    return (
      <Box component="span" sx={{color: COLOR.textTertiary}}>
        —
      </Box>
    );
  }
  return <PreciseNumber value={value} highlight={false} />;
}

// Small muted "(%)" shown beneath the B - A delta. Parenthesized to match the
// "(%)" sort label in the header. data-copy-skip keeps it out of the cell's
// click-to-copy text, so copying a B - A cell yields just the value.
function PercentBeneath({
  src,
  dst,
  percentage,
}: {
  src: number;
  dst: number;
  percentage: number;
}) {
  return (
    <Typography
      variant="caption"
      component="span"
      data-copy-skip
      title={src === 0 ? undefined : `${percentage}%`}
      sx={{
        lineHeight: 1,
        fontVariantNumeric: 'tabular-nums',
        color: COLOR.textTertiary,
      }}>
      {`(${formatPercentChange(src, dst, percentage).replace(/-/g, '\u2212')})`}
    </Typography>
  );
}

// B - A for an objective: an arrow beside the colored value, so the direction
// does not depend on color alone. Down (lower) is better: Rebalancer
// minimizes.
/** An objective's change: the direction icon and the signed delta. */
function ObjectiveDelta({value}: {value: number}) {
  if (value === 0 || Number.isNaN(value)) {
    return <PreciseNumber value={value} highlight />;
  }
  return (
    <Box
      component="span"
      sx={{display: 'inline-flex', alignItems: 'center', gap: 0.75}}>
      <ChangeIcon delta={value} tone={changeTone(value)} />
      <PreciseNumber value={value} highlight signed />
    </Box>
  );
}

/** The two comparison sides: baseline grey (A), current blue (B). */
function SideDot({side}: {side: 'A' | 'B'}) {
  return (
    <Box
      component="span"
      aria-hidden
      sx={{
        display: 'inline-block',
        width: 8,
        height: 8,
        mr: 0.75,
        borderRadius: '50%',
        verticalAlign: 'middle',
        bgcolor: side === 'A' ? COLOR.baseline : COLOR.current,
      }}
    />
  );
}

function SortLabel({
  label,
  column,
  secondary = false,
}: {
  label: string;
  column: Column<EvaluationRow> | undefined;
  secondary?: boolean;
}) {
  const sorted = column?.getIsSorted();
  const arrowSize = secondary ? 12 : 14;
  return (
    <Box
      component="button"
      type="button"
      aria-label={`Sort by ${label}`}
      onClick={e => {
        e.stopPropagation();
        column?.toggleSorting();
      }}
      sx={{
        display: 'flex',
        alignItems: 'center',
        gap: 0.25,
        cursor: 'pointer',
        border: 0,
        p: 0,
        bgcolor: 'transparent',
        font: 'inherit',
        color: 'inherit',
        ...(secondary && {typography: 'caption'}),
      }}>
      {label}
      {sorted === 'asc' && <ArrowUp size={arrowSize} />}
      {sorted === 'desc' && <ArrowDown size={arrowSize} />}
    </Box>
  );
}

// Fixed width for the A / B / B-A number columns, so they line up and stay
// compact; longer values wrap. `ch` is the width of one digit (tabular-nums
// keeps digits equal), plus `3rem` padding.
const NUMBER_COL_WIDTH = 'calc(17ch + 3rem)';
// With the three penalty columns shown, all number columns narrow and
// Description gets a fixed width, so the table scrolls horizontally on a narrow
// screen instead of squeezing Description to nothing.
const NUMBER_COL_WITH_PENALTY_WIDTH = 'calc(12ch + 3rem)';
const DESCRIPTION_WITH_PENALTY_WIDTH = 200;

// Name sizes to content (clamped) so short names free width for the auto-width
// Description column.
const MIN_NAME_CH = 10;
const MAX_NAME_CH = 44;

// Stacks the B - A value over its smaller "(%)" line, right-aligned. Used in the
// header, the body cell, and the totals footer so all three line up.
const STACKED_BA_SX = {
  display: 'flex',
  flexDirection: 'column',
  alignItems: 'flex-end',
  gap: '2px',
} as const;

// ---------------------------------------------------------------------------
// Column definitions
// ---------------------------------------------------------------------------

const columnHelper = createColumnHelper<EvaluationRow>();

// ---------------------------------------------------------------------------
// Main component
// ---------------------------------------------------------------------------

const EvaluationTable = memo(function EvaluationTable({
  title,
  srcExpressions,
  dstExpressions,
  expressionType,
  tupleIndex,
  hideAllUnchangedControl,
  handle,
  sourceAssignment,
  destinationAssignment,
  evaluationFilters,
  showPenalty,
  onShowPenaltyChange,
  srcLabel,
  dstLabel,
  maxTupleIndex,
  focused = false,
}: EvaluationTableProps) {
  const {
    hideAllUnchanged,
    setHideAllUnchanged,
    uncheckedByTable,
    setUncheckedByTable,
    tableIndex,
  } = hideAllUnchangedControl;

  // A table opened from a link to its tuple shows all of its rows, as if
  // "Show unchanged" had been pressed.
  const [hideUnchanged, setHideUnchanged] = useState(
    hideAllUnchanged && !focused,
  );
  // The group head folds the table away.
  const [folded, setFolded] = useState(false);
  const {copyOnClick, copyOnKeyDown, snackbar} = useCopyOnClick();
  const {open: openPane} = useDetailPaneActions();

  const handleTableHideUnchangedChange = useCallback(
    (newState: boolean) => {
      setHideUnchanged(newState);
      if (!newState) {
        setHideAllUnchanged(false);
        setUncheckedByTable(tableIndex);
      }
    },
    [setHideAllUnchanged, setUncheckedByTable, tableIndex],
  );

  // Sync local state with global "hide all" control whenever the parent
  // values change:
  // 1. hideAll checked → check this table
  // 2. hideAll unchecked directly (uncheckedByTable === null) → uncheck
  // 3. hideAll unchecked by THIS table → uncheck
  // 4. hideAll unchecked by ANOTHER table → leave unchanged
  const [prevHideAllUnchanged, setPrevHideAllUnchanged] =
    useState(hideAllUnchanged);
  const [prevUncheckedByTable, setPrevUncheckedByTable] =
    useState(uncheckedByTable);
  if (
    hideAllUnchanged !== prevHideAllUnchanged ||
    uncheckedByTable !== prevUncheckedByTable
  ) {
    setPrevHideAllUnchanged(hideAllUnchanged);
    setPrevUncheckedByTable(uncheckedByTable);
    if (hideAllUnchanged) {
      setHideUnchanged(true);
    } else if (uncheckedByTable === null || uncheckedByTable === tableIndex) {
      setHideUnchanged(false);
    }
  }

  const isConstraint = expressionType === 'CONSTRAINT';

  // Join src and dst by expression id
  const allRows = useMemo(
    () => joinEvaluationRows(srcExpressions, dstExpressions),
    [srcExpressions, dstExpressions],
  );

  const hasPenalty = useMemo(() => rowsHavePenalty(allRows), [allRows]);
  const isPenaltyShown = hasPenalty && showPenalty;

  // Rows matching the user-selected evaluation filters.
  const filteredRows = useMemo(() => {
    if (evaluationFilters == null || evaluationFilters.length === 0) {
      return allRows;
    }
    const compiled = compileEvaluationFilters(evaluationFilters);
    return allRows.filter(row => matchesEvaluationFilters(row, compiled));
  }, [allRows, evaluationFilters]);

  // Rows actually rendered in the table body: the "hide unchanged" toggle
  // layered on top of the filtered rows.
  const displayRows = useMemo(
    () =>
      hideUnchanged
        ? filteredRows.filter(row => isRowChanged(row, isPenaltyShown))
        : filteredRows,
    [filteredRows, hideUnchanged, isPenaltyShown],
  );

  const changedCount = useMemo(
    () => filteredRows.filter(row => isRowChanged(row, isPenaltyShown)).length,
    [filteredRows, isPenaltyShown],
  );
  const unchangedCount = filteredRows.length - changedCount;

  // Based on allRows, not the visible rows, so the width holds steady as
  // filters and "hide unchanged" change what's shown.
  const nameColWidth = useMemo(() => {
    const longest = allRows.reduce(
      (max, row) => Math.max(max, row.name.length),
      0,
    );
    const ch = Math.min(Math.max(longest, MIN_NAME_CH), MAX_NAME_CH);
    return `calc(${ch}ch + 3rem)`;
  }, [allRows]);

  // Footer totals sum exactly the rows rendered in the table body, so they
  // always match what is on screen — both the evaluation filter and the
  // "hide unchanged" toggle are reflected.
  const totals = useMemo(() => {
    let srcTotal = 0;
    let dstTotal = 0;
    let srcPenaltyTotal = 0;
    let dstPenaltyTotal = 0;
    for (const row of displayRows) {
      srcTotal += row.srcValue;
      dstTotal += row.dstValue;
      srcPenaltyTotal += row.srcPenalty ?? 0;
      dstPenaltyTotal += row.dstPenalty ?? 0;
    }
    const deltaTotal = dstTotal - srcTotal;
    const percentTotal = calculatePercentageChange(srcTotal, dstTotal);
    return {
      srcTotal,
      dstTotal,
      deltaTotal,
      percentTotal,
      srcPenaltyTotal,
      dstPenaltyTotal,
      penaltyDeltaTotal: dstPenaltyTotal - srcPenaltyTotal,
    };
  }, [displayRows]);

  // Sorting state
  const [sorting, setSorting] = useState<SortingState>([]);

  const openRowPane = useCallback(
    (row: EvaluationRow, tab: 'spec' | 'tree') =>
      openPane({
        kind: 'expression',
        tab,
        payload: {
          ...row,
          expressionType,
          tupleIndex,
          handle,
          sourceAssignment,
          destinationAssignment,
          srcLabel,
          dstLabel,
          maxTupleIndex,
        },
      }),
    [
      openPane,
      expressionType,
      tupleIndex,
      handle,
      sourceAssignment,
      destinationAssignment,
      srcLabel,
      dstLabel,
      maxTupleIndex,
    ],
  );

  // Column definitions
  const columns = useMemo(
    () => [
      columnHelper.accessor('name', {
        header: 'Name',
        cell: info => (
          <Box
            component="button"
            type="button"
            data-overflow
            data-pane-open="spec"
            onClick={() => openRowPane(info.row.original, 'spec')}
            sx={{
              ...CLAMP_2_SX,
              maxWidth: '100%',
              p: 0,
              border: 0,
              bgcolor: 'transparent',
              font: 'inherit',
              color: COLOR.textPrimary,
              fontWeight: WEIGHT.semibold,
              textAlign: 'left',
              cursor: 'pointer',
              '&:hover': {color: COLOR.action, textDecoration: 'underline'},
            }}>
            {info.getValue()}
          </Box>
        ),
        enableSorting: false,
        meta: {
          tooltip:
            'Unique name of a goal or constraint. It can be customized with the parameter "name" when constructing a spec.',
        },
      }),
      columnHelper.accessor('description', {
        header: 'Description',
        cell: info => <CollapsibleText text={info.getValue()} clampLines={2} />,
        enableSorting: false,
        size: 280,
        meta: {
          flex: true,
          tooltip: 'Auto-generated description of the goal or constraint.',
        },
      }),
      columnHelper.accessor('srcValue', {
        header: () => (
          <span>
            <SideDot side="A" />
            {srcLabel != null ? `A · ${srcLabel}` : 'A'}
          </span>
        ),
        cell: info => (
          <PreciseNumber value={info.getValue()} highlight={isConstraint} />
        ),
        meta: {
          numeric: true,
          headerLabel: 'A',
          tooltip:
            'Result of evaluating the expression with assignment A. Click a number to switch between formatted and raw.',
        },
      }),
      columnHelper.accessor('dstValue', {
        header: () => (
          <span>
            <SideDot side="B" />
            {dstLabel != null ? `B · ${dstLabel}` : 'B'}
          </span>
        ),
        cell: info => (
          <PreciseNumber value={info.getValue()} highlight={isConstraint} />
        ),
        meta: {
          numeric: true,
          headerLabel: 'B',
          tooltip:
            'Result of evaluating the expression with assignment B. Click a number to switch between formatted and raw.',
        },
      }),
      columnHelper.accessor('delta', {
        // Two independent sort targets stacked in one header: "B - A" sorts by
        // the delta, "(%)" sorts by the hidden percentage column.
        header: ({table: t}) => (
          <Box sx={STACKED_BA_SX}>
            <SortLabel label="B − A" column={t.getColumn('delta')} />
            <SortLabel
              label="(%)"
              column={t.getColumn('percentage')}
              secondary
            />
          </Box>
        ),
        cell: info => {
          const {delta, percentage, srcValue, dstValue} = info.row.original;
          return (
            <Box sx={STACKED_BA_SX}>
              {isConstraint ? (
                <PreciseNumber value={delta} highlight={false} />
              ) : (
                <ObjectiveDelta value={delta} />
              )}
              <PercentBeneath
                src={srcValue}
                dst={dstValue}
                percentage={percentage}
              />
            </Box>
          );
        },
        meta: {
          numeric: true,
          dualSort: true,
          headerLabel: 'B − A',
          tooltip:
            'B - A is the difference between the two assignments, with the percentage change beneath. Click "B - A" or "(%)" in the header to sort by either, and a number to switch between formatted and raw.',
        },
      }),
      // Hidden (see columnVisibility): exists only as the sort target for the
      // "(%)" control in the B - A header. Its value is shown by PercentBeneath.
      columnHelper.accessor('percentage', {header: '%'}),
      // Penalty columns sort a missing penalty as 0; hidden unless the table
      // has penalties and "Show penalty" is checked (see columnVisibility).
      columnHelper.accessor(row => row.srcPenalty ?? 0, {
        id: 'srcPenalty',
        header: 'Penalty A',
        cell: info => <PenaltyValue value={info.row.original.srcPenalty} />,
        meta: {
          numeric: true,
          tooltip:
            'Separated local-search penalty with assignment A. Local search compares it only after the objective value, so it can only break ties between moves that leave the objective unchanged.',
        },
      }),
      columnHelper.accessor(row => row.dstPenalty ?? 0, {
        id: 'dstPenalty',
        header: 'Penalty B',
        cell: info => <PenaltyValue value={info.row.original.dstPenalty} />,
        meta: {
          numeric: true,
          tooltip: 'Separated local-search penalty with assignment B.',
        },
      }),
      columnHelper.accessor('penaltyDelta', {
        header: 'Penalty B - A',
        cell: info => {
          const {srcPenalty, dstPenalty, penaltyDelta} = info.row.original;
          return srcPenalty == null && dstPenalty == null ? (
            <PenaltyValue value={null} />
          ) : (
            <PreciseNumber value={penaltyDelta} highlight signed />
          );
        },
        meta: {
          numeric: true,
          tooltip: 'Difference of the separated local-search penalty.',
        },
      }),
      columnHelper.display({
        id: 'actions',
        header: ' ',
        cell: info => {
          const row = info.row.original;
          return (
            <Box sx={{display: 'flex', gap: 0.5}}>
              <IconButton
                size="small"
                aria-label="View spec"
                title="View spec"
                onClick={() => openRowPane(row, 'spec')}>
                <FileText size={16} />
              </IconButton>
              <IconButton
                size="small"
                aria-label="View tree"
                title="View tree"
                data-pane-open="tree"
                onClick={() => openRowPane(row, 'tree')}>
                <ListTree size={16} />
              </IconButton>
            </Box>
          );
        },
        size: 90,
        enableSorting: false,
      }),
    ],
    [isConstraint, openRowPane, srcLabel, dstLabel],
  );

  const columnVisibility = useMemo(
    () => ({
      // The percentage column is merged into the B - A cell; keep it in the
      // model (so it stays sortable) but hidden so it renders no column of its
      // own.
      percentage: false,
      srcPenalty: isPenaltyShown,
      dstPenalty: isPenaltyShown,
      penaltyDelta: isPenaltyShown,
    }),
    [isPenaltyShown],
  );

  const table = useReactTable({
    data: displayRows,
    columns,
    state: {sorting, columnVisibility},
    onSortingChange: setSorting,
    getCoreRowModel: getCoreRowModel(),
    getSortedRowModel: getSortedRowModel(),
  });

  const headerGroups = table.getHeaderGroups();
  const rows = table.getRowModel().rows;
  const clipStore = useClipStore();

  return (
    <Box>
      {/* Group head: fold, rank pill, title, counts, per-table penalty and
          unchanged toggles. */}
      <Box
        sx={{
          display: 'flex',
          alignItems: 'center',
          flexWrap: 'wrap',
          columnGap: 1.25,
          rowGap: 0.5,
          px: 2,
          py: 1.25,
          bgcolor: COLOR.bgSurface2,
          borderBottom: folded ? 0 : `1px solid ${COLOR.borderDefault}`,
        }}>
        <Box
          sx={{
            display: 'inline-flex',
            alignItems: 'center',
            gap: 1,
            minWidth: 0,
          }}>
          <Tooltip title={folded ? 'Show this table' : 'Fold this table'}>
            <IconButton
              size="small"
              aria-expanded={!folded}
              aria-label={`${folded ? 'Show' : 'Fold'} ${title}`}
              onClick={() => setFolded(f => !f)}
              sx={{
                width: 24,
                height: 24,
                ml: -0.75,
                color: COLOR.textDecorative,
                '& svg': {
                  transform: folded ? 'none' : 'rotate(90deg)',
                  transition: `transform ${DURATION.base}ms`,
                },
                '&:hover': {color: COLOR.textSecondary},
              }}>
              <ChevronRight size={16} />
            </IconButton>
          </Tooltip>
          {tupleIndex != null && maxTupleIndex != null && maxTupleIndex > 0 && (
            <TupleBadge tupleIndex={tupleIndex} maxTupleIndex={maxTupleIndex} />
          )}
          <Typography
            component="h3"
            sx={{fontSize: TEXT.sm, fontWeight: WEIGHT.semibold}}>
            {title}
          </Typography>
        </Box>
        <Typography variant="caption">
          {`${changedCount} changed · ${filteredRows.length} total`}
        </Typography>
        <Box sx={{ml: 'auto', display: 'flex', alignItems: 'center', gap: 1}}>
          {hasPenalty && (
            <Tooltip title="Show the penalty of each objective in this table">
              <FormControlLabel
                sx={{mr: 0}}
                control={
                  <Checkbox
                    checked={showPenalty}
                    onChange={(_e, checked) => onShowPenaltyChange(checked)}
                  />
                }
                label="Penalty"
              />
            </Tooltip>
          )}
          {(!hideUnchanged || unchangedCount > 0) && (
            <Tooltip
              title={
                isPenaltyShown
                  ? 'Rows where the two assignments have the same value and the same penalty'
                  : 'Rows where the two assignments have the same value'
              }>
              <Button
                size="small"
                variant="text"
                onClick={() => handleTableHideUnchangedChange(!hideUnchanged)}>
                {hideUnchanged
                  ? `Show unchanged (${unchangedCount})`
                  : 'Hide unchanged'}
              </Button>
            </Tooltip>
          )}
        </Box>
      </Box>

      {/* Table */}
      {!folded && (
        <ClipStoreContext.Provider value={clipStore}>
          <TableShell
            fixedLayout
            minWidth={isPenaltyShown ? 1240 : 960}
            label={title}>
            <thead>
              {headerGroups.map(headerGroup => (
                <tr key={headerGroup.id}>
                  {headerGroup.headers.map(header => {
                    const meta = header.column.columnDef.meta as
                      | {
                          numeric?: boolean;
                          flex?: boolean;
                          tooltip?: string;
                          dualSort?: boolean;
                          headerLabel?: string;
                        }
                      | undefined;
                    // The dual-sort column (B - A / %) handles its own clicks and
                    // arrows inside its header, so skip the generic ones here.
                    const isDualSort = meta?.dualSort === true;
                    const canSort = header.column.getCanSort() && !isDualSort;
                    const sorted = header.column.getIsSorted();
                    const headerLabel =
                      meta?.headerLabel ??
                      (typeof header.column.columnDef.header === 'string'
                        ? header.column.columnDef.header
                        : header.column.id);

                    return (
                      <th
                        key={header.id}
                        className={[
                          meta?.numeric ? 'num' : '',
                          canSort ? 'sortable' : '',
                        ].join(' ')}
                        style={{
                          width: meta?.flex
                            ? isPenaltyShown
                              ? DESCRIPTION_WITH_PENALTY_WIDTH
                              : 'auto'
                            : meta?.numeric
                              ? isPenaltyShown
                                ? NUMBER_COL_WITH_PENALTY_WIDTH
                                : NUMBER_COL_WIDTH
                              : header.column.id === 'name'
                                ? nameColWidth
                                : header.getSize(),
                        }}
                        onClick={
                          canSort
                            ? header.column.getToggleSortingHandler()
                            : undefined
                        }>
                        <HeaderLabel
                          label={flexRender(
                            header.column.columnDef.header,
                            header.getContext(),
                          )}
                          hint={meta?.tooltip}
                          hintLabel={headerLabel}
                          sorted={isDualSort ? false : sorted}
                          align={meta?.numeric ? 'right' : 'left'}
                        />
                      </th>
                    );
                  })}
                </tr>
              ))}
            </thead>
            <tbody>
              {rows.map(row => (
                <tr key={row.id} data-pane-row={row.original.expressionId}>
                  {row.getVisibleCells().map(cell => {
                    const meta = cell.column.columnDef.meta as
                      {numeric?: boolean} | undefined;
                    const copyable =
                      cell.column.id !== 'actions' && cell.column.id !== 'name';
                    return (
                      <td
                        key={cell.id}
                        className={[
                          copyable ? 'group copyable' : '',
                          meta?.numeric ? 'num' : '',
                        ].join(' ')}
                        onClick={copyable ? copyOnClick : undefined}
                        onKeyDown={copyable ? copyOnKeyDown : undefined}
                        tabIndex={copyable ? 0 : undefined}>
                        {flexRender(
                          cell.column.columnDef.cell,
                          cell.getContext(),
                        )}
                        {copyable && <CopyCellAffordance />}
                      </td>
                    );
                  })}
                </tr>
              ))}
              {rows.length === 0 && (
                <tr>
                  <td
                    className="empty"
                    colSpan={table.getVisibleLeafColumns().length}>
                    <EmptyState
                      compact
                      title={
                        hideUnchanged
                          ? 'All rows are unchanged'
                          : 'No evaluation results'
                      }
                    />
                  </td>
                </tr>
              )}
            </tbody>
            {rows.length > 0 && (
              <tfoot>
                <tr>
                  {/* Name */}
                  <td>
                    <Box
                      sx={{
                        display: 'inline-flex',
                        alignItems: 'center',
                        gap: 0.5,
                      }}>
                      <Typography variant="subtitle2" component="span">
                        Total
                      </Typography>
                      <HintIcon
                        title="Sum of the rows shown above. % is recomputed from the A and B totals."
                        label="Help: Total"
                      />
                    </Box>
                  </td>
                  {/* Description */}
                  <td />
                  {/* A */}
                  <td className="num">
                    <PreciseNumber
                      value={totals.srcTotal}
                      highlight={isConstraint}
                    />
                  </td>
                  {/* B */}
                  <td className="num">
                    <PreciseNumber
                      value={totals.dstTotal}
                      highlight={isConstraint}
                    />
                  </td>
                  {/* B - A, with % beneath */}
                  <td className="num">
                    <Box sx={STACKED_BA_SX}>
                      {isConstraint ? (
                        <PreciseNumber
                          value={totals.deltaTotal}
                          highlight={false}
                        />
                      ) : (
                        <ObjectiveDelta value={totals.deltaTotal} />
                      )}
                      <PercentBeneath
                        src={totals.srcTotal}
                        dst={totals.dstTotal}
                        percentage={totals.percentTotal}
                      />
                    </Box>
                  </td>
                  {isPenaltyShown && (
                    <>
                      {/* Penalty A */}
                      <td className="num">
                        <PreciseNumber
                          value={totals.srcPenaltyTotal}
                          highlight={false}
                        />
                      </td>
                      {/* Penalty B */}
                      <td className="num">
                        <PreciseNumber
                          value={totals.dstPenaltyTotal}
                          highlight={false}
                        />
                      </td>
                      {/* Penalty B - A */}
                      <td className="num">
                        <PreciseNumber
                          value={totals.penaltyDeltaTotal}
                          highlight
                          signed
                        />
                      </td>
                    </>
                  )}
                  {/* Actions */}
                  <td />
                </tr>
              </tfoot>
            )}
          </TableShell>
        </ClipStoreContext.Provider>
      )}
      {snackbar}
    </Box>
  );
});

export default EvaluationTable;
