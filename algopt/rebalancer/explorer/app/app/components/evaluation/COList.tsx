'use client';

import {
  Fragment,
  useCallback,
  useEffect,
  useMemo,
  useState,
  type ReactNode,
} from 'react';

import {keyframes} from '@emotion/react';
import {
  Box,
  Button,
  FormControlLabel,
  IconButton,
  MenuItem,
  Paper,
  Switch,
  TextField,
  ToggleButton,
  ToggleButtonGroup,
  Tooltip,
} from '@mui/material';
import {
  ArrowDownWideNarrow,
  ArrowUpNarrowWide,
  Check,
  ChevronRight,
  ListTree,
  TriangleAlert,
} from 'lucide-react';

import {useDetailPaneActions} from '@/app/components/detail-pane/DetailPaneContext';
import EntityFilter from '@/app/components/EntityFilter';
import {
  groupRows,
  passes,
  SORT_DEFAULT_DIR,
  SORT_LABEL,
  sortRows,
  STATUS_LABEL,
  totals,
  type COGroup,
  type COQuery,
  type CORow,
  type GroupBy,
  type SortDir,
  type SortKey,
} from '@/app/components/evaluation/co-list';
import {PENALTY_HELP_TEXT} from '@/app/components/evaluation/evaluation-penalty';
import {formatIndexRanges} from '@/app/components/evaluation/evaluation-rows';
import PreciseNumber from '@/app/components/evaluation/PreciseNumber';
import {ChangeBar} from '@/app/components/ui/Bars';
import {ChangeIcon, changeTone} from '@/app/components/ui/Delta';
import FilterChips, {type ExtraChip} from '@/app/components/ui/FilterChips';
import HintIcon from '@/app/components/ui/HintIcon';
import Pill from '@/app/components/ui/Pill';
import {EmptyState} from '@/app/components/ui/StateViews';
import TupleBadge from '@/app/components/ui/TupleBadge';
import {formatPercentChange} from '@/lib/format';
import type {
  Assignment,
  ColumnDescription,
  Filter,
  Handle,
} from '@/lib/rebalancer-explorer-types';
import {ColumnType} from '@/lib/rebalancer-explorer-types';
import {
  COLOR,
  DURATION,
  focusRing,
  TABULAR,
  TEXT,
  WEIGHT,
} from '@/lib/ui-tokens';

const FILTER_COLUMNS: ColumnDescription[] = [
  {name: 'Name', type: ColumnType.STRING, primaryKey: false, description: ''},
  {name: 'A', type: ColumnType.DOUBLE, primaryKey: false, description: ''},
  {name: 'B', type: ColumnType.DOUBLE, primaryKey: false, description: ''},
  {name: 'B - A', type: ColumnType.DOUBLE, primaryKey: false, description: ''},
  {name: '%', type: ColumnType.DOUBLE, primaryKey: false, description: ''},
];

// A group opened from a Summary link flashes the action color once.
const focusFlash = keyframes`
  0%, 40% { box-shadow: inset 0 0 0 2px var(--color-action); }
  100% { box-shadow: inset 0 0 0 2px transparent; }
`;

/** status · name · [penalty] · A → B · B − A · bar · tree */
function gridSx(penaltyShown: boolean) {
  return {
    display: 'grid',
    gridTemplateColumns: penaltyShown
      ? '24px minmax(180px, 1fr) 200px 250px 170px 72px 32px'
      : '24px minmax(180px, 1fr) 250px 170px 72px 32px',
    columnGap: 1.5,
    alignItems: 'center',
    px: 2,
  } as const;
}

const NUM = {
  textAlign: 'right',
  minWidth: 0,
  overflowWrap: 'anywhere',
  ...TABULAR,
} as const;

const CAPTION = {color: COLOR.textTertiary, fontSize: TEXT.xs} as const;

function SideDot({side}: {side: 'A' | 'B'}) {
  return (
    <Box
      component="span"
      aria-hidden
      sx={{
        display: 'inline-block',
        width: 8,
        height: 8,
        mr: 0.5,
        borderRadius: '50%',
        bgcolor: side === 'A' ? COLOR.baseline : COLOR.current,
      }}
    />
  );
}

/** ↓ / ↑ for objectives; alert or check for constraints. */
function StatusIcon({row}: {row: CORow}) {
  const label = STATUS_LABEL[row.status];
  if (row.kind === 'objective') {
    return (
      <Tooltip title={label}>
        <Box component="span" sx={{display: 'inline-flex'}}>
          <ChangeIcon delta={row.delta} tone={changeTone(row.delta)} />
        </Box>
      </Tooltip>
    );
  }
  const broken = row.status === 'violated' || row.status === 'newlyViolated';
  const Icon = broken ? TriangleAlert : Check;
  const tone = broken
    ? {bg: COLOR.negativeBg, fg: COLOR.negativeText}
    : row.status === 'fixed'
      ? {bg: COLOR.positiveBg, fg: COLOR.positiveText}
      : {bg: COLOR.bgSubtle, fg: COLOR.textTertiary};
  return (
    <Tooltip title={label}>
      <Box
        component="span"
        role="img"
        aria-label={label}
        sx={{
          display: 'inline-grid',
          placeItems: 'center',
          width: 18,
          height: 18,
          borderRadius: '50%',
          bgcolor: tone.bg,
          color: tone.fg,
        }}>
        <Icon size={11} strokeWidth={2.5} aria-hidden />
      </Box>
    </Tooltip>
  );
}

/** A → B, with B in the primary weight. */
function Values({a, b, highlight}: {a: number; b: number; highlight: boolean}) {
  return (
    <Box sx={NUM}>
      <Box component="span" sx={{color: COLOR.textTertiary}}>
        <PreciseNumber value={a} highlight={highlight && a !== 0} />
      </Box>
      <Box
        component="span"
        aria-label="to"
        sx={{mx: 0.5, color: COLOR.textDecorative}}>
        →
      </Box>
      <Box component="span" sx={{fontWeight: WEIGHT.semibold}}>
        <PreciseNumber value={b} highlight={highlight && b !== 0} />
      </Box>
    </Box>
  );
}

/** B − A in the direction's color, with its percentage beneath. */
function Change({a, b}: {a: number; b: number}) {
  const delta = b - a;
  const pct = formatPercentChange(delta, a);
  return (
    <Box sx={NUM}>
      <PreciseNumber value={delta} highlight signed />
      {pct !== '' && (
        <Box sx={{...CAPTION, lineHeight: 1.3}} data-copy-skip>
          ({pct})
        </Box>
      )}
    </Box>
  );
}

function Penalty({a, b}: {a: number | null; b: number | null}) {
  if (a == null && b == null) {
    return <Box sx={{...NUM, color: COLOR.textTertiary}}>—</Box>;
  }
  return (
    <Box sx={NUM}>
      <Box>
        <PreciseNumber value={a ?? 0} highlight={false} />
        <Box component="span" sx={{mx: 0.5, color: COLOR.textDecorative}}>
          →
        </Box>
        <PreciseNumber value={b ?? 0} highlight={false} />
      </Box>
      <Box sx={{fontSize: TEXT.xs}}>
        <PreciseNumber value={(b ?? 0) - (a ?? 0)} highlight signed />
      </Box>
    </Box>
  );
}

function groupLabel(
  g: {kind: string; tupleIndex: number | null},
  single: boolean,
) {
  if (g.kind === 'constraint') {
    return 'Constraints';
  }
  return single ? 'Objectives' : `Tuple index ${g.tupleIndex}`;
}

/** "Constraints, tuple index 0, 2, 4–6" for a run of folded groups. */
function describeGroups(groups: COGroup[], single: boolean): string {
  const parts: string[] = [];
  const tuples: number[] = [];
  for (const g of groups) {
    if (g.kind === 'objective' && !single && g.tupleIndex != null) {
      tuples.push(g.tupleIndex);
    } else {
      parts.push(groupLabel(g, single));
    }
  }
  if (tuples.length > 0) {
    parts.push(`tuple index ${formatIndexRanges(tuples)}`);
  }
  const text = parts.join(', ');
  return text.charAt(0).toUpperCase() + text.slice(1);
}

export interface PaneContext {
  handle: Handle;
  sourceAssignment: Assignment;
  destinationAssignment: Assignment;
  srcLabel: string;
  dstLabel: string;
  maxTupleIndex: number;
}

/**
 * The constraints and objectives as one list: grouped by tuple index (or
 * not), each row its status, name and description, A → B, the change and a
 * change bar. A row opens its spec in the side panel; the tree button opens
 * its expression tree.
 */
export default function COList({
  rows,
  query,
  group,
  onGroupChange,
  sort,
  dir,
  onSortChange,
  hasPenalty,
  onPenaltyShownChange,
  evalFilters,
  onEvalFiltersChange,
  extraChips,
  onClearAll,
  pane,
  focusTupleIndex,
}: {
  rows: CORow[];
  query: COQuery;
  group: GroupBy;
  onGroupChange: (group: GroupBy) => void;
  sort: SortKey;
  dir: SortDir;
  onSortChange: (sort: SortKey, dir: SortDir) => void;
  hasPenalty: boolean;
  onPenaltyShownChange: (shown: boolean) => void;
  evalFilters: Filter[];
  onEvalFiltersChange: (filters: Filter[]) => void;
  extraChips: ExtraChip[];
  onClearAll: () => void;
  pane: PaneContext;
  focusTupleIndex: number | null;
}) {
  const {open: openPane} = useDetailPaneActions();
  const penaltyShown = hasPenalty && query.penaltyShown;
  const single = pane.maxTupleIndex === 0;
  const grid = gridSx(penaltyShown);

  // Groups showing their unchanged rows too; a Summary link opens its tuple
  // that way.
  const [showUnchanged, setShowUnchanged] = useState<ReadonlySet<string>>(
    () => new Set(focusTupleIndex != null ? [`t${focusTupleIndex}`] : []),
  );
  const [folded, setFolded] = useState<ReadonlySet<string>>(() => new Set());
  // "Changed" again resets the groups opened one by one.
  const [prevChangedOnly, setPrevChangedOnly] = useState(query.changedOnly);
  if (prevChangedOnly !== query.changedOnly) {
    setPrevChangedOnly(query.changedOnly);
    setShowUnchanged(new Set());
  }

  const groups = useMemo(
    () => groupRows(rows, query, sort, dir, showUnchanged),
    [rows, query, sort, dir, showUnchanged],
  );
  const flat = useMemo(
    () =>
      group === 'none'
        ? sortRows(
            rows.filter(r => passes(r, query)),
            sort,
            dir,
          )
        : null,
    [group, rows, query, sort, dir],
  );
  const shownCount =
    flat?.length ?? groups.reduce((n, g) => n + g.shown.length, 0);
  const hiddenUnchanged =
    flat != null
      ? rows.filter(r => passes(r, query, null, true)).length - flat.length
      : groups.reduce((n, g) => n + g.hiddenUnchanged, 0);

  const nameOptions = useMemo(
    () => ({Name: Array.from(new Set(rows.map(r => r.name))).sort()}),
    [rows],
  );

  const openRow = useCallback(
    (row: CORow, tab: 'spec' | 'tree') =>
      openPane({
        kind: 'expression',
        tab,
        payload: {
          expressionId: row.expressionId,
          name: row.name,
          description: row.description,
          expressionType:
            row.kind === 'constraint' ? 'CONSTRAINT' : 'OBJECTIVE',
          tupleIndex: row.tupleIndex,
          srcValue: row.srcValue,
          dstValue: row.dstValue,
          delta: row.delta,
          percentage: row.percentage,
          srcPenalty: row.srcPenalty,
          dstPenalty: row.dstPenalty,
          ...pane,
        },
      }),
    [openPane, pane],
  );

  // Scroll to the linked tuple once it has rendered, and again after the
  // cards above it have settled.
  useEffect(() => {
    if (focusTupleIndex == null) {
      return;
    }
    const scroll = () =>
      document
        .querySelector(`[data-co-group="t${focusTupleIndex}"]`)
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

  const toggleIn = (set: ReadonlySet<string>, keys: string[], on: boolean) => {
    const next = new Set(set);
    for (const k of keys) {
      if (on) {
        next.add(k);
      } else {
        next.delete(k);
      }
    }
    return next;
  };

  const renderRow = (row: CORow) => (
    <Box
      key={row.expressionId}
      role="row"
      data-pane-row={row.expressionId}
      onClick={e => {
        if ((e.target as HTMLElement).closest('button, a, input') == null) {
          openRow(row, 'spec');
        }
      }}
      sx={{
        ...grid,
        py: 1,
        cursor: 'pointer',
        borderBottom: `1px solid ${COLOR.borderDefault}`,
        '&:last-child': {borderBottom: 0},
        '&:hover': {bgcolor: COLOR.bgSurface2},
      }}>
      <Box role="cell" sx={{display: 'flex'}}>
        <StatusIcon row={row} />
      </Box>
      <Box role="cell" sx={{minWidth: 0}}>
        <Box sx={{display: 'flex', alignItems: 'center', gap: 1, minWidth: 0}}>
          {group === 'none' &&
            (row.kind === 'constraint' ? (
              <Pill>constraint</Pill>
            ) : (
              !single &&
              row.tupleIndex != null && (
                <TupleBadge
                  tupleIndex={row.tupleIndex}
                  maxTupleIndex={pane.maxTupleIndex}
                />
              )
            ))}
          <Box
            component="button"
            type="button"
            data-pane-open="spec"
            title={`${row.name}: open its spec`}
            onClick={() => openRow(row, 'spec')}
            sx={{
              minWidth: 0,
              p: 0,
              border: 0,
              bgcolor: 'transparent',
              font: 'inherit',
              color: COLOR.textPrimary,
              fontWeight: WEIGHT.semibold,
              textAlign: 'left',
              cursor: 'pointer',
              overflow: 'hidden',
              textOverflow: 'ellipsis',
              whiteSpace: 'nowrap',
              borderRadius: '4px',
              '&:hover': {color: COLOR.action, textDecoration: 'underline'},
              '&:focus-visible': focusRing(),
            }}>
            {row.name}
          </Box>
        </Box>
        {row.description !== '' && (
          <Box
            title={row.description}
            sx={{
              ...CAPTION,
              overflow: 'hidden',
              textOverflow: 'ellipsis',
              whiteSpace: 'nowrap',
            }}>
            {row.description}
          </Box>
        )}
      </Box>
      {penaltyShown && (
        <Box role="cell">
          <Penalty a={row.srcPenalty} b={row.dstPenalty} />
        </Box>
      )}
      <Box role="cell">
        <Values
          a={row.srcValue}
          b={row.dstValue}
          highlight={row.kind === 'constraint'}
        />
      </Box>
      <Box role="cell">
        <Change a={row.srcValue} b={row.dstValue} />
      </Box>
      <Box role="cell" sx={{display: 'flex', justifyContent: 'flex-end'}}>
        <ChangeBar
          delta={row.delta}
          baseline={row.srcValue}
          good={row.delta < 0}
        />
      </Box>
      <Box role="cell">
        <Tooltip title="Expression tree">
          <IconButton
            size="small"
            aria-label={`Expression tree of ${row.name}`}
            data-pane-open="tree"
            onClick={() => openRow(row, 'tree')}>
            <ListTree size={16} />
          </IconButton>
        </Tooltip>
      </Box>
    </Box>
  );

  const renderGroupHead = (g: COGroup) => {
    const t = totals(g.shown);
    const isFolded = folded.has(g.key);
    const label = groupLabel(g, single);
    const focused = g.tupleIndex === focusTupleIndex && g.kind === 'objective';
    const opened = showUnchanged.has(g.key);
    return (
      <Box
        role="row"
        data-co-group={g.key}
        sx={{
          ...grid,
          py: 1,
          bgcolor: COLOR.bgSurface2,
          borderBottom: `1px solid ${COLOR.borderDefault}`,
          scrollMarginTop: 120,
          ...(focused && {animation: `${focusFlash} 2.4s ease-out 0.3s both`}),
        }}>
        <Box
          role="cell"
          sx={{
            gridColumn: 'span 2',
            display: 'flex',
            alignItems: 'center',
            flexWrap: 'wrap',
            columnGap: 1,
            minWidth: 0,
          }}>
          <IconButton
            size="small"
            aria-expanded={!isFolded}
            aria-label={`${isFolded ? 'Show' : 'Fold'} ${label}`}
            onClick={() =>
              setFolded(prev => toggleIn(prev, [g.key], !prev.has(g.key)))
            }
            sx={{
              width: 24,
              height: 24,
              ml: -0.5,
              color: COLOR.textDecorative,
              '& svg': {
                transform: isFolded ? 'none' : 'rotate(90deg)',
                transition: `transform ${DURATION.base}ms`,
              },
            }}>
            <ChevronRight size={16} />
          </IconButton>
          {g.kind === 'objective' && !single && g.tupleIndex != null && (
            <TupleBadge
              tupleIndex={g.tupleIndex}
              maxTupleIndex={pane.maxTupleIndex}
            />
          )}
          <Box
            component="h3"
            sx={{
              m: 0,
              fontFamily: 'inherit',
              fontSize: TEXT.sm,
              fontWeight: WEIGHT.semibold,
            }}>
            {label}
          </Box>
          <Box component="span" sx={CAPTION}>
            {g.shown.length} of {g.rows.length} shown
          </Box>
          {(g.hiddenUnchanged > 0 || opened) && query.changedOnly && (
            <Button
              size="small"
              variant="text"
              onClick={() =>
                setShowUnchanged(prev => toggleIn(prev, [g.key], !opened))
              }>
              {opened
                ? 'Hide unchanged'
                : `Show unchanged (${g.hiddenUnchanged})`}
            </Button>
          )}
        </Box>
        {penaltyShown && (
          <Box role="cell">
            <Penalty a={t.srcPenalty} b={t.dstPenalty} />
          </Box>
        )}
        <Tooltip title={`Sum of the ${g.shown.length} rows shown`}>
          <Box role="cell">
            <Values a={t.src} b={t.dst} highlight={g.kind === 'constraint'} />
          </Box>
        </Tooltip>
        <Box role="cell">
          <Change a={t.src} b={t.dst} />
        </Box>
        <Box role="cell" sx={{gridColumn: 'span 2'}} />
      </Box>
    );
  };

  /** One line for a run of groups with nothing to show. */
  const renderFoldedRun = (run: COGroup[]) => {
    const canShow = query.changedOnly && run.some(g => g.hiddenUnchanged > 0);
    return (
      <Box
        key={`run-${run[0].key}`}
        sx={{
          display: 'flex',
          alignItems: 'center',
          flexWrap: 'wrap',
          gap: 1.25,
          px: 2,
          py: 0.75,
          minHeight: 40,
          bgcolor: COLOR.bgSurface2,
          borderBottom: `1px solid ${COLOR.borderDefault}`,
          '&:last-child': {borderBottom: 0},
        }}>
        <Box
          component="span"
          aria-hidden
          sx={{display: 'flex', color: COLOR.textDecorative}}>
          <ChevronRight size={16} />
        </Box>
        <Box component="span" sx={{fontWeight: WEIGHT.semibold}}>
          {describeGroups(run, single)}
        </Box>
        <Box component="span" sx={CAPTION}>
          {canShow ? 'no changed rows' : 'no rows match the filters'}
        </Box>
        {canShow && (
          <Button
            size="small"
            variant="text"
            sx={{ml: 'auto'}}
            onClick={() =>
              setShowUnchanged(prev =>
                toggleIn(
                  prev,
                  run.map(g => g.key),
                  true,
                ),
              )
            }>
            Show
          </Button>
        )}
      </Box>
    );
  };

  const body = () => {
    if (
      shownCount === 0 &&
      (flat != null || groups.every(g => g.hiddenUnchanged === 0))
    ) {
      return (
        <EmptyState title="No constraints or objectives match these filters">
          <Button size="small" variant="outlined" onClick={onClearAll}>
            Clear filters
          </Button>
        </EmptyState>
      );
    }
    if (flat != null) {
      return flat.map(renderRow);
    }
    const out: ReactNode[] = [];
    let run: COGroup[] = [];
    const flush = () => {
      if (run.length > 0) {
        out.push(renderFoldedRun(run));
        run = [];
      }
    };
    for (const g of groups) {
      if (g.shown.length === 0) {
        run.push(g);
        continue;
      }
      flush();
      out.push(
        <Fragment key={g.key}>
          {renderGroupHead(g)}
          {!folded.has(g.key) && g.shown.map(renderRow)}
        </Fragment>,
      );
    }
    flush();
    return out;
  };

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
            {shownCount}
          </Box>{' '}
          of {rows.length} shown
          {hiddenUnchanged > 0 && ` · ${hiddenUnchanged} unchanged hidden`}
        </Box>
        <Box
          sx={{
            ml: 'auto',
            display: 'flex',
            alignItems: 'center',
            flexWrap: 'wrap',
            gap: 1.5,
          }}>
          {hasPenalty && (
            <FormControlLabel
              sx={{mr: 0, gap: 0.5}}
              control={
                <Switch
                  size="small"
                  checked={query.penaltyShown}
                  onChange={(_e, checked) => onPenaltyShownChange(checked)}
                />
              }
              label={
                <Box
                  component="span"
                  sx={{display: 'inline-flex', alignItems: 'center', gap: 0.5}}>
                  Penalties
                  <HintIcon title={PENALTY_HELP_TEXT} label="Help: Penalties" />
                </Box>
              }
            />
          )}
          <Box sx={{display: 'flex', alignItems: 'center', gap: 1}}>
            <Box component="span" sx={CAPTION}>
              Group
            </Box>
            <ToggleButtonGroup
              size="small"
              exclusive
              aria-label="Group by"
              value={group}
              onChange={(_e, value: GroupBy | null) => {
                if (value != null) {
                  onGroupChange(value);
                }
              }}>
              <ToggleButton value="tuple">Tuple index</ToggleButton>
              <ToggleButton value="none">None</ToggleButton>
            </ToggleButtonGroup>
          </Box>
          <Box sx={{display: 'flex', alignItems: 'center', gap: 1}}>
            <Box component="span" sx={CAPTION}>
              Sort
            </Box>
            <TextField
              select
              size="small"
              value={sort}
              onChange={e => {
                const key = e.target.value as SortKey;
                onSortChange(key, SORT_DEFAULT_DIR[key]);
              }}
              slotProps={{htmlInput: {'aria-label': 'Sort by'}}}
              sx={{minWidth: 150}}>
              {(Object.keys(SORT_LABEL) as SortKey[]).map(key => (
                <MenuItem key={key} value={key}>
                  {SORT_LABEL[key]}
                </MenuItem>
              ))}
            </TextField>
            <Tooltip title={dir === 'asc' ? 'Ascending' : 'Descending'}>
              <IconButton
                size="small"
                aria-label={`Sort ${dir === 'asc' ? 'descending' : 'ascending'}`}
                onClick={() =>
                  onSortChange(sort, dir === 'asc' ? 'desc' : 'asc')
                }>
                {dir === 'asc' ? (
                  <ArrowUpNarrowWide size={16} />
                ) : (
                  <ArrowDownWideNarrow size={16} />
                )}
              </IconButton>
            </Tooltip>
          </Box>
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
        <EntityFilter
          columnDescriptions={FILTER_COLUMNS}
          filters={evalFilters}
          onFiltersChange={onEvalFiltersChange}
          entityName="expression"
          localOptions={nameOptions}
        />
        <FilterChips
          filters={evalFilters}
          onChange={onEvalFiltersChange}
          extra={extraChips}
          onClearAll={onClearAll}
        />
      </Box>
      <Box sx={{overflowX: 'auto'}}>
        <Box
          role="table"
          aria-label="Constraints and objectives"
          sx={{minWidth: penaltyShown ? 1040 : 820}}>
          <Box
            role="row"
            sx={{
              ...grid,
              py: 1,
              bgcolor: COLOR.bgSurface2,
              borderTop: `1px solid ${COLOR.borderDefault}`,
              borderBottom: `1px solid ${COLOR.borderDefault}`,
              color: COLOR.textTertiary,
              fontSize: TEXT.xs,
              fontWeight: WEIGHT.semibold,
            }}>
            <span role="columnheader" aria-label="Status" />
            <span role="columnheader">Constraint / objective</span>
            {penaltyShown && (
              <Box role="columnheader" sx={{textAlign: 'right'}}>
                Penalty A → B
              </Box>
            )}
            <Box
              role="columnheader"
              sx={{
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'flex-end',
                gap: 0.5,
                whiteSpace: 'nowrap',
              }}>
              <SideDot side="A" />A · {pane.srcLabel}
              <Box component="span" sx={{color: COLOR.textDecorative}}>
                →
              </Box>
              <SideDot side="B" />B · {pane.dstLabel}
              <HintIcon
                title="Each expression evaluated with assignment A and with B. Click a number to switch between formatted and raw."
                label="Help: A and B"
              />
            </Box>
            <Box
              role="columnheader"
              sx={{
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'flex-end',
                gap: 0.5,
              }}>
              B − A
              <HintIcon
                title="The change from A to B, with the percentage of |A| beneath. Lower is better: the solver minimizes objectives, and a constraint is fine only at 0."
                label="Help: B − A"
              />
            </Box>
            <span role="columnheader" aria-label="Change bar" />
            <span role="columnheader" aria-label="Open" />
          </Box>
          {body()}
        </Box>
      </Box>
    </Paper>
  );
}
