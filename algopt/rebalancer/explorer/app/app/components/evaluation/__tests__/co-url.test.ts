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

import type {Assignments} from '@/app/components/evaluation/AssignmentCard';
import {
  buildCOSearchParams,
  parseCOSearchParams,
} from '@/app/components/evaluation/ConstraintsObjectivesView.url';
import type {MoveSetsViewState} from '@/app/components/evaluation/MoveSetsTable';

const assignments: Assignments = {
  src: {
    base: 'INTERMEDIATE',
    step: 3,
    overrides: [{variable: 'obj_1', container: 'c_9'}],
  },
  dst: {base: 'FINAL', step: null, overrides: []},
};

const ms: MoveSetsViewState = {
  partition: null,
  scope: null,
  objectives: [],
  filters: [],
  offset: 0,
  pageSize: 10,
  orderColumn: null,
  orderDirection: 0,
  isMinimized: true,
};

describe('C&O URL', () => {
  it('writes no detail keys while the pane is closed', () => {
    const search = buildCOSearchParams(assignments, true, [], ms);
    expect(search).not.toContain('detail');
    const parsed = parseCOSearchParams(new URLSearchParams(search));
    expect(parsed.detailExpr).toBeNull();
    expect(parsed.detailTab).toBe('spec');
    expect(parsed.srcBase).toBe('INTERMEDIATE');
    expect(parsed.srcStep).toBe(3);
    expect(parsed.srcOverrides).toEqual(assignments.src.overrides);
  });

  it('round-trips the open expression and tab', () => {
    const tree = buildCOSearchParams(assignments, true, [], ms, {
      expressionId: 42,
      tab: 'tree',
    });
    expect(parseCOSearchParams(new URLSearchParams(tree))).toMatchObject({
      detailExpr: 42,
      detailTab: 'tree',
    });
    const spec = buildCOSearchParams(assignments, true, [], ms, {
      expressionId: 7,
      tab: 'spec',
    });
    expect(spec).not.toContain('detailTab');
    expect(parseCOSearchParams(new URLSearchParams(spec)).detailExpr).toBe(7);
  });

  it('keeps every other key the same as without a pane', () => {
    const without = buildCOSearchParams(assignments, false, [], ms);
    const withPane = buildCOSearchParams(assignments, false, [], ms, {
      expressionId: 5,
      tab: 'spec',
    });
    expect(withPane.startsWith(without)).toBe(true);
  });

  it('ignores a malformed detailExpr', () => {
    expect(
      parseCOSearchParams(new URLSearchParams('detailExpr=abc')).detailExpr,
    ).toBeNull();
  });
});
