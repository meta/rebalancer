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
  COLUMN_GROUP_ORDER,
  COLUMN_TYPE_INFO,
  columnTypeInfo,
} from '@/lib/column-types';
import {ColumnType} from '@/lib/rebalancer-explorer-types';

describe('column types', () => {
  it('labels every ColumnType', () => {
    const values = Object.values(ColumnType).filter(
      (v): v is ColumnType => typeof v === 'number',
    );
    for (const type of values) {
      expect(COLUMN_TYPE_INFO[type].full).not.toBe('');
      expect(COLUMN_GROUP_ORDER).toContain(COLUMN_TYPE_INFO[type].group);
    }
  });

  it('keeps the filter groups used before', () => {
    expect(columnTypeInfo(ColumnType.DIMENSION).group).toBe('Dimensions');
    expect(columnTypeInfo(ColumnType.UTILIZATION).group).toBe('Utilizations');
    expect(columnTypeInfo(ColumnType.PARTITION).group).toBe('Partitions');
    expect(columnTypeInfo(ColumnType.ASSIGNMENT).group).toBe('Assignments');
    expect(columnTypeInfo(ColumnType.STRING).group).toBe('Other');
    expect(columnTypeInfo(99 as ColumnType).group).toBe('Other');
  });
});
