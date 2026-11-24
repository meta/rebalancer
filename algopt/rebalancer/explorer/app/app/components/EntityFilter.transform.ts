import type {FullOperator, RuleGroupType, RuleType} from 'react-querybuilder';

import {isNumericColumn} from '@/lib/format';
import type {
  ColumnDescription,
  Filter,
  FilterRule,
} from '@/lib/rebalancer-explorer-types';
import {ColumnType, Comparator} from '@/lib/rebalancer-explorer-types';

export const STRING_OPERATORS: FullOperator[] = [
  {name: 'in', value: 'in', label: 'is any of'},
  {name: 'regex', value: 'regex', label: 'matches regex'},
  {name: 'notEqual', value: 'notEqual', label: 'is not'},
];

export const NUMERIC_OPERATORS: FullOperator[] = [
  {name: '=', value: '=', label: '='},
  {name: '!=', value: '!=', label: '!='},
  {name: '<', value: '<', label: '<'},
  {name: '<=', value: '<=', label: '<='},
  {name: '>', value: '>', label: '>'},
  {name: '>=', value: '>=', label: '>='},
];

export const OPERATOR_TO_COMPARATOR: Record<string, Comparator> = {
  '=': Comparator.EQ,
  '!=': Comparator.NE,
  '<': Comparator.LT,
  '<=': Comparator.LE,
  '>': Comparator.GT,
  '>=': Comparator.GE,
};

export const COMPARATOR_TO_OPERATOR: Record<Comparator, string> = {
  [Comparator.EQ]: '=',
  [Comparator.NE]: '!=',
  [Comparator.LT]: '<',
  [Comparator.LE]: '<=',
  [Comparator.GT]: '>',
  [Comparator.GE]: '>=',
};

// --- Conversion: Filter[] <-> RuleGroupType ---

export function filtersToQuery(filters: Filter[]): RuleGroupType {
  const rules: RuleType[] = [];
  for (const filter of filters) {
    for (const rule of filter.rules) {
      if ('regex' in rule) {
        rules.push({
          id: crypto.randomUUID(),
          field: rule.regex.column,
          operator: 'regex',
          value: rule.regex.regex,
        });
      } else if ('numeric' in rule) {
        rules.push({
          id: crypto.randomUUID(),
          field: rule.numeric.column,
          operator: COMPARATOR_TO_OPERATOR[rule.numeric.comparator],
          value: String(rule.numeric.doubleValue),
        });
      } else if ('stringAny' in rule) {
        rules.push({
          id: crypto.randomUUID(),
          field: rule.stringAny.column,
          operator: 'in',
          value: rule.stringAny.values.join(','),
        });
      } else if ('stringNe' in rule) {
        rules.push({
          id: crypto.randomUUID(),
          field: rule.stringNe.column,
          operator: 'notEqual',
          value: rule.stringNe.value,
        });
      }
    }
  }
  return {
    id: 'root',
    combinator: 'and',
    rules,
  };
}

export function queryToFilters(
  query: RuleGroupType,
  columnDescriptions: ColumnDescription[],
): Filter[] {
  const columnTypeMap = new Map(columnDescriptions.map(c => [c.name, c.type]));
  const filterRules: FilterRule[] = [];
  for (const rule of query.rules) {
    if ('rules' in rule) {
      // Nested groups — flatten (backend only supports AND)
      const nested = queryToFilters(rule as RuleGroupType, columnDescriptions);
      for (const f of nested) {
        filterRules.push(...f.rules);
      }
      continue;
    }
    const r = rule as RuleType;
    if (r.field === '' || r.field === '~') continue;

    const colType = columnTypeMap.get(r.field);
    const numeric = colType != null && isNumericColumn(colType);

    switch (r.operator) {
      case 'regex':
        // Skip string filters for numeric columns
        if (!numeric && r.value !== '') {
          filterRules.push({regex: {column: r.field, regex: String(r.value)}});
        }
        break;
      case 'in': {
        if (!numeric) {
          const vals =
            typeof r.value === 'string'
              ? r.value
                  .split(',')
                  .map(v => v.trim())
                  .filter(Boolean)
              : Array.isArray(r.value)
                ? (r.value as string[])
                : [];
          if (vals.length > 0) {
            filterRules.push({stringAny: {column: r.field, values: vals}});
          }
        }
        break;
      }
      case 'notEqual':
        if (!numeric && r.value !== '') {
          filterRules.push({
            stringNe: {column: r.field, value: String(r.value)},
          });
        }
        break;
      default: {
        // Numeric operators — skip for non-numeric columns
        const comparator = OPERATOR_TO_COMPARATOR[r.operator];
        if (comparator != null && numeric) {
          const val = parseFloat(String(r.value));
          if (!isNaN(val)) {
            filterRules.push({
              numeric: {column: r.field, comparator, doubleValue: val},
            });
          }
        }
        break;
      }
    }
  }
  if (filterRules.length === 0) return [];
  return [{rules: filterRules}];
}
