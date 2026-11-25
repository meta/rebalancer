import {describe, expect, it} from 'vitest';

import {
  buildFields,
  fieldValues,
  fitToWidth,
  inDisplayOrder,
  largestChange,
  normalizeShown,
  profileFields,
  rankFields,
  searchRule,
} from '@/lib/entity-fields';
import {
  ColumnType,
  type ColumnDescription,
  type RowData,
} from '@/lib/rebalancer-explorer-types';

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
  col('cpu', ColumnType.DIMENSION),
  col('job', ColumnType.PARTITION),
  col('src.host', ColumnType.ASSIGNMENT),
  col('dst.host', ColumnType.ASSIGNMENT),
  col('src.rack', ColumnType.ASSIGNMENT),
  col('dst.rack', ColumnType.ASSIGNMENT),
];

const CONTAINER_COLUMNS = [
  col('host', ColumnType.ENTITY_NAME, true),
  col('rack', ColumnType.SCOPE),
  col('cpu.finalUtil', ColumnType.UTILIZATION),
  col('cpu.initUtil', ColumnType.UTILIZATION),
  col('mem.finalUtil', ColumnType.UTILIZATION),
  col('mem.initUtil', ColumnType.UTILIZATION),
];

describe('buildFields', () => {
  it('merges initial/final pairs into one field at the first one', () => {
    const fields = buildFields(OBJECT_COLUMNS);
    expect(fields.map(f => [f.key, f.kind, f.a, f.b])).toEqual([
      ['task', 'name', null, 0],
      ['cpu', 'dimension', null, 1],
      ['job', 'partition', null, 2],
      ['host', 'assignment', 3, 4],
      ['rack', 'assignment', 5, 6],
    ]);
    expect(fields[3].sortColumn).toBe('dst.host');
  });

  it('pairs utilization whichever comes first', () => {
    const fields = buildFields(CONTAINER_COLUMNS);
    const cpu = fields.find(f => f.key === 'cpu')!;
    expect([cpu.a, cpu.b, cpu.sortColumn]).toEqual([3, 2, 'cpu.finalUtil']);
    expect(fields).toHaveLength(4);
  });

  it('keeps a half pair as its own column', () => {
    const fields = buildFields([col('src.host', ColumnType.ASSIGNMENT)]);
    expect(fields.map(f => [f.key, f.a])).toEqual([['src.host', null]]);
  });

  it('labels the group count', () => {
    const fields = buildFields([
      col('job', ColumnType.PARTITION),
      col('Row_Count', ColumnType.INTEGER),
    ]);
    expect(fields[1]).toMatchObject({kind: 'count', label: 'Rows'});
  });
});

describe('profileFields', () => {
  const fields = buildFields(OBJECT_COLUMNS);
  const rows = [
    row('t1', 0, 'j1', 'h1', 'h2', 'r1', 'r1'),
    row('t2', 0, 'j2', 'h1', 'h1', 'r1', 'r1'),
    row('t3', 0, 'j1', 'h3', 'h3', 'r1', 'r1'),
  ];
  const p = profileFields(fields, rows);

  it('finds blank, constant and changed fields', () => {
    expect(p.get('cpu')).toMatchObject({blank: true, filled: 0});
    expect(p.get('rack')).toMatchObject({constant: true, value: 'r1'});
    expect(p.get('host')).toMatchObject({constant: false, changed: 1});
    expect(p.get('job')).toMatchObject({distinct: 2, filled: 3});
  });

  it('ranks changed fields first and leaves out one-value fields', () => {
    expect(rankFields(fields, p, rows.length)).toEqual(['host', 'job']);
  });
});

describe('rankFields', () => {
  const columns = [
    col('name', ColumnType.ENTITY_NAME, true),
    col('s1', ColumnType.SCOPE),
    col('s2', ColumnType.SCOPE),
    col('u.initUtil', ColumnType.UTILIZATION),
    col('u.finalUtil', ColumnType.UTILIZATION),
    col('v.initUtil', ColumnType.UTILIZATION),
    col('v.finalUtil', ColumnType.UTILIZATION),
    col('id', ColumnType.PARTITION),
  ];
  const fields = buildFields(columns);
  const rows = [
    row('a', 'x', 'p', 1, 1, 0, 0, 'i1'),
    row('b', 'y', 'p', 2, 2, 5, 5, 'i2'),
    row('c', 'x', 'q', 3, 3, 0, 0, 'i3'),
  ];
  const p = profileFields(fields, rows);

  it('takes a column of each kind in turn, fullest first, ids last', () => {
    expect(rankFields(fields, p, rows.length)).toEqual([
      's1',
      'u',
      's2',
      'v',
      'id',
    ]);
  });

  it('puts excluded fields out and demoted ones last', () => {
    expect(
      rankFields(fields, p, rows.length, new Set(['u']), new Set(['s1'])),
    ).toEqual(['s2', 'v', 's1', 'id']);
  });

  it('keeps one-value fields when there are too few rows to tell', () => {
    const one = profileFields(fields, rows.slice(0, 1));
    expect(rankFields(fields, one, 1)).toContain('s2');
  });
});

describe('fitToWidth', () => {
  it('stops at the budget but keeps the minimum', () => {
    const w = () => 100;
    expect(fitToWidth(['a', 'b', 'c'], w, 250)).toEqual(['a', 'b']);
    expect(fitToWidth(['a', 'b', 'c'], w, 50)).toEqual(['a', 'b']);
    expect(fitToWidth(['a', 'b', 'c'], w, 50, 1)).toEqual(['a']);
  });
});

describe('inDisplayOrder', () => {
  it('groups by kind, each kind in column order', () => {
    const fields = buildFields(OBJECT_COLUMNS);
    expect(
      inDisplayOrder(fields, new Set(['cpu', 'rack', 'job', 'host'])).map(
        f => f.key,
      ),
    ).toEqual(['host', 'rack', 'job', 'cpu']);
  });
});

describe('largestChange', () => {
  const fields = buildFields(CONTAINER_COLUMNS);

  it('picks the largest change relative to its size', () => {
    // cpu 10 → 12 (+20%), mem 1 → 2 (+100%).
    const top = largestChange(fields, row('h', 'r', 12, 10, 2, 1));
    expect(top).toMatchObject({a: 1, b: 2, changed: 2});
    expect(top?.field.key).toBe('mem');
  });

  it('is null when nothing changed', () => {
    expect(largestChange(fields, row('h', 'r', 3, 3, 0, 0))).toBeNull();
  });
});

describe('fieldValues', () => {
  it('reads blanks and N/A as no value', () => {
    const fields = buildFields([col('x', ColumnType.STRING)]);
    expect(fieldValues(fields[0], row('N/A')).b).toBeNull();
    expect(fieldValues(fields[0], row(null)).b).toBeNull();
  });
});

describe('normalizeShown', () => {
  it('maps backend column names to their pair, once', () => {
    const fields = buildFields(OBJECT_COLUMNS);
    expect(
      normalizeShown(['src.host', 'dst.host', 'job', 'gone'], fields),
    ).toEqual(['host', 'job']);
  });
});

describe('searchRule', () => {
  it('matches the text anywhere, ignoring case, with regex characters escaped', () => {
    expect(searchRule('task', 'a.b(1)')).toEqual({
      regex: {column: 'task', regex: '(?i)a\\.b\\(1\\)'},
    });
  });
});
