'use client';

import {Box, ButtonBase, Link, Tooltip} from '@mui/material';
import {X} from 'lucide-react';

import {COMPARATOR_TO_OPERATOR} from '@/app/components/EntityFilter.transform';
import type {Filter, FilterRule} from '@/lib/rebalancer-explorer-types';
import {COLOR, radius, TEXT, WEIGHT} from '@/lib/ui-tokens';

function ruleLabel(rule: FilterRule): string {
  if ('regex' in rule) {
    return `${rule.regex.column} ~ ${rule.regex.regex}`;
  }
  if ('numeric' in rule) {
    const op = COMPARATOR_TO_OPERATOR[rule.numeric.comparator] ?? '?';
    return `${rule.numeric.column} ${op} ${rule.numeric.doubleValue}`;
  }
  if ('stringAny' in rule) {
    return `${rule.stringAny.column}: ${rule.stringAny.values.join(', ')}`;
  }
  return `${rule.stringNe.column} ≠ ${rule.stringNe.value}`;
}

/**
 * The active filters as removable chips above the results, with "Clear all".
 * The filter editor above stays where filters are added and changed.
 */
export default function FilterChips({
  filters,
  onChange,
}: {
  filters: Filter[];
  onChange: (filters: Filter[]) => void;
}) {
  const chips = filters.flatMap((f, fi) =>
    f.rules.map((rule, ri) => ({fi, ri, label: ruleLabel(rule)})),
  );
  if (chips.length === 0) {
    return null;
  }
  const remove = (fi: number, ri: number) =>
    onChange(
      filters
        .map((f, i) =>
          i === fi ? {...f, rules: f.rules.filter((_, j) => j !== ri)} : f,
        )
        .filter(f => f.rules.length > 0),
    );
  return (
    <Box
      sx={{
        display: 'flex',
        alignItems: 'center',
        flexWrap: 'wrap',
        gap: 0.75,
        color: COLOR.textTertiary,
        fontSize: TEXT.xs,
      }}>
      <span>Filtered by</span>
      {chips.map(c => (
        <Box
          key={`${c.fi}:${c.ri}`}
          component="span"
          sx={{
            display: 'inline-flex',
            alignItems: 'center',
            gap: 0.5,
            height: 24,
            maxWidth: 320,
            pl: 1.25,
            pr: 0,
            borderRadius: radius('full'),
            bgcolor: COLOR.actionSubtle,
            border: `1px solid ${COLOR.actionSubtleBorder}`,
            color: COLOR.actionHover,
            fontWeight: WEIGHT.semibold,
          }}>
          <Box
            component="span"
            title={c.label}
            sx={{
              overflow: 'hidden',
              textOverflow: 'ellipsis',
              whiteSpace: 'nowrap',
              minWidth: 0,
            }}>
            {c.label}
          </Box>
          <Tooltip title="Remove this filter">
            <ButtonBase
              aria-label={`Remove filter ${c.label}`}
              onClick={() => remove(c.fi, c.ri)}
              sx={{
                width: 24,
                height: 24,
                borderRadius: '50%',
                flexShrink: 0,
                color: 'inherit',
                '&:hover': {bgcolor: COLOR.actionSubtleBorder},
              }}>
              <X size={12} strokeWidth={2.5} />
            </ButtonBase>
          </Tooltip>
        </Box>
      ))}
      <Link
        component="button"
        type="button"
        onClick={() => onChange([])}
        sx={{fontSize: TEXT.xs, fontWeight: WEIGHT.semibold, ml: 0.5}}>
        Clear all
      </Link>
    </Box>
  );
}
