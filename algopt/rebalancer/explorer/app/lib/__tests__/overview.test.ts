import {describe, expect, it} from 'vitest';

import {
  changedObjectiveNames,
  moveSetDeltasFromRows,
  summarizeEvaluations,
  thinSeries,
  tupleMix,
  tupleStepTotals,
} from '@/lib/overview';
import {
  ColumnType,
  ExpressionType,
  type ColumnDescription,
  type ExpressionResult,
  type RowData,
} from '@/lib/rebalancer-explorer-types';

function objective(
  id: number,
  tupleIndex: number,
  value: number,
): ExpressionResult {
  return {
    id,
    type: ExpressionType.OBJECTIVE,
    name: `obj_${id}`,
    description: '',
    value,
    tupleIndex,
  };
}

function constraint(id: number, value: number): ExpressionResult {
  return {
    id,
    type: ExpressionType.CONSTRAINT,
    name: `con_${id}`,
    description: '',
    value,
    tupleIndex: 0,
  };
}

describe('summarizeEvaluations', () => {
  it('keeps the A and B values of every objective', () => {
    const s = summarizeEvaluations([objective(1, 0, 5)], [objective(1, 0, 2)]);
    expect(s.tuples[0].objectives[0]).toEqual({
      id: 1,
      name: 'obj_1',
      changed: true,
      srcValue: 5,
      dstValue: 2,
    });
  });

  it('sums each tuple and counts improved, worse and unchanged objectives', () => {
    const src = [objective(1, 0, 5), objective(2, 1, 3), objective(3, 1, 0)];
    const dst = [objective(1, 0, 5), objective(2, 1, 2), objective(3, 1, 4)];
    const s = summarizeEvaluations(src, dst);
    expect(s.tuples.map(t => [t.tupleIndex, t.srcTotal, t.dstTotal])).toEqual([
      [0, 5, 5],
      [1, 3, 6],
    ]);
    expect(s.tuples[1].delta).toBe(3);
    expect(s.tuples[1].changedCount).toBe(2);
    expect(s.objectives).toEqual({
      total: 3,
      improved: 1,
      worse: 1,
      unchanged: 1,
    });
  });

  it('decides at the first tuple whose total differs', () => {
    const src = [objective(1, 0, 1), objective(2, 1, 1), objective(3, 2, 1)];
    const dst = [objective(1, 0, 1), objective(2, 1, 0), objective(3, 2, 9)];
    expect(summarizeEvaluations(src, dst).decidingTupleIndex).toBe(1);
  });

  it('treats offsetting changes inside one tuple as a tie', () => {
    const src = [objective(1, 0, 1), objective(2, 0, 2), objective(3, 1, 1)];
    const dst = [objective(1, 0, 2), objective(2, 0, 1), objective(3, 1, 0)];
    const s = summarizeEvaluations(src, dst);
    expect(s.tuples[0].delta).toBe(0);
    expect(s.tuples[0].changedCount).toBe(2);
    expect(s.decidingTupleIndex).toBe(1);
  });

  it('gives an unchanged tuple a delta of exactly 0 despite rounding', () => {
    const src = [objective(1, 0, 0.1), objective(2, 0, 0.2)];
    const dst = [objective(2, 0, 0.2), objective(1, 0, 0.1)];
    const s = summarizeEvaluations(src, dst);
    expect(s.tuples[0].delta).toBe(0);
    expect(s.decidingTupleIndex).toBeNull();
  });

  it('counts nonzero constraints on each side and skips unmatched ids', () => {
    const src = [constraint(1, 0), constraint(2, 3), objective(3, 0, 1)];
    const dst = [constraint(1, 2), constraint(2, 0)];
    const s = summarizeEvaluations(src, dst);
    expect(s.constraints).toEqual({total: 2, nonzeroSrc: 1, nonzeroDst: 1});
    expect(s.tuples).toEqual([]);
  });
});

describe('changedObjectiveNames', () => {
  it('lists changed objectives by tuple index, skipping constraints', () => {
    const src = [
      objective(1, 2, 1),
      objective(2, 0, 1),
      constraint(3, 0),
      objective(4, 1, 1),
      objective(5, 1, 7),
    ];
    const dst = [
      objective(1, 2, 2),
      objective(2, 0, 0),
      constraint(3, 1),
      objective(4, 1, 1),
      objective(5, 1, 6),
    ];
    expect(changedObjectiveNames(src, dst, 10)).toEqual([
      'obj_2',
      'obj_5',
      'obj_1',
    ]);
    expect(changedObjectiveNames(src, dst, 2)).toEqual(['obj_2', 'obj_5']);
  });
});

describe('tupleMix', () => {
  it('splits objectives into improved, regressed, unchanged non-zero and zero', () => {
    const src = [
      objective(1, 0, 5),
      objective(2, 0, 1),
      objective(3, 0, 4),
      objective(4, 0, 0),
      objective(5, 0, 3),
    ];
    const dst = [
      objective(1, 0, 2),
      objective(2, 0, 3),
      objective(3, 0, 4),
      objective(4, 0, 0),
      objective(5, 0, 0),
    ];
    const [t] = summarizeEvaluations(src, dst).tuples;
    expect(tupleMix(t)).toEqual({
      improved: 2,
      regressed: 1,
      unchangedNonzero: 1,
      zero: 1,
    });
  });
});

function column(name: string): ColumnDescription {
  return {name, type: ColumnType.DOUBLE, primaryKey: false, description: ''};
}

function row(...values: Array<number | string>): RowData {
  return {
    cells: values.map(v =>
      typeof v === 'number' ? {doubleValue: v} : {stringValue: v},
    ),
  };
}

describe('moveSetDeltasFromRows', () => {
  const columns = [
    column('MoveSet #'),
    column('obj_1 (Δ)'),
    column('obj_2 (Δ)'),
    column('Object'),
  ];

  it('keeps one entry per move set, in move set order, in the order of the names', () => {
    const rows = [
      row(2, -1, 0.5, 'b'),
      row(1, 3, -2, 'a1'),
      row(1, 3, -2, 'a2'),
    ];
    expect(moveSetDeltasFromRows(columns, rows, ['obj_2', 'obj_1'])).toEqual({
      names: ['obj_2', 'obj_1'],
      sets: [
        [-2, 3],
        [0.5, -1],
      ],
    });
  });

  it('fails when a name has no change column', () => {
    expect(() => moveSetDeltasFromRows(columns, [], ['obj_3'])).toThrow(
      'Move sets have no change column for obj_3',
    );
  });
});

describe('tupleStepTotals', () => {
  it('starts at the Initial total and adds each move set of the tuple', () => {
    const src = [objective(1, 0, 10), objective(2, 0, 5), objective(3, 1, 0)];
    const dst = [objective(1, 0, 7), objective(2, 0, 6), objective(3, 1, 0)];
    const summary = summarizeEvaluations(src, dst);
    const totals = tupleStepTotals(summary, {
      names: ['obj_1', 'obj_2'],
      sets: [
        [-2, 0],
        [-1, 1],
      ],
    });
    expect(Array.from(totals.entries())).toEqual([[0, [15, 13, 13]]]);
  });
});

describe('thinSeries', () => {
  it('keeps short series and thins long ones, keeping both ends', () => {
    expect(thinSeries([1, 2, 3], 5)).toEqual([1, 2, 3]);
    const long = Array.from({length: 500}, (_, i) => i);
    const thin = thinSeries(long, 50);
    expect(thin).toHaveLength(50);
    expect([thin[0], thin[49]]).toEqual([0, 499]);
  });
});
