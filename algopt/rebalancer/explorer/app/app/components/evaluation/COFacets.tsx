'use client';

import type {ReactNode} from 'react';

import {
  Box,
  Checkbox,
  InputAdornment,
  TextField,
  ToggleButton,
  ToggleButtonGroup,
} from '@mui/material';
import {Search} from 'lucide-react';

import {
  CONSTRAINT_STATUSES,
  OBJECTIVE_STATUSES,
  STATUS_HELP,
  STATUS_LABEL,
  type COFacets as Facets,
  type FacetCounts,
  type RowKind,
  type RowStatus,
} from '@/app/components/evaluation/co-list';
import HintIcon from '@/app/components/ui/HintIcon';
import TupleBadge from '@/app/components/ui/TupleBadge';
import {COLOR, EYEBROW_TEXT, radius, TABULAR, TEXT} from '@/lib/ui-tokens';

const KIND_LABEL: Record<RowKind, string> = {
  objective: 'Objective',
  constraint: 'Constraint',
};

const KIND_HELP: Record<RowKind, string> = {
  objective:
    'Minimized by the solver in tuple index order: a higher index only matters when every lower index ties.',
  constraint:
    'How far a hard constraint is broken. The solver never accepts a move that breaks one further, so it should stay at 0.',
};

function toggle<T>(list: readonly T[], item: T): T[] {
  return list.includes(item) ? list.filter(x => x !== item) : [...list, item];
}

function Section({title, children}: {title: string; children: ReactNode}) {
  return (
    <Box component="section" sx={{display: 'flex', flexDirection: 'column'}}>
      <Box
        component="h3"
        sx={{...EYEBROW_TEXT, fontFamily: 'inherit', m: 0, mb: 0.75, px: 0.75}}>
        {title}
      </Box>
      {children}
    </Box>
  );
}

/** A checkbox row with the number of rows ticking it would show. */
function Option({
  name,
  checked,
  onToggle,
  count,
  hint,
  title,
  children,
}: {
  name: string;
  checked: boolean;
  onToggle: () => void;
  count: number;
  hint?: string;
  title?: string;
  children: ReactNode;
}) {
  return (
    <Box
      component="label"
      title={title}
      sx={{
        display: 'flex',
        alignItems: 'center',
        gap: 1,
        minHeight: 28,
        px: 0.75,
        borderRadius: radius('md'),
        cursor: 'pointer',
        color: count === 0 && !checked ? COLOR.textTertiary : undefined,
        '&:hover': {bgcolor: COLOR.bgSubtle},
      }}>
      <Checkbox
        size="small"
        checked={checked}
        onChange={onToggle}
        sx={{p: 0}}
      />
      <Box
        component="span"
        sx={{
          display: 'inline-flex',
          alignItems: 'center',
          gap: 0.75,
          minWidth: 0,
          flex: 1,
        }}>
        {children}
        {hint != null && <HintIcon title={hint} label={`Help: ${name}`} />}
      </Box>
      <Box
        component="span"
        sx={{
          flexShrink: 0,
          color: COLOR.textTertiary,
          fontSize: TEXT['2xs'],
          ...TABULAR,
        }}>
        {count}
      </Box>
    </Box>
  );
}

/**
 * Filters for the constraints & objectives list: a search, changed or all,
 * and facets by kind, status and tuple index, each option with the number of
 * rows it would show.
 */
export default function COFacets({
  search,
  onSearchChange,
  changedOnly,
  onChangedOnlyChange,
  total,
  facets,
  onFacetsChange,
  counts,
  kinds,
  tuples,
  maxTupleIndex,
}: {
  search: string;
  onSearchChange: (search: string) => void;
  changedOnly: boolean;
  onChangedOnlyChange: (changedOnly: boolean) => void;
  total: number;
  facets: Facets;
  onFacetsChange: (facets: Facets) => void;
  counts: FacetCounts;
  // Kinds the run has.
  kinds: readonly RowKind[];
  // Each tuple index with its objective names.
  tuples: ReadonlyArray<{tupleIndex: number; names: string[]}>;
  maxTupleIndex: number;
}) {
  const statuses: RowStatus[] = [
    ...(kinds.includes('objective') ? OBJECTIVE_STATUSES : []),
    ...(kinds.includes('constraint') ? CONSTRAINT_STATUSES : []),
  ];
  return (
    <Box
      component="aside"
      aria-label="Filters"
      sx={{display: 'flex', flexDirection: 'column', gap: 2.5}}>
      <TextField
        size="small"
        placeholder="Filter by name or description…"
        value={search}
        onChange={e => onSearchChange(e.target.value)}
        slotProps={{
          htmlInput: {'aria-label': 'Filter by name or description'},
          input: {
            startAdornment: (
              <InputAdornment position="start">
                <Search size={14} />
              </InputAdornment>
            ),
          },
        }}
      />
      <Section title="Show">
        <ToggleButtonGroup
          size="small"
          exclusive
          fullWidth
          aria-label="Rows to show"
          value={changedOnly ? 'changed' : 'all'}
          onChange={(_e, value: 'changed' | 'all' | null) => {
            if (value != null) {
              onChangedOnlyChange(value === 'changed');
            }
          }}>
          <ToggleButton value="changed">Changed</ToggleButton>
          <ToggleButton value="all">All {total}</ToggleButton>
        </ToggleButtonGroup>
      </Section>
      {kinds.length > 1 && (
        <Section title="Kind">
          {kinds.map(kind => (
            <Option
              key={kind}
              name={KIND_LABEL[kind]}
              checked={facets.kinds.includes(kind)}
              onToggle={() =>
                onFacetsChange({...facets, kinds: toggle(facets.kinds, kind)})
              }
              count={counts.kinds.get(kind) ?? 0}
              hint={KIND_HELP[kind]}>
              {KIND_LABEL[kind]}
            </Option>
          ))}
        </Section>
      )}
      <Section title="Status">
        {statuses.map(status => (
          <Option
            key={status}
            name={STATUS_LABEL[status]}
            checked={facets.statuses.includes(status)}
            onToggle={() =>
              onFacetsChange({
                ...facets,
                statuses: toggle(facets.statuses, status),
              })
            }
            count={counts.statuses.get(status) ?? 0}
            title={STATUS_HELP[status]}>
            {STATUS_LABEL[status]}
          </Option>
        ))}
      </Section>
      {tuples.length > 1 && (
        <Section title="Tuple index">
          {tuples.map(t => (
            <Option
              key={t.tupleIndex}
              name={`Tuple index ${t.tupleIndex}`}
              checked={facets.tuples.includes(t.tupleIndex)}
              onToggle={() =>
                onFacetsChange({
                  ...facets,
                  tuples: toggle(facets.tuples, t.tupleIndex),
                })
              }
              count={counts.tuples.get(t.tupleIndex) ?? 0}>
              <TupleBadge
                tupleIndex={t.tupleIndex}
                maxTupleIndex={maxTupleIndex}
                hint={null}
              />
              <Box
                component="span"
                title={t.names.join(', ')}
                sx={{
                  minWidth: 0,
                  overflow: 'hidden',
                  textOverflow: 'ellipsis',
                  whiteSpace: 'nowrap',
                  fontSize: TEXT.xs,
                  color: COLOR.textSecondary,
                }}>
                {t.names[0]}
                {t.names.length > 1 && ` +${t.names.length - 1}`}
              </Box>
            </Option>
          ))}
        </Section>
      )}
    </Box>
  );
}
