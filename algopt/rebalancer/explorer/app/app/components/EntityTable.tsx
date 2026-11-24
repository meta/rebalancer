'use client';

import type {Dispatch, SetStateAction} from 'react';
import {useCallback, useMemo} from 'react';

import {Box, Paper, TablePagination, Typography} from '@mui/material';
import {ArrowDown, ArrowUp} from 'lucide-react';
import {
  createColumnHelper,
  flexRender,
  getCoreRowModel,
  useReactTable,
} from '@tanstack/react-table';
import {
  ColumnPinButton,
  getPinnedColumnHelpers,
  useColumnPinning,
} from '@/app/components/columnPinning';
import {
  ColumnHideButton,
  getColumnHiding,
} from '@/app/components/columnVisibility';

import type {CellData, Result} from '@/lib/rebalancer-explorer-types';
import {ColumnType, OrderDirection} from '@/lib/rebalancer-explorer-types';
import {columnTypeInfo} from '@/lib/column-types';
import {formatCount, isNumericColumn} from '@/lib/format';

import type {ViewState} from './EntityView';
import CopyCellAffordance from './CopyCellAffordance';
import UpdatingOverlay from './UpdatingOverlay';
import {EmptyState} from './ui/StateViews';
import useCopyOnClick from './useCopyOnClick';
import TablePaginationActions from './TablePaginationActions';
import EntityLink from './detail-pane/EntityLink';
import EmptyColumnsToggle from './table/EmptyColumnsToggle';
import TableShell from './table/TableShell';
import {useEmptyColumns} from './table/useEmptyColumns';
import {ELLIPSIS_SX} from './table/tableSx';
import Tag from './ui/Tag';

interface EntityTableProps {
  result: Result | null;
  viewState: ViewState;
  onViewStateChange: Dispatch<SetStateAction<ViewState>>;
  loading: boolean;
  /** The entity this table lists; its primary-key cells open the side pane. */
  entityName: string;
  /** Other columns whose values name an entity with its own page. */
  linkEntities?: Record<string, string>;
}

const PAGE_SIZE_OPTIONS = [10, 25, 50, 100];

// Column width sized to the value/type; long header names wrap within it.
function getColumnWidth(type: ColumnType, isPrimaryKey: boolean): number {
  if (isPrimaryKey) {
    return 260;
  }
  return isNumericColumn(type) ? 140 : 180;
}

function formatCellValue(cell: CellData | undefined): string {
  if (cell == null) {
    return '-';
  }
  if (cell.doubleValue != null) {
    const num = Number(cell.doubleValue);
    if (Number.isInteger(num)) {
      return String(num);
    }
    return num.toFixed(4);
  }
  if (cell.stringValue != null) {
    return cell.stringValue;
  }
  return '-';
}

/**
 * Split a header name into multiple lines at `.` boundaries.
 * e.g. "host_count.initUtil" → ["host_count", ".initUtil"]
 */
function splitHeaderName(name: string): string[] {
  const parts = name.split('.');
  if (parts.length <= 1) {
    return [name];
  }
  return parts.map((part, i) => (i === 0 ? part : `.${part}`));
}

type RowData = Record<string, CellData | undefined>;

const columnHelper = createColumnHelper<RowData>();

/**
 * EntityTable renders entity data in a sortable, paginated table with column pinning.
 * All sorting and pagination is server-side via callbacks to the parent.
 */
export default function EntityTable({
  result,
  viewState,
  onViewStateChange,
  loading,
  entityName,
  linkEntities,
}: EntityTableProps) {
  const {copyOnClick, copyOnKeyDown, snackbar} = useCopyOnClick();

  // Use the result's own columns for rendering — these reflect the actual
  // columns returned by the backend (which change when group-by is active,
  // since non-group-by string columns are dropped and IDENTIFIER becomes
  // Row_Count).
  const resultColumns = useMemo(() => result?.columns ?? [], [result]);

  // Determine which columns are visible. When showColumns is non-empty,
  // entity name and group-by columns are always shown alongside the
  // explicitly-selected columns (matching the legacy projection behavior).
  const visibleColumnIndices = useMemo(() => {
    if (viewState.showColumns.length === 0) {
      return resultColumns.map((_, i) => i);
    }
    return resultColumns
      .map((col, i) => ({col, i}))
      .filter(
        ({col}) =>
          col.type === ColumnType.ENTITY_NAME ||
          viewState.showColumns.includes(col.name) ||
          viewState.groupByColumns.includes(col.name),
      )
      .map(({i}) => i);
  }, [resultColumns, viewState.showColumns, viewState.groupByColumns]);

  // Column pinning state — primary key columns pinned by default
  const defaultPinnedIds = useMemo(() => {
    return resultColumns
      .map((col, i) => ({col, i}))
      .filter(({col, i}) => col.primaryKey && visibleColumnIndices.includes(i))
      .map(({i}) => String(i));
  }, [resultColumns, visibleColumnIndices]);

  const {columnPinning, setColumnPinning, togglePin} =
    useColumnPinning(defaultPinnedIds);
  const pinnedIds = columnPinning.left;

  // Columns empty on this page are hidden until the user asks for them,
  // except names, group-by, sort and pinned columns.
  const isProtectedColumn = useCallback(
    (index: number) =>
      resultColumns[index]?.type === ColumnType.ENTITY_NAME ||
      viewState.groupByColumns.includes(resultColumns[index]?.name ?? '') ||
      resultColumns[index]?.name === viewState.orderColumn ||
      (pinnedIds ?? []).includes(String(index)),
    [resultColumns, viewState.groupByColumns, viewState.orderColumn, pinnedIds],
  );
  const {renderedIndices, emptyCount, showEmpty, setShowEmpty} =
    useEmptyColumns(result?.rows, visibleColumnIndices, isProtectedColumn);

  // Build TanStack columns from visible column descriptions
  const columns = useMemo(() => {
    return renderedIndices.map(colIndex => {
      const col = resultColumns[colIndex];
      return columnHelper.accessor(String(colIndex), {
        id: String(colIndex),
        header: col.name,
        cell: info => {
          const value = formatCellValue(info.getValue());
          const linkEntity =
            col.primaryKey && col.type === ColumnType.ENTITY_NAME
              ? entityName
              : linkEntities?.[col.name];
          return linkEntity != null ? (
            <EntityLink entity={linkEntity} id={value} />
          ) : (
            <Box component="span" data-overflow sx={ELLIPSIS_SX}>
              {value}
            </Box>
          );
        },
        size: getColumnWidth(col.type, col.primaryKey),
        minSize: 80,
        meta: {
          numeric: isNumericColumn(col.type),
          primaryKey: col.primaryKey,
          colName: col.name,
          colType: col.type,
        },
      });
    });
  }, [renderedIndices, resultColumns, entityName, linkEntities]);

  // Build row data
  const data = useMemo<RowData[]>(() => {
    if (result == null) {
      return [];
    }
    return result.rows.map(row => {
      const rowObj: RowData = {};
      resultColumns.forEach((_col, colIndex) => {
        rowObj[String(colIndex)] = row.cells[colIndex];
      });
      return rowObj;
    });
  }, [result, resultColumns]);

  const table = useReactTable({
    data,
    columns,
    state: {
      columnPinning,
    },
    onColumnPinningChange: setColumnPinning,
    getCoreRowModel: getCoreRowModel(),
    manualSorting: true,
    manualPagination: true,
    columnResizeMode: 'onChange',
  });

  const handleSort = useCallback(
    (colIndex: string) => {
      const col = resultColumns[parseInt(colIndex, 10)];
      if (col == null) {
        return;
      }
      onViewStateChange(prev => {
        if (prev.orderColumn === col.name) {
          // Toggle direction
          if (prev.orderDirection === OrderDirection.ASCENDING) {
            return {
              ...prev,
              orderDirection: OrderDirection.DESCENDING,
              offset: 0,
            };
          }
          // Clear sort on third click
          return {...prev, orderColumn: null, offset: 0};
        }
        return {
          ...prev,
          orderColumn: col.name,
          orderDirection: OrderDirection.ASCENDING,
          offset: 0,
        };
      });
    },
    [resultColumns, onViewStateChange],
  );

  const currentPage = Math.floor(viewState.offset / viewState.limit);
  const totalCount = result?.totalCount ?? 0;

  const handlePageChange = useCallback(
    (_event: unknown, newPage: number) => {
      onViewStateChange(prev => ({
        ...prev,
        offset: newPage * prev.limit,
      }));
    },
    [onViewStateChange],
  );

  const handleRowsPerPageChange = useCallback(
    (event: React.ChangeEvent<HTMLInputElement>) => {
      const newSize = parseInt(event.target.value, 10);
      onViewStateChange(prev => ({
        ...prev,
        limit: newSize,
        offset: 0,
      }));
    },
    [onViewStateChange],
  );

  if (result == null) {
    return null;
  }

  const pinnedLeft = columnPinning.left ?? [];
  const {stickyStyle, isLastPinned} = getPinnedColumnHelpers(table, pinnedLeft);
  const hiding = getColumnHiding(
    resultColumns,
    viewState.groupByColumns,
    viewState.showColumns,
  );

  const headerGroups = table.getHeaderGroups();
  const rows = table.getRowModel().rows;

  const firstRow = totalCount === 0 ? 0 : viewState.offset + 1;
  const lastRow = Math.min(viewState.offset + viewState.limit, totalCount);

  return (
    <Paper
      variant="outlined"
      sx={{
        display: 'flex',
        flexDirection: 'column',
        position: 'relative',
        overflow: 'hidden',
      }}>
      {/* Keeps the old rows visible while refetching */}
      {loading && <UpdatingOverlay />}

      {/* Row range and a compact pager above the table */}
      <Box
        sx={{
          display: 'flex',
          alignItems: 'center',
          flexWrap: 'wrap',
          columnGap: 1,
          rowGap: 0.5,
          px: 2,
          py: 0.5,
          minHeight: 44,
          borderBottom: 1,
          borderColor: 'divider',
        }}>
        <Typography variant="caption">
          {`${formatCount(firstRow)}–${formatCount(lastRow)} of ${formatCount(totalCount)}`}
        </Typography>
        <EmptyColumnsToggle
          count={emptyCount}
          shown={showEmpty}
          onToggle={() => setShowEmpty(v => !v)}
        />
        <Box sx={{ml: 'auto'}}>
          <TablePaginationActions
            count={totalCount}
            page={currentPage}
            rowsPerPage={viewState.limit}
            onPageChange={handlePageChange}
          />
        </Box>
      </Box>

      {data.length === 0 && !loading ? (
        <EmptyState title="No data found" />
      ) : (
        <TableShell
          fixedLayout
          minWidth={0}
          tableSx={{width: table.getTotalSize(), minWidth: '100%'}}>
          <thead>
            {headerGroups.map(headerGroup => (
              <tr key={headerGroup.id}>
                {headerGroup.headers.map(header => {
                  const meta = header.column.columnDef.meta as
                    | {
                        numeric?: boolean;
                        primaryKey?: boolean;
                        colName?: string;
                        colType?: ColumnType;
                      }
                    | undefined;
                  const isPinned = pinnedLeft.includes(header.column.id);
                  const isSorted = viewState.orderColumn === meta?.colName;
                  const isAsc =
                    isSorted &&
                    viewState.orderDirection === OrderDirection.ASCENDING;
                  const typeLabel =
                    meta?.colType != null ? columnTypeInfo(meta.colType) : null;

                  return (
                    <th
                      key={header.id}
                      className={[
                        meta?.numeric ? 'num' : '',
                        isLastPinned(header.column.id) ? 'last-pinned' : '',
                      ].join(' ')}
                      style={{
                        ...stickyStyle(header.column.id, 3),
                        width: header.getSize(),
                        maxWidth: header.getSize(),
                        minWidth: header.column.columnDef.minSize,
                      }}>
                      <Box
                        sx={{
                          display: 'flex',
                          alignItems: 'center',
                          justifyContent: meta?.numeric
                            ? 'flex-end'
                            : 'flex-start',
                          gap: 0.5,
                          minWidth: 0,
                        }}>
                        <Box
                          component="button"
                          type="button"
                          onClick={() => handleSort(header.column.id)}
                          aria-label={`Sort by ${meta?.colName ?? ''}`}
                          sx={{
                            display: 'flex',
                            alignItems: 'center',
                            gap: 0.5,
                            minWidth: 0,
                            p: 0,
                            border: 0,
                            bgcolor: 'transparent',
                            font: 'inherit',
                            color: meta?.primaryKey
                              ? 'text.primary'
                              : 'inherit',
                            textAlign: meta?.numeric ? 'right' : 'left',
                            cursor: 'pointer',
                          }}>
                          <Box
                            component="span"
                            sx={{
                              display: 'flex',
                              flexDirection: 'column',
                              alignItems: meta?.numeric
                                ? 'flex-end'
                                : 'flex-start',
                              minWidth: 0,
                            }}>
                            {splitHeaderName(
                              String(
                                flexRender(
                                  header.column.columnDef.header,
                                  header.getContext(),
                                ),
                              ),
                            ).map((part, i) => (
                              <Box
                                component="span"
                                key={i}
                                sx={{overflowWrap: 'anywhere'}}>
                                {part}
                              </Box>
                            ))}
                            {typeLabel != null && typeLabel.short !== '' && (
                              <Tag title={typeLabel.full} small sx={{mt: 0.25}}>
                                {typeLabel.short}
                              </Tag>
                            )}
                          </Box>
                          {isSorted &&
                            (isAsc ? (
                              <ArrowUp size={14} className="shrink-0" />
                            ) : (
                              <ArrowDown size={14} className="shrink-0" />
                            ))}
                        </Box>
                        <Box className="col-actions">
                          <ColumnPinButton
                            pinned={isPinned}
                            onToggle={() => togglePin(header.column.id)}
                            label={
                              isPinned
                                ? `Unpin ${meta?.colName}`
                                : `Pin ${meta?.colName}`
                            }
                          />
                          {meta?.colName != null &&
                            hiding.isHideable(meta.colName) && (
                              <ColumnHideButton
                                colName={meta.colName}
                                pinned={isPinned}
                                hideDisabled={hiding.hideDisabled}
                                onHide={() =>
                                  onViewStateChange(prev => ({
                                    ...prev,
                                    showColumns: hiding.hideColumn(
                                      meta.colName!,
                                      prev.showColumns,
                                    ),
                                    offset: 0,
                                  }))
                                }
                              />
                            )}
                        </Box>
                      </Box>
                      <div
                        className={
                          header.column.getIsResizing()
                            ? 'resize-handle active'
                            : 'resize-handle'
                        }
                        onMouseDown={header.getResizeHandler()}
                        onTouchStart={header.getResizeHandler()}
                        onClick={e => e.stopPropagation()}
                      />
                    </th>
                  );
                })}
              </tr>
            ))}
          </thead>
          <tbody>
            {rows.map(row => (
              <tr key={row.id}>
                {row.getVisibleCells().map(cell => {
                  const meta = cell.column.columnDef.meta as
                    {numeric?: boolean; primaryKey?: boolean} | undefined;
                  const isPinned = pinnedLeft.includes(cell.column.id);

                  return (
                    <td
                      key={cell.id}
                      className={[
                        'group copyable',
                        meta?.numeric ? 'num' : '',
                        meta?.primaryKey ? 'primary' : '',
                        isPinned ? 'pinned-cell' : '',
                        isLastPinned(cell.column.id) ? 'last-pinned' : '',
                      ].join(' ')}
                      onClick={copyOnClick}
                      onKeyDown={copyOnKeyDown}
                      tabIndex={0}
                      style={{
                        ...stickyStyle(cell.column.id, 2),
                        width: cell.column.getSize(),
                        maxWidth: cell.column.getSize(),
                      }}>
                      {flexRender(
                        cell.column.columnDef.cell,
                        cell.getContext(),
                      )}
                      <CopyCellAffordance />
                    </td>
                  );
                })}
              </tr>
            ))}
          </tbody>
        </TableShell>
      )}

      {/* Pagination */}
      <TablePagination
        component="div"
        count={totalCount}
        page={currentPage}
        onPageChange={handlePageChange}
        rowsPerPage={viewState.limit}
        onRowsPerPageChange={handleRowsPerPageChange}
        rowsPerPageOptions={PAGE_SIZE_OPTIONS}
        ActionsComponent={TablePaginationActions}
        sx={{
          borderTop: 1,
          borderTopColor: 'divider',
          flexShrink: 0,
          '& .MuiTablePagination-spacer': {display: 'none'},
          '& .MuiTablePagination-toolbar': {justifyContent: 'flex-start'},
        }}
      />

      {snackbar}
    </Paper>
  );
}
