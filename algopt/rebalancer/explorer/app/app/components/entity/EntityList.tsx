'use client';

import {useState, type ReactNode} from 'react';

import {
  Autocomplete,
  Box,
  Chip,
  Paper,
  TablePagination,
  TextField,
} from '@mui/material';
import {ArrowDown, ArrowUp} from 'lucide-react';

import {renderColumnTypeGroup} from '@/app/components/columnTypeGroup';
import {
  entityRowKey,
  useDetailPaneActions,
} from '@/app/components/detail-pane/DetailPaneContext';
import EntityLink from '@/app/components/detail-pane/EntityLink';
import EntityFilter from '@/app/components/EntityFilter';
import TablePaginationActions from '@/app/components/TablePaginationActions';
import {ELLIPSIS_SX, TABLE_SX} from '@/app/components/table/tableSx';
import FilterChips, {type ExtraChip} from '@/app/components/ui/FilterChips';
import {
  BaselineCurrentBars,
  OutInBar,
  ShareBar,
} from '@/app/components/ui/Bars';
import Pill from '@/app/components/ui/Pill';
import {EmptyState} from '@/app/components/ui/StateViews';
import {TAG_CHIP_SX} from '@/app/components/ui/Tag';
import {columnTypeInfo} from '@/lib/column-types';
import {
  fieldValues,
  fieldWidth,
  largestChange,
  type CellValue,
  type EntityField,
  type FieldProfile,
} from '@/lib/entity-fields';
import {
  AUTOCOMPLETE_WORD_BREAK_PROPS,
  formatCompact,
  formatCount,
  formatSignedCompact,
} from '@/lib/format';
import type {
  ColumnDescription,
  Filter,
  Handle,
  Result,
  RowData,
} from '@/lib/rebalancer-explorer-types';
import {ColumnType, OrderDirection} from '@/lib/rebalancer-explorer-types';
import {
  COLOR,
  focusRing,
  MONO_TEXT,
  PINNED_SHADOW,
  TABULAR,
  TEXT,
  WEIGHT,
} from '@/lib/ui-tokens';

import type {ItemMoves} from '@/lib/run-changes';

import type {ViewState} from './entity-view-state';

const PAGE_SIZE_OPTIONS = [10, 25, 50, 100];
// The backend groups by text columns only.
const GROUPABLE_TYPES = new Set([
  ColumnType.ASSIGNMENT,
  ColumnType.ENTITY_NAME,
  ColumnType.SCOPE,
  ColumnType.PARTITION,
  ColumnType.STRING,
]);
const MOVES_WIDTH = 116;
const CHANGE_WIDTH = 232;
// Constant columns listed before "+N more".
const SAME_PREVIEW = 4;

const KIND_CAPTION: Record<EntityField['kind'], string> = {
  name: '',
  count: 'grouped rows',
  assignment: 'assignment',
  partition: 'partition',
  scope: 'scope',
  dimension: 'dimension',
  utilization: 'utilization',
  other: '',
};

function caption(field: EntityField): string {
  const kind = KIND_CAPTION[field.kind];
  return field.a != null ? `${kind} · initial → final`.trim() : kind;
}

const ARROW = (
  <Box
    component="span"
    aria-label="to"
    sx={{mx: 0.5, color: COLOR.textDecorative}}>
    →
  </Box>
);

/** A value in the table: compact, exact on hover, 0 in the faint text. */
function NumberText({value, strong}: {value: number; strong?: boolean}) {
  return (
    <Box
      component="span"
      title={String(value)}
      sx={{
        ...TABULAR,
        color: value === 0 ? COLOR.textTertiary : undefined,
        fontWeight: strong ? WEIGHT.semibold : undefined,
      }}>
      {formatCompact(value)}
    </Box>
  );
}

function TextValue({
  value,
  entity,
  strong,
}: {
  value: string | null;
  entity: string | undefined;
  strong?: boolean;
}) {
  if (value == null) {
    return (
      <Box component="span" sx={{color: COLOR.textDecorative}}>
        —
      </Box>
    );
  }
  if (entity != null) {
    return <EntityLink entity={entity} id={value} />;
  }
  return (
    <Box
      component="span"
      data-overflow
      title={value}
      sx={{...ELLIPSIS_SX, fontWeight: strong ? WEIGHT.semibold : undefined}}>
      {value}
    </Box>
  );
}

function asNumber(v: CellValue): number {
  return typeof v === 'number' ? v : 0;
}

/** One field's cell: a value, or initial → final when they differ. */
function FieldCell({
  field,
  row,
  linkEntity,
}: {
  field: EntityField;
  row: RowData;
  linkEntity: string | undefined;
}) {
  const {a, b} = fieldValues(field, row);
  if (field.numeric) {
    const x = asNumber(a);
    const y = asNumber(b);
    if (field.a == null || x === y) {
      return <NumberText value={y} />;
    }
    return (
      <Box
        component="span"
        title={`${x} → ${y} (${formatSignedCompact(y - x)})`}
        sx={{whiteSpace: 'nowrap'}}>
        <Box component="span" sx={{color: COLOR.textTertiary}}>
          <NumberText value={x} />
        </Box>
        {ARROW}
        <NumberText value={y} strong />
      </Box>
    );
  }
  const x = typeof a === 'string' ? a : null;
  const y = typeof b === 'string' ? b : null;
  if (field.a == null || x === y) {
    return <TextValue value={y} entity={linkEntity} />;
  }
  return (
    <Box sx={{display: 'flex', alignItems: 'center', minWidth: 0}}>
      {/* The initial name gives way first; the final one matters more. */}
      <Box sx={{minWidth: 24, flex: '0 1000 auto', color: COLOR.textTertiary}}>
        <TextValue value={x} entity={linkEntity} />
      </Box>
      {ARROW}
      <Box sx={{minWidth: 0, flex: '0 1 auto'}}>
        <TextValue value={y} entity={linkEntity} strong />
      </Box>
    </Box>
  );
}

/** The value that changed most, relative to its size, and how many did. */
function ChangeCell({
  fields,
  row,
}: {
  fields: readonly EntityField[];
  row: RowData;
}) {
  const top = largestChange(fields, row);
  if (top == null) {
    return (
      <Box component="span" sx={{color: COLOR.textTertiary}}>
        no change
      </Box>
    );
  }
  return (
    <Box sx={{minWidth: 0}}>
      <Box
        title={top.field.label}
        sx={{...ELLIPSIS_SX, ...MONO_TEXT, color: COLOR.textSecondary}}>
        {top.field.label}
      </Box>
      <Box sx={{display: 'flex', alignItems: 'center', gap: 1}}>
        <BaselineCurrentBars a={top.a} b={top.b} width={48} />
        <Box component="span" sx={{whiteSpace: 'nowrap'}}>
          <Box component="span" sx={{color: COLOR.textTertiary}}>
            <NumberText value={top.a} />
          </Box>
          {ARROW}
          <NumberText value={top.b} strong />
        </Box>
        {top.changed > 1 && (
          <Box
            component="span"
            sx={{color: COLOR.textTertiary, fontSize: TEXT.xs, ...TABULAR}}>
            +{top.changed - 1} more
          </Box>
        )}
      </Box>
    </Box>
  );
}

/**
 * A group's member count over a bar on the page's scale: undefined while it
 * is counted, null when it is not counted.
 */
function MembersCell({n, max}: {n: number | null | undefined; max: number}) {
  if (n == null) {
    return (
      <Box
        component="span"
        title={n === null ? 'Not counted' : 'Counting…'}
        sx={{color: COLOR.textDecorative}}>
        {n === null ? '—' : '…'}
      </Box>
    );
  }
  return (
    <Box
      sx={{
        display: 'inline-flex',
        flexDirection: 'column',
        alignItems: 'flex-end',
        gap: 0.5,
      }}>
      <NumberText value={n} />
      <Box
        aria-hidden
        sx={{
          width: 64,
          height: 6,
          borderRadius: '999px',
          bgcolor: COLOR.bgSubtle,
          display: 'flex',
          justifyContent: 'flex-end',
          overflow: 'hidden',
        }}>
        <Box
          sx={{
            width: `${max > 0 ? Math.max(3, (100 * n) / max) : 0}%`,
            bgcolor: COLOR.borderStrong,
            borderRadius: '999px',
          }}
        />
      </Box>
    </Box>
  );
}

/** How many of a group's objects moved: "k of n" over the moved share. */
function GroupMovedCell({
  moved,
  members,
}: {
  moved: number;
  members: number | null;
}) {
  if (moved === 0) {
    return (
      <Box component="span" sx={{color: COLOR.textDecorative}}>
        —
      </Box>
    );
  }
  return (
    <Box
      sx={{
        display: 'inline-flex',
        flexDirection: 'column',
        alignItems: 'flex-end',
        gap: 0.5,
      }}>
      <Box component="span" sx={{...TABULAR, whiteSpace: 'nowrap'}}>
        {members != null
          ? `${formatCount(moved)} of ${formatCount(members)}`
          : formatCount(moved)}
      </Box>
      {members != null && members > 0 && (
        <ShareBar
          part={moved}
          whole={members}
          label={`${formatCount(moved)} of ${formatCount(members)} moved`}
        />
      )}
    </Box>
  );
}

/** Moves out and in as words, over a bar on the page's scale. */
function MovesCell({out, into, max}: {out: number; into: number; max: number}) {
  if (out === 0 && into === 0) {
    return (
      <Box component="span" sx={{color: COLOR.textDecorative}}>
        —
      </Box>
    );
  }
  return (
    <Box
      sx={{
        display: 'inline-flex',
        flexDirection: 'column',
        alignItems: 'flex-end',
        gap: 0.5,
      }}>
      <Box component="span" sx={{...TABULAR, whiteSpace: 'nowrap'}}>
        {out > 0 && `${formatCount(out)} out`}
        {out > 0 && into > 0 && ' · '}
        {into > 0 && `${formatCount(into)} in`}
      </Box>
      <OutInBar out={out} into={into} max={max} />
    </Box>
  );
}

function SortArrow({dir}: {dir: OrderDirection}) {
  return dir === OrderDirection.ASCENDING ? (
    <ArrowUp size={13} aria-hidden style={{flexShrink: 0}} />
  ) : (
    <ArrowDown size={13} aria-hidden style={{flexShrink: 0}} />
  );
}

const HEAD_BUTTON_SX = {
  display: 'flex',
  alignItems: 'center',
  gap: 0.5,
  maxWidth: '100%',
  minWidth: 0,
  p: 0,
  border: 0,
  bgcolor: 'transparent',
  font: 'inherit',
  color: 'inherit',
  cursor: 'pointer',
  borderRadius: '4px',
  '&:hover': {color: COLOR.textPrimary},
  '&:focus-visible': focusRing(),
} as const;

const STICKY = {position: 'sticky', left: 0} as const;

function rowName(fields: readonly EntityField[], row: RowData): string {
  const name = fields.find(f => f.kind === 'name');
  const v = name == null ? null : fieldValues(name, row).b;
  return typeof v === 'string' ? v : '';
}

export interface ObjectStatus {
  // The object's container pair, to mark moved rows.
  container: EntityField | null;
  // The 0/1 movable column, to mark fixed rows.
  movable: EntityField | null;
}

/**
 * The entity page's list: the row count, grouping and pages; the filter
 * editor and its chips; the columns that hold one value on every row, said
 * once; and the table of the rest.
 */
export default function EntityList({
  entityName,
  nameLabel,
  result,
  fields,
  columns,
  labels,
  profiles,
  viewState,
  onViewStateChange,
  rowEntity,
  handle,
  runId,
  linkEntities,
  objectStatus,
  moves,
  movesOrdered,
  rowGroup,
  groupMoved,
  groupSizes,
  allowGroupBy,
  showChange,
  columnDescriptions,
  columnTypeaheadEntities,
  extraChips,
  onClearAll,
  onShowField,
}: {
  entityName: string;
  nameLabel: string;
  result: Result;
  fields: readonly EntityField[];
  // Picked columns, in display order.
  columns: readonly EntityField[];
  // The row's label columns: its name, or the grouped-by columns and count.
  labels: readonly EntityField[];
  profiles: ReadonlyMap<string, FieldProfile>;
  viewState: ViewState;
  onViewStateChange: (update: (prev: ViewState) => ViewState) => void;
  // The entity a row is, to open it in the side panel; null when grouped.
  rowEntity: string | null;
  handle: Handle;
  runId: string;
  // Backend column → the entity its values name.
  linkEntities: Readonly<Record<string, string>>;
  objectStatus: ObjectStatus | null;
  moves: ReadonlyMap<string, ItemMoves> | null;
  // The rows come busiest first: the Moves header shows the order.
  movesOrdered: boolean;
  // A partition's page: rows are its groups, and a group opens its panel.
  rowGroup: string | null;
  // Moved objects and members per group, on a partition's page.
  groupMoved: ReadonlyMap<string, number> | null;
  groupSizes: ReadonlyMap<string, number | null> | null;
  allowGroupBy: boolean;
  showChange: boolean;
  columnDescriptions: ColumnDescription[];
  columnTypeaheadEntities: Record<string, string> | undefined;
  extraChips: ExtraChip[];
  onClearAll: () => void;
  onShowField: (key: string) => void;
}) {
  const {open} = useDetailPaneActions();
  const [sameOpen, setSameOpen] = useState(false);
  const rows = result.rows;
  const total = result.totalCount;
  // One scale for every page: the busiest item's moves.
  let maxMoves = 0;
  for (const m of moves?.values() ?? []) {
    maxMoves = Math.max(maxMoves, m.out, m.in);
  }
  const grouped = viewState.groupByColumns.length > 0;
  const page = Math.floor(viewState.offset / viewState.limit);
  const first = total === 0 ? 0 : viewState.offset + 1;
  const last = Math.min(viewState.offset + viewState.limit, total);

  const shown = new Set([...columns, ...labels].map(f => f.key));
  const same = fields.filter(f => {
    const p = profiles.get(f.key);
    return (
      !shown.has(f.key) &&
      f.kind !== 'name' &&
      p != null &&
      !p.blank &&
      p.constant &&
      rows.length >= 3
    );
  });
  const blank = fields.filter(
    f => !shown.has(f.key) && f.kind !== 'name' && profiles.get(f.key)?.blank,
  ).length;

  const setPage = (next: number) =>
    onViewStateChange(prev => ({...prev, offset: next * prev.limit}));
  const sortBy = (column: string) =>
    onViewStateChange(prev => {
      if (prev.orderColumn !== column) {
        return {
          ...prev,
          orderColumn: column,
          orderDirection: OrderDirection.ASCENDING,
          offset: 0,
        };
      }
      return prev.orderDirection === OrderDirection.ASCENDING
        ? {...prev, orderDirection: OrderDirection.DESCENDING, offset: 0}
        : {...prev, orderColumn: null, offset: 0};
    });
  const setFilters = (filters: Filter[]) =>
    onViewStateChange(prev => ({...prev, filters, offset: 0}));

  const openRow = (id: string) => {
    if (id === '') {
      return;
    }
    if (rowEntity != null) {
      open({kind: 'entity', payload: {entity: rowEntity, id, handle, runId}});
    } else if (rowGroup != null) {
      open({
        kind: 'group',
        payload: {partition: rowGroup, group: id, handle, runId},
      });
    }
  };
  const groupField =
    rowGroup != null ? (labels.find(f => f.key === rowGroup) ?? null) : null;
  let maxMembers = 0;
  for (const n of groupSizes?.values() ?? []) {
    maxMembers = Math.max(maxMembers, n ?? 0);
  }

  const linkFor = (field: EntityField): string | undefined =>
    linkEntities[field.columns[field.columns.length - 1]];

  const groupOptions = columnDescriptions.filter(c =>
    GROUPABLE_TYPES.has(c.type),
  );

  const head = (field: EntityField, label: ReactNode, numeric: boolean) => {
    const sorted = viewState.orderColumn === field.sortColumn;
    return (
      <Box
        component="button"
        type="button"
        onClick={() => sortBy(field.sortColumn)}
        aria-label={`Sort by ${field.label}`}
        title={`${field.label}${caption(field) !== '' ? ` · ${caption(field)}` : ''}${field.a != null ? ' · sorts by the final value' : ''}`}
        sx={{
          ...HEAD_BUTTON_SX,
          ml: numeric ? 'auto' : 0,
          flexDirection: numeric ? 'row-reverse' : 'row',
          color: sorted ? COLOR.textPrimary : 'inherit',
        }}>
        <Box
          component="span"
          sx={{
            display: 'flex',
            flexDirection: 'column',
            alignItems: numeric ? 'flex-end' : 'flex-start',
            minWidth: 0,
          }}>
          <Box component="span" sx={{...ELLIPSIS_SX, maxWidth: '100%'}}>
            {label}
          </Box>
          {caption(field) !== '' && (
            <Box
              component="span"
              sx={{
                ...ELLIPSIS_SX,
                maxWidth: '100%',
                fontSize: TEXT['2xs'],
                fontWeight: WEIGHT.regular,
                color: COLOR.textTertiary,
              }}>
              {caption(field)}
            </Box>
          )}
        </Box>
        {sorted && <SortArrow dir={viewState.orderDirection} />}
      </Box>
    );
  };

  const labelWidth = (f: EntityField) =>
    f.kind === 'count' ? 96 : grouped ? 200 : 240;
  const widths = [
    ...labels.map(labelWidth),
    ...(moves != null ? [MOVES_WIDTH] : []),
    ...(showChange ? [CHANGE_WIDTH] : []),
    ...columns.map(fieldWidth),
  ];
  const tableWidth = widths.reduce((s, w) => s + w, 0);

  return (
    <Paper variant="outlined" sx={{overflow: 'hidden', minWidth: 0}}>
      <Box
        sx={{
          display: 'flex',
          alignItems: 'center',
          flexWrap: 'wrap',
          gap: 1.5,
          px: 2,
          py: 1.25,
        }}>
        <Box component="span" role="status" sx={{color: COLOR.textSecondary}}>
          <Box component="b" sx={{color: COLOR.textPrimary, ...TABULAR}}>
            {`${formatCount(first)}–${formatCount(last)}`}
          </Box>
          {` of ${formatCount(total)} ${grouped ? (total === 1 ? 'group' : 'groups') : nameLabel}`}
        </Box>
        <Box
          sx={{
            ml: 'auto',
            display: 'flex',
            alignItems: 'center',
            flexWrap: 'wrap',
            gap: 1.5,
          }}>
          {allowGroupBy && groupOptions.length > 0 && (
            <Box sx={{display: 'flex', alignItems: 'center', gap: 1}}>
              <Box
                component="span"
                sx={{color: COLOR.textTertiary, fontSize: TEXT.xs}}>
                Group by
              </Box>
              <Autocomplete
                multiple
                size="small"
                options={groupOptions}
                value={groupOptions.filter(c =>
                  viewState.groupByColumns.includes(c.name),
                )}
                onChange={(_e, value) => {
                  const groupByColumns = value.map(c => c.name);
                  onViewStateChange(prev => ({
                    ...prev,
                    groupByColumns,
                    offset: 0,
                    orderColumn:
                      groupByColumns.length > 0 ? groupByColumns[0] : null,
                    orderDirection: OrderDirection.ASCENDING,
                  }));
                }}
                getOptionLabel={c => c.name}
                groupBy={c => columnTypeInfo(c.type).full}
                renderGroup={renderColumnTypeGroup}
                isOptionEqualToValue={(x, y) => x.name === y.name}
                renderTags={(value, getTagProps) =>
                  value.map((c, index) => {
                    const {key, ...chipProps} = getTagProps({index});
                    return (
                      <Chip
                        key={key}
                        label={c.name}
                        size="small"
                        sx={TAG_CHIP_SX}
                        {...chipProps}
                      />
                    );
                  })
                }
                renderInput={params => (
                  <TextField
                    {...params}
                    placeholder={
                      viewState.groupByColumns.length === 0 ? 'None' : ''
                    }
                    slotProps={{
                      htmlInput: {
                        ...params.inputProps,
                        'aria-label': 'Group by',
                      },
                    }}
                  />
                )}
                slotProps={AUTOCOMPLETE_WORD_BREAK_PROPS}
                sx={{width: 280}}
              />
            </Box>
          )}
          <TablePaginationActions
            count={total}
            page={page}
            rowsPerPage={viewState.limit}
            onPageChange={(_e, next) => setPage(next)}
          />
        </Box>
      </Box>
      <Box
        sx={{
          display: 'flex',
          flexDirection: 'column',
          gap: 1,
          px: 2,
          pb: 1.25,
        }}>
        {columnDescriptions.length > 0 && (
          <EntityFilter
            columnDescriptions={columnDescriptions}
            filters={viewState.filters}
            onFiltersChange={setFilters}
            entityName={entityName}
            columnTypeaheadEntities={columnTypeaheadEntities}
          />
        )}
        <FilterChips
          filters={viewState.filters}
          onChange={setFilters}
          extra={extraChips}
          onClearAll={onClearAll}
        />
        {(same.length > 0 || blank > 0) && (
          <Box
            sx={{
              display: 'flex',
              alignItems: 'center',
              flexWrap: 'wrap',
              gap: 0.75,
              fontSize: TEXT.xs,
              color: COLOR.textTertiary,
            }}>
            {same.length > 0 && (
              <>
                <Box component="span">
                  Same in all {formatCount(rows.length)} rows:
                </Box>
                {(sameOpen ? same : same.slice(0, SAME_PREVIEW)).map(f => {
                  const v = profiles.get(f.key)?.value ?? null;
                  const text =
                    typeof v === 'number' ? formatCompact(v) : String(v);
                  return (
                    <Box
                      key={f.key}
                      component="button"
                      type="button"
                      onClick={() => onShowField(f.key)}
                      title={`${f.label} = ${String(v)} in every row shown. Show as a column.`}
                      sx={{
                        display: 'inline-flex',
                        alignItems: 'baseline',
                        gap: 0.5,
                        maxWidth: 320,
                        height: 20,
                        px: 0.75,
                        border: `1px solid ${COLOR.borderDefault}`,
                        borderRadius: '6px',
                        bgcolor: COLOR.bgSubtle,
                        font: 'inherit',
                        color: COLOR.textSecondary,
                        cursor: 'pointer',
                        '&:hover': {borderColor: COLOR.borderStrong},
                        '&:focus-visible': focusRing(),
                      }}>
                      <Box component="span" sx={ELLIPSIS_SX}>
                        {f.label}
                      </Box>
                      <Box
                        component="span"
                        sx={{
                          ...ELLIPSIS_SX,
                          flexShrink: 0,
                          maxWidth: 140,
                          color: COLOR.textPrimary,
                          fontWeight: WEIGHT.medium,
                          ...TABULAR,
                        }}>
                        {text}
                      </Box>
                    </Box>
                  );
                })}
                {same.length > SAME_PREVIEW && (
                  <Box
                    component="button"
                    type="button"
                    onClick={() => setSameOpen(v => !v)}
                    sx={{
                      p: 0,
                      border: 0,
                      bgcolor: 'transparent',
                      font: 'inherit',
                      color: COLOR.action,
                      cursor: 'pointer',
                      '&:hover': {textDecoration: 'underline'},
                      '&:focus-visible': focusRing(),
                    }}>
                    {sameOpen
                      ? 'Show fewer'
                      : `+${same.length - SAME_PREVIEW} more`}
                  </Box>
                )}
              </>
            )}
            {blank > 0 && (
              <Box component="span">
                {same.length > 0 ? '· ' : ''}
                {formatCount(blank)} {blank === 1 ? 'column' : 'columns'} 0 or
                empty in every row, hidden
              </Box>
            )}
          </Box>
        )}
      </Box>
      {rows.length === 0 ? (
        <Box sx={{borderTop: `1px solid ${COLOR.borderDefault}`}}>
          <EmptyState title={`No ${nameLabel} match`}>
            {extraChips.length > 0 || viewState.filters.length > 0
              ? 'Try fewer filters, or clear them.'
              : null}
          </EmptyState>
        </Box>
      ) : (
        <Box
          sx={{
            overflowX: 'auto',
            borderTop: `1px solid ${COLOR.borderDefault}`,
          }}>
          <Box
            component="table"
            aria-label={entityName}
            sx={{
              ...TABLE_SX,
              tableLayout: 'fixed',
              width: tableWidth,
              minWidth: '100%',
              '& tbody tr': {
                bgcolor: COLOR.bgSurface,
                cursor: rowEntity != null ? 'pointer' : 'default',
              },
              '& tbody tr:last-child td': {borderBottom: 0},
            }}>
            <colgroup>
              {widths.map((w, i) => (
                <col key={i} style={{width: w}} />
              ))}
            </colgroup>
            <thead>
              <tr>
                {labels.map((f, i) => (
                  <Box
                    component="th"
                    key={f.key}
                    className={f.numeric ? 'num' : undefined}
                    sx={
                      i === 0
                        ? {...STICKY, zIndex: 3, boxShadow: PINNED_SHADOW}
                        : undefined
                    }>
                    {head(f, f.label, f.numeric)}
                  </Box>
                ))}
                {groupMoved != null && (
                  <>
                    <th
                      className="num"
                      title="Objects in the group (one count per group shown)">
                      Members
                    </th>
                    <th
                      className="num"
                      title="Objects of the group that moved, Initial → Final"
                      aria-sort={movesOrdered ? 'descending' : undefined}>
                      <Box
                        component="span"
                        sx={{
                          display: 'inline-flex',
                          alignItems: 'center',
                          gap: 0.5,
                        }}>
                        Moved
                        {movesOrdered && (
                          <SortArrow dir={OrderDirection.DESCENDING} />
                        )}
                      </Box>
                    </th>
                  </>
                )}
                {moves != null && (
                  <th
                    className="num"
                    title="Moved objects out and in, Initial → Final"
                    aria-sort={movesOrdered ? 'descending' : undefined}>
                    <Box
                      component="span"
                      sx={{
                        display: 'inline-flex',
                        alignItems: 'center',
                        gap: 0.5,
                      }}>
                      Moves
                      {movesOrdered && (
                        <SortArrow dir={OrderDirection.DESCENDING} />
                      )}
                    </Box>
                  </th>
                )}
                {showChange && (
                  <th title="The value that changed most relative to its size, Initial → Final">
                    Largest change
                  </th>
                )}
                {columns.map(f => (
                  <th key={f.key} className={f.numeric ? 'num' : undefined}>
                    {head(f, f.label, f.numeric)}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {rows.map((row, ri) => {
                const id =
                  rowEntity != null
                    ? rowName(fields, row)
                    : groupField != null
                      ? String(fieldValues(groupField, row).b ?? '')
                      : '';
                const paneKey =
                  rowEntity != null
                    ? entityRowKey(rowEntity, id)
                    : rowGroup != null && id !== ''
                      ? entityRowKey(rowGroup, id)
                      : null;
                return (
                  <tr
                    key={paneKey ?? ri}
                    data-pane-row={paneKey ?? undefined}
                    onClick={e => {
                      if (
                        (e.target as HTMLElement).closest('button, a, input') ==
                        null
                      ) {
                        openRow(id);
                      }
                    }}>
                    {labels.map((f, i) => (
                      <Box
                        component="td"
                        key={f.key}
                        className={f.numeric ? 'num' : undefined}
                        sx={
                          i === 0
                            ? {
                                ...STICKY,
                                zIndex: 1,
                                bgcolor: 'inherit',
                                boxShadow: PINNED_SHADOW,
                              }
                            : undefined
                        }>
                        {f.kind === 'name' && rowEntity != null ? (
                          <NameCell
                            id={id}
                            onOpen={() => openRow(id)}
                            status={objectStatus}
                            row={row}
                          />
                        ) : f === groupField ? (
                          <NameCell
                            id={id}
                            onOpen={() => openRow(id)}
                            status={null}
                            row={row}
                            control="group"
                          />
                        ) : f.numeric ? (
                          <NumberText value={asNumber(fieldValues(f, row).b)} />
                        ) : (
                          <TextValue
                            value={
                              (fieldValues(f, row).b as string | null) ?? null
                            }
                            entity={linkFor(f)}
                            strong
                          />
                        )}
                      </Box>
                    ))}
                    {groupMoved != null && (
                      <>
                        <td className="num">
                          <MembersCell
                            n={groupSizes?.get(id)}
                            max={maxMembers}
                          />
                        </td>
                        <td className="num">
                          <GroupMovedCell
                            moved={groupMoved.get(id) ?? 0}
                            members={groupSizes?.get(id) ?? null}
                          />
                        </td>
                      </>
                    )}
                    {moves != null && (
                      <td className="num">
                        <MovesCell
                          out={moves.get(id)?.out ?? 0}
                          into={moves.get(id)?.in ?? 0}
                          max={maxMoves}
                        />
                      </td>
                    )}
                    {showChange && (
                      <td>
                        <ChangeCell fields={fields} row={row} />
                      </td>
                    )}
                    {columns.map(f => (
                      <td key={f.key} className={f.numeric ? 'num' : undefined}>
                        <FieldCell
                          field={f}
                          row={row}
                          linkEntity={linkFor(f)}
                        />
                      </td>
                    ))}
                  </tr>
                );
              })}
            </tbody>
          </Box>
        </Box>
      )}
      <TablePagination
        component="div"
        count={total}
        page={page}
        onPageChange={(_e, next) => setPage(next)}
        rowsPerPage={viewState.limit}
        onRowsPerPageChange={e =>
          onViewStateChange(prev => ({
            ...prev,
            limit: parseInt(e.target.value, 10),
            offset: 0,
          }))
        }
        rowsPerPageOptions={PAGE_SIZE_OPTIONS}
        ActionsComponent={TablePaginationActions}
        sx={{
          borderTop: `1px solid ${COLOR.borderDefault}`,
          '& .MuiTablePagination-spacer': {display: 'none'},
          '& .MuiTablePagination-toolbar': {justifyContent: 'flex-start'},
        }}
      />
    </Paper>
  );
}

/** The row's name, which opens it in the side panel, and its status pills. */
function NameCell({
  id,
  onOpen,
  status,
  row,
  control = 'entity',
}: {
  id: string;
  onOpen: () => void;
  status: ObjectStatus | null;
  row: RowData;
  // What the pane stepper presses on this row.
  control?: 'entity' | 'group';
}) {
  let moved = false;
  let fixed = false;
  if (status?.container != null) {
    const {a, b} = fieldValues(status.container, row);
    moved = a !== b;
  }
  if (status?.movable != null) {
    fixed = fieldValues(status.movable, row).b === 0;
  }
  return (
    <Box sx={{display: 'flex', alignItems: 'center', gap: 0.75, minWidth: 0}}>
      <Box
        component="button"
        type="button"
        data-pane-open={control}
        data-overflow
        title={`${id}: open its details`}
        onClick={onOpen}
        sx={{
          ...ELLIPSIS_SX,
          minWidth: 0,
          p: 0,
          border: 0,
          bgcolor: 'transparent',
          font: 'inherit',
          fontWeight: WEIGHT.semibold,
          color: COLOR.textPrimary,
          textAlign: 'left',
          cursor: 'pointer',
          borderRadius: '4px',
          '&:hover': {color: COLOR.action, textDecoration: 'underline'},
          '&:focus-visible': focusRing(),
        }}>
        {id}
      </Box>
      {moved && (
        <Pill
          tone="info"
          title="Its container differs between Initial and Final">
          moved
        </Pill>
      )}
      {fixed && (
        <Pill title="Not movable: the solver keeps it where it is">fixed</Pill>
      )}
    </Box>
  );
}
