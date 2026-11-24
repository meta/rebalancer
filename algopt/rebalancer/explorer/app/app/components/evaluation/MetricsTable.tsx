'use client';

import type {Dispatch, SetStateAction} from 'react';
import {useCallback, useMemo} from 'react';

import {
  Autocomplete,
  Box,
  IconButton,
  Paper,
  TablePagination,
  TextField,
  Tooltip,
  Typography,
} from '@mui/material';
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

import {metricsFaqUrl} from '@platform/internal-links';

import {CircleHelp} from 'lucide-react';

import EntityLink from '@/app/components/detail-pane/EntityLink';
import EntityFilter from '@/app/components/EntityFilter';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import HeaderLabel from '@/app/components/table/HeaderLabel';
import EmptyColumnsToggle from '@/app/components/table/EmptyColumnsToggle';
import TableShell from '@/app/components/table/TableShell';
import {useEmptyColumns} from '@/app/components/table/useEmptyColumns';
import {ELLIPSIS_SX} from '@/app/components/table/tableSx';
import TablePaginationActions from '@/app/components/TablePaginationActions';
import FilterChips from '@/app/components/ui/FilterChips';
import {COLOR, TEXT} from '@/lib/ui-tokens';
import Pill from '@/app/components/ui/Pill';
import GroupBySelector from '@/app/components/GroupBySelector';
import ShowColumnsSelector from '@/app/components/ShowColumnsSelector';
import useCopyOnClick from '@/app/components/useCopyOnClick';
import type {Assignments} from '@/app/components/evaluation/AssignmentCard';
import {
  B_MINUS_A_SUFFIX,
  getColumnWidth,
  isVisibleColumn,
  type MetricsViewState,
} from '@/app/components/evaluation/MetricsTable.state';
import {
  extractCellValue,
  formatCellValue,
  NAN_STRING,
} from '@/app/components/evaluation/MoveSetsTable.cells';
import CopyCellAffordance from '@/app/components/CopyCellAffordance';
import UpdatingOverlay from '@/app/components/UpdatingOverlay';
import {
  EmptyState,
  ErrorState,
  LoadingState,
} from '@/app/components/ui/StateViews';
import PreciseNumber from '@/app/components/evaluation/PreciseNumber';
import {useMetricsData} from '@/app/components/evaluation/useMetricsData';
import {
  AUTOCOMPLETE_WORD_BREAK_PROPS,
  formatCount,
  isNumericColumn,
} from '@/lib/format';
import type {CellData, Filter} from '@/lib/rebalancer-explorer-types';
import {ColumnType, OrderDirection} from '@/lib/rebalancer-explorer-types';

const PAGE_SIZE_OPTIONS = [10, 25, 50, 100];

type RowData = Record<string, CellData | undefined>;

const columnHelper = createColumnHelper<RowData>();

interface MetricsTableProps {
  metricName: string;
  viewState: MetricsViewState;
  onViewStateChange: Dispatch<SetStateAction<MetricsViewState>>;
  assignments: Assignments;
  metricCollectionNames: string[];
  onMetricNameChange: (
    event: React.SyntheticEvent,
    value: string | null,
  ) => void;
}

export default function MetricsTable({
  metricName,
  viewState,
  onViewStateChange,
  assignments,
  metricCollectionNames,
  onMetricNameChange,
}: MetricsTableProps) {
  const {copyOnClick, copyOnKeyDown, snackbar} = useCopyOnClick();

  const {offset, pageSize, orderBy, filters, groupBy, showColumns} = viewState;

  const {
    result,
    originalResult,
    loading,
    error,
    setError,
    isValid,
    columnTypeaheadEntities,
  } = useMetricsData({metricName, viewState, assignments});

  // ---- Columns ----
  const allColumns = useMemo(() => result?.columns ?? [], [result]);

  const visibleColumnIndices = useMemo(
    () =>
      allColumns
        .map((col, i) => ({col, i}))
        .filter(({col}) => isVisibleColumn(col, groupBy, showColumns))
        .map(({i}) => i),
    [allColumns, groupBy, showColumns],
  );

  // Object, container and scope values open their row in the side pane;
  // partitions have no entity page and stay plain text.
  const {metadata} = useProblemMetadata();
  const linkEntities = useMemo(
    () =>
      Object.fromEntries(
        Object.entries(columnTypeaheadEntities ?? {}).filter(
          ([, entity]) => !(metadata?.partitionNames ?? []).includes(entity),
        ),
      ),
    [columnTypeaheadEntities, metadata],
  );

  // The identifier columns start pinned to the left.
  const defaultPinnedIds = useMemo(
    () =>
      visibleColumnIndices
        .filter(i => allColumns[i]?.primaryKey)
        .map(i => String(i)),
    [visibleColumnIndices, allColumns],
  );
  const {columnPinning, setColumnPinning, togglePin} =
    useColumnPinning(defaultPinnedIds);
  const pinnedIds = columnPinning.left;

  // Columns empty on this page (e.g. all `N/A`) are hidden until asked for,
  // except names, group-by, sort and pinned columns.
  const isProtectedColumn = useCallback(
    (index: number) =>
      allColumns[index]?.type === ColumnType.ENTITY_NAME ||
      groupBy.includes(allColumns[index]?.name ?? '') ||
      allColumns[index]?.name === orderBy?.column ||
      (pinnedIds ?? []).includes(String(index)),
    [allColumns, groupBy, orderBy?.column, pinnedIds],
  );
  const {renderedIndices, emptyCount, showEmpty, setShowEmpty} =
    useEmptyColumns(result?.rows, visibleColumnIndices, isProtectedColumn);

  const columns = useMemo(() => {
    return renderedIndices.map(colIndex => {
      const col = allColumns[colIndex];
      const isBMinusA = col.name.endsWith(B_MINUS_A_SUFFIX);
      const isNumeric = isNumericColumn(col.type);
      return columnHelper.accessor(String(colIndex), {
        id: String(colIndex),
        header: col.name,
        cell: info => {
          const value = extractCellValue(info.getValue(), col);
          if (value === NAN_STRING) {
            return <Pill tone="bad">NaN</Pill>;
          }
          if (isBMinusA && typeof value === 'number') {
            return (
              <PreciseNumber
                value={value}
                highlight={true}
                positiveIsBad={true}
                signed
                fixedDecimals={Number.isInteger(value) ? 0 : 4}
              />
            );
          }
          const linkEntity = linkEntities[col.name];
          if (linkEntity != null && typeof value === 'string') {
            return <EntityLink entity={linkEntity} id={value} />;
          }
          return (
            <Box component="span" data-overflow sx={ELLIPSIS_SX}>
              {formatCellValue(value)}
            </Box>
          );
        },
        size: getColumnWidth(col),
        meta: {
          numeric: isNumeric,
          colName: col.name,
          primaryKey: col.primaryKey,
        },
      });
    });
  }, [renderedIndices, allColumns, linkEntities]);

  // ---- Rows ----
  const data = useMemo<RowData[]>(() => {
    if (result == null) return [];
    return result.rows.map(row => {
      const rowObj: RowData = {};
      allColumns.forEach((_col, colIndex) => {
        rowObj[String(colIndex)] = row.cells[colIndex];
      });
      return rowObj;
    });
  }, [result, allColumns]);

  const table = useReactTable({
    data,
    columns,
    state: {columnPinning},
    onColumnPinningChange: setColumnPinning,
    getCoreRowModel: getCoreRowModel(),
    manualSorting: true,
    manualPagination: true,
    columnResizeMode: 'onChange',
  });

  // ---- Handlers ----
  const handleSort = useCallback(
    (colIndex: string) => {
      const col = allColumns[parseInt(colIndex, 10)];
      if (col == null) return;
      let nextOrderBy: MetricsViewState['orderBy'];
      if (orderBy?.column === col.name) {
        nextOrderBy =
          orderBy.direction === OrderDirection.ASCENDING
            ? {column: col.name, direction: OrderDirection.DESCENDING}
            : null;
      } else {
        nextOrderBy = {column: col.name, direction: OrderDirection.ASCENDING};
      }
      onViewStateChange({...viewState, orderBy: nextOrderBy, offset: 0});
    },
    [allColumns, orderBy, viewState, onViewStateChange],
  );

  const currentPage = pageSize > 0 ? Math.floor(offset / pageSize) : 0;

  const handlePageChange = useCallback(
    (_event: unknown, newPage: number) => {
      onViewStateChange({...viewState, offset: newPage * pageSize});
    },
    [viewState, pageSize, onViewStateChange],
  );

  const handleRowsPerPageChange = useCallback(
    (event: React.ChangeEvent<HTMLInputElement>) => {
      const newSize = parseInt(event.target.value, 10);
      onViewStateChange({...viewState, pageSize: newSize, offset: 0});
    },
    [viewState, onViewStateChange],
  );

  const handleFiltersChange = useCallback(
    (newFilters: Filter[]) => {
      onViewStateChange({...viewState, filters: newFilters, offset: 0});
    },
    [viewState, onViewStateChange],
  );

  const handleGroupByChange = useCallback(
    (newGroupBy: string[]) => {
      onViewStateChange({
        ...viewState,
        groupBy: newGroupBy,
        offset: 0,
        orderBy:
          newGroupBy.length > 0
            ? {column: newGroupBy[0], direction: OrderDirection.ASCENDING}
            : null,
      });
    },
    [viewState, onViewStateChange],
  );

  const handleShowColumnsChange = useCallback(
    (newShowColumns: string[]) => {
      onViewStateChange({...viewState, showColumns: newShowColumns, offset: 0});
    },
    [viewState, onViewStateChange],
  );

  // ---- Render ----
  if (!isValid) {
    return null;
  }

  const totalCount = result?.totalCount ?? 0;
  const filterColumns = originalResult?.columns ?? [];
  const headerGroups = table.getHeaderGroups();
  const rows = table.getRowModel().rows;

  const pinnedLeft = columnPinning.left ?? [];
  const {stickyStyle, isLastPinned} = getPinnedColumnHelpers(table, pinnedLeft);
  const hiding = getColumnHiding(allColumns, groupBy, showColumns);

  return (
    <Paper variant="outlined" sx={{position: 'relative', overflow: 'hidden'}}>
      {/* Header */}
      <Box
        sx={{
          display: 'flex',
          alignItems: 'center',
          flexWrap: 'wrap',
          gap: 1,
          px: 2,
          py: 1,
          borderBottom: 1,
          borderBottomColor: 'divider',
        }}>
        <Typography variant="h6" component="h2">
          Metrics
        </Typography>
        {(() => {
          const faqUrl = metricsFaqUrl();
          return faqUrl == null ? null : (
            <Tooltip title="Click to learn more about Metrics">
              <IconButton
                component="a"
                href={faqUrl}
                target="_blank"
                rel="noopener noreferrer"
                size="small"
                aria-label="Click to learn more about Metrics">
                <CircleHelp size={16} />
              </IconButton>
            </Tooltip>
          );
        })()}
        {result != null && (
          <Box
            component="span"
            sx={{color: COLOR.textTertiary, fontSize: TEXT.xs}}>
            {`${formatCount(totalCount)} ${totalCount === 1 ? 'row' : 'rows'}`}
          </Box>
        )}
        <EmptyColumnsToggle
          count={emptyCount}
          shown={showEmpty}
          onToggle={() => setShowEmpty(v => !v)}
        />
      </Box>

      {/* Metric collection picker */}
      <Box sx={{px: 2, pt: 2, pb: 1}}>
        <Autocomplete
          options={metricCollectionNames}
          value={metricCollectionNames.includes(metricName) ? metricName : null}
          onChange={onMetricNameChange}
          isOptionEqualToValue={(option, value) => option === value}
          renderInput={inputParams => (
            <TextField
              {...inputParams}
              label="Metric Collection"
              placeholder="Select a metric collection"
            />
          )}
          size="small"
          fullWidth
          sx={{maxWidth: 420}}
          slotProps={AUTOCOMPLETE_WORD_BREAK_PROPS}
        />
      </Box>

      {/* Filters */}
      {filterColumns.length > 0 && (
        <Box sx={{px: 2, py: 1}}>
          <EntityFilter
            columnDescriptions={filterColumns}
            filters={filters}
            onFiltersChange={handleFiltersChange}
            entityName={metricName}
            columnTypeaheadEntities={columnTypeaheadEntities}
          />
          <Box sx={{mt: 1}}>
            <FilterChips filters={filters} onChange={handleFiltersChange} />
          </Box>
        </Box>
      )}

      {/* Group-by and columns-to-show pickers, on one row */}
      {filterColumns.length > 0 && (
        <Box
          sx={{
            px: 2,
            py: 1,
            display: 'flex',
            flexWrap: 'wrap',
            gap: 1.5,
            alignItems: 'flex-start',
          }}>
          <GroupBySelector
            columnDescriptions={filterColumns}
            groupByColumns={groupBy}
            onGroupByChange={handleGroupByChange}
          />
          <ShowColumnsSelector
            columnDescriptions={filterColumns}
            groupByColumns={groupBy}
            showColumns={showColumns}
            onShowColumnsChange={handleShowColumnsChange}
          />
        </Box>
      )}

      {/* Error */}
      {error != null && (
        <ErrorState sx={{m: 2}} onClose={() => setError(null)}>
          {error}
        </ErrorState>
      )}

      {/* Initial loading (no data yet) */}
      {result == null && loading && <LoadingState label="Loading metrics..." />}

      {/* Refetch overlay, anchored to the Paper so the badge stays in view. */}
      {result != null && loading && <UpdatingOverlay />}

      {/* Table */}
      {result != null && (
        <TableShell
          fixedLayout
          minWidth={0}
          label="Metrics"
          tableSx={{width: table.getTotalSize(), minWidth: '100%'}}>
          <thead>
            {headerGroups.map(headerGroup => (
              <tr key={headerGroup.id}>
                {headerGroup.headers.map(header => {
                  const meta = header.column.columnDef.meta as
                    | {
                        numeric?: boolean;
                        colName?: string;
                        primaryKey?: boolean;
                      }
                    | undefined;
                  const isSorted = orderBy?.column === meta?.colName;
                  const isAsc =
                    isSorted && orderBy?.direction === OrderDirection.ASCENDING;
                  const isPinned = pinnedLeft.includes(header.column.id);
                  return (
                    <th
                      key={header.id}
                      className={[
                        'sortable',
                        meta?.numeric ? 'num' : '',
                        isLastPinned(header.column.id) ? 'last-pinned' : '',
                      ].join(' ')}
                      onClick={() => handleSort(header.column.id)}
                      style={{
                        ...stickyStyle(header.column.id, 3),
                        width: header.getSize(),
                      }}>
                      <Box
                        sx={{
                          display: 'flex',
                          alignItems: 'center',
                          justifyContent: meta?.numeric
                            ? 'flex-end'
                            : 'flex-start',
                          gap: 0.5,
                          color: meta?.primaryKey ? 'text.primary' : undefined,
                        }}>
                        <HeaderLabel
                          label={flexRender(
                            header.column.columnDef.header,
                            header.getContext(),
                          )}
                          sorted={isSorted ? (isAsc ? 'asc' : 'desc') : false}
                          align={meta?.numeric ? 'right' : 'left'}
                        />
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
                      {header.column.getCanResize() && (
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
                      )}
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
            {rows.length === 0 && !loading && (
              <tr>
                <td className="empty" colSpan={renderedIndices.length || 1}>
                  <EmptyState compact title="No data" />
                </td>
              </tr>
            )}
          </tbody>
        </TableShell>
      )}

      {/* Pagination */}
      {result != null && (
        <TablePagination
          component="div"
          count={totalCount}
          page={currentPage}
          onPageChange={handlePageChange}
          rowsPerPage={pageSize}
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
      )}
      {snackbar}
    </Paper>
  );
}
