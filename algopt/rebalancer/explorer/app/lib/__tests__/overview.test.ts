import {describe, expect, it} from 'vitest';

import {
  changedObjectiveNames,
  profileTrace,
  stillAboveZero,
  summarizeEvaluations,
} from '@/lib/overview';
import {
  ExpressionType,
  type ExpressionResult,
  type LocalSearchProfile,
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

describe('stillAboveZero', () => {
  it('lists unchanged objectives still above 0 at B, in tuple order', () => {
    const src = [
      objective(1, 1, 4),
      objective(2, 0, 0),
      objective(3, 0, 2),
      objective(4, 1, 5),
      objective(5, 2, -1),
    ];
    const dst = [
      objective(1, 1, 4),
      objective(2, 0, 0),
      objective(3, 0, 2),
      objective(4, 1, 3),
      objective(5, 2, -1),
    ];
    const rows = stillAboveZero(summarizeEvaluations(src, dst));
    expect(rows.map(r => [r.id, r.tupleIndex, r.dstValue])).toEqual([
      [3, 0, 2],
      [1, 1, 4],
    ]);
  });

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
});

describe('profileTrace', () => {
  const profile: LocalSearchProfile = {
    moveTypeNames: ['SINGLE'],
    moveTypeEvents: [
      {moveTypeIndex: 0, duration: 1, initialValue: 10, finalValue: 8},
      {moveTypeIndex: 0, duration: 2, initialValue: 8, finalValue: 7},
    ],
  };

  it('starts at the first initial value and accumulates time', () => {
    expect(profileTrace(profile)).toEqual([
      {t: 0, v: 10},
      {t: 1, v: 8},
      {t: 3, v: 7},
    ]);
  });

  it('thins long traces but keeps both ends', () => {
    const long: LocalSearchProfile = {
      moveTypeNames: ['SINGLE'],
      moveTypeEvents: Array.from({length: 500}, (_, i) => ({
        moveTypeIndex: 0,
        duration: 1,
        initialValue: 500 - i,
        finalValue: 499 - i,
      })),
    };
    const trace = profileTrace(long, 50);
    expect(trace).toHaveLength(50);
    expect(trace[0]).toEqual({t: 0, v: 500});
    expect(trace[49]).toEqual({t: 500, v: 0});
  });

  it('is empty for a profile with no events', () => {
    expect(profileTrace({moveTypeNames: [], moveTypeEvents: []})).toEqual([]);
  });
});
