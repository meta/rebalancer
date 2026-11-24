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

import {findEmptyColumns} from '@/app/components/table/useEmptyColumns';
import type {RowData} from '@/lib/rebalancer-explorer-types';

function row(...cells: Array<string | number | null>): RowData {
  return {
    cells: cells.map(c =>
      c == null
        ? {}
        : typeof c === 'number'
          ? {doubleValue: c}
          : {stringValue: c},
    ),
  } as unknown as RowData;
}

describe('findEmptyColumns', () => {
  const rows = [row('a', 'N/A', null, 0, ''), row('b', 'N/A', null, 1, '')];

  it('finds columns empty in every row (null, empty string, N/A)', () => {
    expect(findEmptyColumns(rows, [0, 1, 2, 3, 4], () => false)).toEqual([
      1, 2, 4,
    ]);
  });

  it('keeps protected columns and numbers, including zero', () => {
    expect(findEmptyColumns(rows, [0, 1, 2, 3, 4], i => i === 1)).toEqual([
      2, 4,
    ]);
  });

  it('reports nothing for a page without rows', () => {
    expect(findEmptyColumns([], [0, 1], () => false)).toEqual([]);
  });

  it('keeps a column that has a value on one row', () => {
    expect(findEmptyColumns([row('N/A'), row('x')], [0], () => false)).toEqual(
      [],
    );
  });
});
