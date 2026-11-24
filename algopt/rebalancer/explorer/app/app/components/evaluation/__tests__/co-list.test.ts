import {describe, expect, it} from 'vitest';

import {
  buildRows,
  facetCounts,
  groupRows,
  NO_FACETS,
  rowStatus,
  sortRows,
  type COQuery,
} from '@/app/components/evaluation/co-list';
import {
  ExpressionType,
  type ExpressionResult,
} from '@/lib/rebalancer-explorer-types';

function expr(
  id: number,
  type: ExpressionType,
  value: number,
  tupleIndex = 0,
): ExpressionResult {
  return {
    id,
    type,
    name: `e_${id}`,
    description: id === 3 ? 'spread across hosts' : '',
    value,
    tupleIndex,
  };
}

const O = ExpressionType.OBJECTIVE;
const C = ExpressionType.CONSTRAINT;

// e_1 constraint fixed, e_2 constraint satisfied, e_3 tuple 0 improved,
// e_4 tuple 1 regressed, e_5 tuple 1 unchanged.
const rows = buildRows(
  [
    expr(1, C, 2),
    expr(2, C, 0),
    expr(3, O, 5),
    expr(4, O, 1, 1),
    expr(5, O, 7, 1),
  ],
  [
    expr(1, C, 0),
    expr(2, C, 0),
    expr(3, O, 4),
    expr(4, O, 3, 1),
    expr(5, O, 7, 1),
  ],
);

const query: COQuery = {
  search: '',
  facets: NO_FACETS,
  changedOnly: true,
  penaltyShown: false,
  matches: null,
};

describe('rowStatus', () => {
  it.each([
    ['objective', 5, 4, 'improved'],
    ['objective', 1, 3, 'regressed'],
    ['objective', 2, 2, 'unchanged'],
    ['constraint', 2, 3, 'violated'],
    ['constraint', 0, 3, 'newlyViolated'],
    ['constraint', 2, 0, 'fixed'],
    ['constraint', 0, 0, 'satisfied'],
  ] as const)('%s %s → %s is %s', (kind, a, b, status) => {
    expect(rowStatus(kind, a, b)).toBe(status);
  });
});

describe('buildRows', () => {
  it('keeps kind, tuple index and evaluation order', () => {
    expect(rows.map(r => [r.name, r.kind, r.tupleIndex, r.order])).toEqual([
      ['e_1', 'constraint', null, 0],
      ['e_2', 'constraint', null, 1],
      ['e_3', 'objective', 0, 2],
      ['e_4', 'objective', 1, 3],
      ['e_5', 'objective', 1, 4],
    ]);
  });
});

describe('facetCounts', () => {
  it('counts each facet against every filter but its own', () => {
    const counts = facetCounts(rows, {
      ...query,
      facets: {kinds: ['constraint'], statuses: [], tuples: []},
    });
    // Changed rows only: e_1, e_3, e_4.
    expect(Object.fromEntries(counts.kinds)).toEqual({
      constraint: 1,
      objective: 2,
    });
    expect(Object.fromEntries(counts.statuses)).toEqual({fixed: 1});
    expect(Object.fromEntries(counts.tuples)).toEqual({});
  });
});

describe('groupRows', () => {
  it('shows changed rows per group and counts the unchanged ones it hides', () => {
    const groups = groupRows(rows, query, 'priority', 'asc', new Set());
    expect(
      groups.map(g => [g.key, g.shown.map(r => r.name), g.hiddenUnchanged]),
    ).toEqual([
      ['constraints', ['e_1'], 1],
      ['t0', ['e_3'], 0],
      ['t1', ['e_4'], 1],
    ]);
  });

  it('shows a group in full when asked, and leaves out groups the facets exclude', () => {
    const groups = groupRows(
      rows,
      {...query, facets: {kinds: ['objective'], statuses: [], tuples: [1]}},
      'priority',
      'asc',
      new Set(['t1']),
    );
    expect(groups.map(g => [g.key, g.shown.map(r => r.name)])).toEqual([
      ['t1', ['e_4', 'e_5']],
    ]);
  });

  it('searches names and descriptions', () => {
    const groups = groupRows(
      rows,
      {...query, search: 'SPREAD'},
      'priority',
      'asc',
      new Set(),
    );
    expect(groups.flatMap(g => g.shown.map(r => r.name))).toEqual(['e_3']);
  });
});

describe('sortRows', () => {
  it('sorts by the key and keeps evaluation order on ties', () => {
    expect(sortRows(rows, 'absChange', 'desc').map(r => r.name)).toEqual([
      'e_1',
      'e_4',
      'e_3',
      'e_2',
      'e_5',
    ]);
    expect(sortRows(rows, 'change', 'asc').map(r => r.name)).toEqual([
      'e_1',
      'e_3',
      'e_2',
      'e_5',
      'e_4',
    ]);
  });
});
