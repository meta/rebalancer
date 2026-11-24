'use client';

import {useState, type ReactNode} from 'react';

import {
  Box,
  Button,
  Checkbox,
  InputAdornment,
  TextField,
  ToggleButton,
  ToggleButtonGroup,
} from '@mui/material';
import {Search} from 'lucide-react';

import HintIcon from '@/app/components/ui/HintIcon';
import {formatCount} from '@/lib/format';
import {
  isFolded,
  MIN_ROWS_TO_FOLD,
  type EntityField,
  type FieldKind,
  type FieldProfile,
} from '@/lib/entity-fields';
import {
  COLOR,
  EYEBROW_TEXT,
  focusRing,
  radius,
  TABULAR,
  TEXT,
} from '@/lib/ui-tokens';

export const KIND_SECTIONS: ReadonlyArray<{
  kind: FieldKind;
  title: string;
  sub?: string;
}> = [
  {kind: 'assignment', title: 'Assignments', sub: 'initial → final'},
  {kind: 'partition', title: 'Partitions'},
  {kind: 'scope', title: 'Scopes'},
  {kind: 'utilization', title: 'Utilization', sub: 'initial → final'},
  {kind: 'dimension', title: 'Dimensions'},
  {kind: 'other', title: 'Other'},
];

// A long section lists this many columns until it is opened.
const SECTION_PREVIEW = 8;

function Heading({children, end}: {children: ReactNode; end?: ReactNode}) {
  return (
    <Box
      sx={{
        display: 'flex',
        alignItems: 'baseline',
        gap: 1,
        mb: 0.75,
        px: 0.75,
      }}>
      <Box component="h3" sx={{...EYEBROW_TEXT, fontFamily: 'inherit', m: 0}}>
        {children}
      </Box>
      {end != null && <Box sx={{ml: 'auto'}}>{end}</Box>}
    </Box>
  );
}

function hintFor(
  field: EntityField,
  p: FieldProfile | undefined,
  rowCount: number,
): {text: string; title: string} | null {
  if (p == null || !isFolded(p, rowCount)) {
    return null;
  }
  if (p.blank) {
    return field.numeric
      ? {text: 'all 0', title: '0 in every row shown'}
      : {text: 'empty', title: 'Empty in every row shown'};
  }
  return {
    text: 'same',
    title: `${String(p.value)} in every row shown`,
  };
}

function ColumnOption({
  field,
  checked,
  locked,
  onToggle,
  hint,
}: {
  field: EntityField;
  checked: boolean;
  locked: boolean;
  onToggle: () => void;
  hint: {text: string; title: string} | null;
}) {
  return (
    <Box
      component="label"
      title={locked ? `${field.label}: grouped by, always shown` : field.label}
      sx={{
        display: 'flex',
        alignItems: 'center',
        gap: 1,
        minHeight: 26,
        px: 0.75,
        borderRadius: radius('md'),
        cursor: locked ? 'default' : 'pointer',
        color: hint != null && !checked ? COLOR.textTertiary : undefined,
        '&:hover': {bgcolor: COLOR.bgSubtle},
      }}>
      <Checkbox
        size="small"
        checked={checked}
        disabled={locked}
        onChange={onToggle}
        sx={{p: 0}}
      />
      <Box
        component="span"
        sx={{
          flex: 1,
          minWidth: 0,
          overflow: 'hidden',
          textOverflow: 'ellipsis',
          whiteSpace: 'nowrap',
          fontSize: TEXT.xs,
        }}>
        {field.label}
      </Box>
      {hint != null && (
        <Box
          component="span"
          title={hint.title}
          sx={{
            flexShrink: 0,
            color: COLOR.textTertiary,
            fontSize: TEXT['2xs'],
          }}>
          {hint.text}
        </Box>
      )}
    </Box>
  );
}

const LINK_BUTTON_SX = {
  p: 0,
  border: 0,
  bgcolor: 'transparent',
  font: 'inherit',
  fontSize: TEXT.xs,
  color: COLOR.action,
  cursor: 'pointer',
  borderRadius: '4px',
  '&:hover': {textDecoration: 'underline'},
  '&:focus-visible': focusRing(),
} as const;

/**
 * The entity page's rail: find a row by name, moved or all, and which
 * columns the table shows. A column marked "all 0", "empty" or "same" holds
 * one value on the rows shown.
 */
export default function EntityFacets({
  nameLabel,
  search,
  onSearchChange,
  show,
  fields,
  shown,
  locked,
  auto,
  onToggle,
  onReset,
  profiles,
  rowCount,
}: {
  // What a row is called, for the search placeholder.
  nameLabel: string;
  search: string;
  onSearchChange: (search: string) => void;
  // Moved or all, when the run's moves are known.
  show: {
    moved: boolean;
    onChange: (moved: boolean) => void;
    label: string;
    help: string;
    movedCount: number;
    total: number | null;
  } | null;
  fields: readonly EntityField[];
  shown: ReadonlySet<string>;
  // Columns that are always shown (grouped by).
  locked: ReadonlySet<string>;
  auto: boolean;
  onToggle: (key: string) => void;
  onReset: () => void;
  profiles: ReadonlyMap<string, FieldProfile>;
  rowCount: number;
}) {
  const [columnQuery, setColumnQuery] = useState('');
  const [opened, setOpened] = useState<ReadonlySet<FieldKind>>(new Set());
  const needle = columnQuery.trim().toLowerCase();

  return (
    <Box
      component="aside"
      aria-label="Rows and columns"
      sx={{display: 'flex', flexDirection: 'column', gap: 2.5}}>
      <TextField
        size="small"
        placeholder={`Find ${nameLabel} by name…`}
        value={search}
        onChange={e => onSearchChange(e.target.value)}
        slotProps={{
          htmlInput: {'aria-label': `Find ${nameLabel} by name`},
          input: {
            startAdornment: (
              <InputAdornment position="start">
                <Search size={14} />
              </InputAdornment>
            ),
          },
        }}
      />
      {show != null && (
        <Box component="section">
          <Heading end={<HintIcon title={show.help} label="Help: Show" />}>
            Show
          </Heading>
          <ToggleButtonGroup
            size="small"
            exclusive
            fullWidth
            aria-label="Rows to show"
            value={show.moved ? 'moved' : 'all'}
            onChange={(_e, value: 'moved' | 'all' | null) => {
              if (value != null) {
                show.onChange(value === 'moved');
              }
            }}>
            <ToggleButton value="moved">
              {show.label} {formatCount(show.movedCount)}
            </ToggleButton>
            <ToggleButton value="all">
              All{show.total != null ? ` ${formatCount(show.total)}` : ''}
            </ToggleButton>
          </ToggleButtonGroup>
        </Box>
      )}
      <Box component="section">
        <Heading
          end={
            auto ? (
              <Box
                component="span"
                title={`Columns that tell the rows shown apart, as many as fit. Columns that hold one value${rowCount >= MIN_ROWS_TO_FOLD ? ' on every row' : ''} are left out.`}
                sx={{color: COLOR.textTertiary, fontSize: TEXT.xs}}>
                Automatic
              </Box>
            ) : (
              <Box
                component="button"
                type="button"
                onClick={onReset}
                sx={LINK_BUTTON_SX}>
                Reset
              </Box>
            )
          }>
          Columns
        </Heading>
        <TextField
          size="small"
          fullWidth
          placeholder="Find a column…"
          value={columnQuery}
          onChange={e => setColumnQuery(e.target.value)}
          slotProps={{htmlInput: {'aria-label': 'Find a column'}}}
          sx={{mb: 1.5}}
        />
        <Box sx={{display: 'flex', flexDirection: 'column', gap: 1.5}}>
          {KIND_SECTIONS.map(section => {
            const all = fields.filter(f => f.kind === section.kind);
            const matching =
              needle === ''
                ? all
                : all.filter(f => f.label.toLowerCase().includes(needle));
            if (matching.length === 0) {
              return null;
            }
            const isOpen = needle !== '' || opened.has(section.kind);
            const listed = isOpen
              ? matching
              : matching.slice(0, SECTION_PREVIEW);
            const shownCount = all.filter(f => shown.has(f.key)).length;
            return (
              <Box key={section.kind} component="section">
                <Box
                  sx={{
                    display: 'flex',
                    alignItems: 'baseline',
                    gap: 0.75,
                    px: 0.75,
                    mb: 0.25,
                    fontSize: TEXT.xs,
                  }}>
                  <Box component="span" sx={{fontWeight: 600}}>
                    {section.title}
                  </Box>
                  {section.sub != null && (
                    <Box component="span" sx={{color: COLOR.textTertiary}}>
                      {section.sub}
                    </Box>
                  )}
                  <Box
                    component="span"
                    sx={{ml: 'auto', color: COLOR.textTertiary, ...TABULAR}}>
                    {shownCount} of {all.length}
                  </Box>
                </Box>
                {listed.map(f => (
                  <ColumnOption
                    key={f.key}
                    field={f}
                    checked={shown.has(f.key)}
                    locked={locked.has(f.key)}
                    onToggle={() => onToggle(f.key)}
                    hint={hintFor(f, profiles.get(f.key), rowCount)}
                  />
                ))}
                {!isOpen && matching.length > SECTION_PREVIEW && (
                  <Button
                    size="small"
                    onClick={() =>
                      setOpened(prev => new Set(prev).add(section.kind))
                    }
                    sx={{ml: 0.25, mt: 0.25}}>
                    Show all {matching.length}
                  </Button>
                )}
              </Box>
            );
          })}
        </Box>
      </Box>
    </Box>
  );
}
