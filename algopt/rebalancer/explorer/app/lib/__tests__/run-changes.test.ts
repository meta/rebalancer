import {describe, expect, it} from 'vitest';

import {
  ColumnType,
  type ColumnDescription,
  type RowData,
} from '@/lib/rebalancer-explorer-types';
import {
  flows,
  itemMoves,
  moveSetsFromTable,
  movedInGroup,
  movedObjectsFromRows,
  movedPerGroup,
  movedThrough,
  objectiveChanges,
  setsOf,
  spreadScopeOf,
} from '@/lib/run-changes';

function col(
  name: string,
  type: ColumnType,
  primaryKey = false,
): ColumnDescription {
  return {name, type, primaryKey, description: ''};
}

function row(...cells: Array<string | number | null>): RowData {
  return {
    cells: cells.map(c =>
      c == null
        ? {}
        : typeof c === 'number'
          ? {doubleValue: c}
          : {stringValue: c},
    ),
  };
}

const OBJECT_COLUMNS = [
  col('task', ColumnType.ENTITY_NAME, true),
  col('job', ColumnType.PARTITION),
  col('src.host', ColumnType.ASSIGNMENT),
  col('dst.host', ColumnType.ASSIGNMENT),
  col('src.rack', ColumnType.ASSIGNMENT),
  col('dst.rack', ColumnType.ASSIGNMENT),
];

// t1 and t2 leave rack r1; t3 moves between hosts of rack r2.
const moved = movedObjectsFromRows(
  OBJECT_COLUMNS,
  [
    row('t1', 'j1', 'h1', 'stale', 'r1', 'r2'),
    row('t2', 'j1', 'h1', 'h3', 'r1', 'r3'),
    row('t3', 'j2', 'h4', 'h5', 'r2', 'r2'),
  ],
  {t1: 'h2', t2: 'h3', t3: 'h5'},
  'host',
  ['job'],
);

describe('movedObjectsFromRows', () => {
  it('reads every scope and group, with the final container from the moves', () => {
    const t1 = moved.get('t1');
    expect(t1).toMatchObject({src: 'h1', dst: 'h2'});
    expect(t1?.where.get('host')).toEqual({src: 'h1', dst: 'h2'});
    expect(t1?.where.get('rack')).toEqual({src: 'r1', dst: 'r2'});
    expect(t1?.groups.get('job')).toBe('j1');
  });

  it('keeps a moved object without a row, with no initial container', () => {
    const m = movedObjectsFromRows(OBJECT_COLUMNS, [], {t9: 'h1'}, 'host', []);
    expect(m.get('t9')).toMatchObject({src: '', dst: 'h1'});
  });
});

describe('itemMoves and movedThrough', () => {
  it('counts objects that cross an item, not moves inside it', () => {
    const racks = itemMoves(moved, 'rack');
    expect(racks.get('r1')).toEqual({out: 2, in: 0});
    expect(racks.get('r2')).toEqual({out: 0, in: 1});
    expect(movedThrough(moved, 'rack', 'r2')).toEqual({out: [], in: ['t1']});
    expect(itemMoves(moved, 'host').get('h1')).toEqual({out: 2, in: 0});
  });

  it('groups flows by initial and final item, most first', () => {
    expect(flows(moved, 'host')).toEqual([
      {src: 'h1', dst: 'h2', count: 1},
      {src: 'h1', dst: 'h3', count: 1},
      {src: 'h4', dst: 'h5', count: 1},
    ]);
  });

  it('finds the moved objects of a group', () => {
    expect(movedInGroup(moved, 'job', 'j1')).toEqual(['t1', 't2']);
    expect(Object.fromEntries(movedPerGroup(moved, 'job'))).toEqual({
      j1: 2,
      j2: 1,
    });
  });

  it('keeps the flows of some objects only', () => {
    expect(flows(moved, 'rack', new Set(['t1', 't3']))).toEqual([
      {src: 'r1', dst: 'r2', count: 1},
    ]);
  });

  it('picks the coarsest scope the objects moved across', () => {
    const scopes = ['host', 'rack'];
    expect(spreadScopeOf(moved, ['t1', 't2'], scopes, 'host')).toBe('rack');
    // t3 moved inside rack r2: only its container changed.
    expect(spreadScopeOf(moved, ['t3'], scopes, 'host')).toBe('host');
    expect(spreadScopeOf(moved, [], scopes, 'host')).toBe('host');
  });

  it('prefers fewer items, and skips moves into or out of no item', () => {
    const m = movedObjectsFromRows(
      [
        col('o', ColumnType.ENTITY_NAME, true),
        col('src.pool', ColumnType.ASSIGNMENT),
        col('dst.pool', ColumnType.ASSIGNMENT),
        col('src.zone', ColumnType.ASSIGNMENT),
        col('dst.zone', ColumnType.ASSIGNMENT),
        col('src.rack', ColumnType.ASSIGNMENT),
        col('dst.rack', ColumnType.ASSIGNMENT),
      ],
      [
        row('a', 'p1', '', 'z1', 'z2', 'r1', 'r2'),
        row('b', '', 'p1', 'z2', 'z1', 'r3', 'r4'),
      ],
      {a: 'h2', b: 'h1'},
      'host',
      [],
    );
    expect(
      spreadScopeOf(m, ['a', 'b'], ['host', 'pool', 'rack', 'zone'], 'host'),
    ).toBe('zone');
  });
});

describe('move sets', () => {
  const columns = [
    col('MoveSet #', ColumnType.IDENTIFIER),
    col('Object', ColumnType.STRING),
    col('Source Container', ColumnType.STRING),
    col('Destination Container', ColumnType.STRING),
    col('cost (Δ)', ColumnType.DOUBLE),
    col('spread (Δ)', ColumnType.DOUBLE),
  ];
  // Set 2 is a swap: its change repeats on both rows.
  const sets = moveSetsFromTable(
    columns,
    [
      row(1, 't1', 'h1', 'h2', -3, 0),
      row(2, 't2', 'h1', 'h3', 1, 0.1),
      row(2, 't4', 'h3', 'h1', 1, 0.1),
      row(3, 't3', 'h4', 'h5', 0.1, -0.1),
    ],
    ['cost', 'spread', 'missing'],
  );

  it('reads rows and one change per set', () => {
    expect(sets.rows).toHaveLength(4);
    expect(sets.objectiveNames).toEqual(['cost', 'spread']);
    expect(sets.deltas.get(2)).toEqual([1, 0.1]);
  });

  it('sums each set once over the objects it moved', () => {
    expect(setsOf(sets, ['t2', 't4', 't1'])).toEqual([1, 2]);
    expect(objectiveChanges(sets, [1, 2])).toEqual([
      {name: 'cost', delta: -2},
      {name: 'spread', delta: 0.1},
    ]);
  });

  it('reads runs of single moves, numbered "Move #"', () => {
    const single = moveSetsFromTable(
      [col('Move #', ColumnType.IDENTIFIER), ...columns.slice(1)],
      [row(4, 't1', 'h1', 'h2', -1, 0)],
      ['cost'],
    );
    expect(single.rows).toEqual([{set: 4, object: 't1', src: 'h1', dst: 'h2'}]);
  });

  it('drops changes that cancel to rounding noise', () => {
    const noisy = moveSetsFromTable(
      columns,
      [row(1, 'a', 'x', 'y', 0.1, 0), row(2, 'b', 'y', 'x', -0.1, 0)],
      ['cost'],
    );
    noisy.deltas.set(3, [0.2 - 0.1 - 0.1]);
    expect(objectiveChanges(noisy, [1, 2, 3])).toEqual([]);
  });
});
