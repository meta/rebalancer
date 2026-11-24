/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import {describe, expect, it} from 'vitest';

import {
  calculatePercentageChange,
  formatPercentChange,
  formatIndexRanges,
  joinEvaluationRows,
} from '@/app/components/evaluation/evaluation-rows';
import type {ExpressionResult} from '@/lib/rebalancer-explorer-types';

function expr(
  id: number,
  value: number,
  penaltyValue?: number,
): ExpressionResult {
  return {
    id,
    type: 0,
    name: `goal_${id}`,
    description: `description ${id}`,
    value,
    tupleIndex: 0,
    penaltyValue,
  } as unknown as ExpressionResult;
}

describe('formatPercentChange', () => {
  it.each([
    [3, 3, '0.00%'],
    [0, 264, 'from 0'],
    [0, -4, 'from 0'],
    [3.113754638, 3.113753959, '-<0.01%'],
    [100000, 100001, '<0.01%'],
    [10, 5, '-50.00%'],
    [-4, -2, '50.00%'],
  ])('labels %p -> %p as %p', (src, dst, label) => {
    expect(
      formatPercentChange(src, dst, calculatePercentageChange(src, dst)),
    ).toBe(label);
  });
});

describe('calculatePercentageChange', () => {
  it.each([
    [0, 0, 0],
    [0, 5, 100],
    [10, 15, 50],
    [-10, -15, -50],
    [4, 3, -25],
  ])('%s -> %s is %s%%', (src, dst, pct) => {
    expect(calculatePercentageChange(src, dst)).toBe(pct);
  });
});

describe('joinEvaluationRows', () => {
  it('joins by id in A order and drops ids missing from B', () => {
    const rows = joinEvaluationRows(
      [expr(2, 1), expr(1, 10, 1), expr(3, 0)],
      [expr(1, 15, 3), expr(2, 1)],
    );
    expect(rows.map(r => r.expressionId)).toEqual([2, 1]);
    expect(rows[1]).toMatchObject({
      name: 'goal_1',
      srcValue: 10,
      dstValue: 15,
      delta: 5,
      percentage: 50,
      srcPenalty: 1,
      dstPenalty: 3,
      penaltyDelta: 2,
    });
    expect(rows[0]).toMatchObject({
      delta: 0,
      srcPenalty: null,
      dstPenalty: null,
      penaltyDelta: 0,
    });
  });
});

describe('formatIndexRanges', () => {
  it.each([
    [[0], '0'],
    [[0, 1], '0, 1'],
    [[4, 0, 2, 5, 6], '0, 2, 4–6'],
    [[1, 2, 3, 7], '1–3, 7'],
  ])('%j -> %s', (indices, text) => {
    expect(formatIndexRanges(indices)).toBe(text);
  });
});
