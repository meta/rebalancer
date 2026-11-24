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

import {ColumnType} from './rebalancer-explorer-types';

export interface ColumnTypeInfo {
  /** Short tag shown under table headers, e.g. "dim". */
  short: string;
  /** Readable name, e.g. "Dimension". */
  full: string;
  /** Group heading in column pickers and the filter field list. */
  group: string;
}

const OTHER: ColumnTypeInfo = {short: '', full: 'Other', group: 'Other'};

/** The one ColumnType label map used across tables, pickers and filters. */
export const COLUMN_TYPE_INFO: Record<ColumnType, ColumnTypeInfo> = {
  [ColumnType.ENTITY_NAME]: {
    short: 'entity',
    full: 'Entity name',
    group: 'Other',
  },
  [ColumnType.DIMENSION]: {
    short: 'dim',
    full: 'Dimension',
    group: 'Dimensions',
  },
  [ColumnType.PARTITION]: {
    short: 'part',
    full: 'Partition',
    group: 'Partitions',
  },
  [ColumnType.ASSIGNMENT]: {
    short: 'assign',
    full: 'Assignment',
    group: 'Assignments',
  },
  [ColumnType.SCOPE]: {short: 'scope', full: 'Scope', group: 'Other'},
  [ColumnType.UTILIZATION]: {
    short: 'util',
    full: 'Utilization',
    group: 'Utilizations',
  },
  [ColumnType.DOUBLE]: {short: 'double', full: 'Double', group: 'Other'},
  [ColumnType.STRING]: {short: 'string', full: 'String', group: 'Other'},
  [ColumnType.IDENTIFIER]: {short: 'id', full: 'Identifier', group: 'Other'},
  [ColumnType.INTEGER]: {short: 'int', full: 'Integer', group: 'Other'},
};

/** Order of the `group` headings. */
export const COLUMN_GROUP_ORDER = [
  'Other',
  'Dimensions',
  'Utilizations',
  'Partitions',
  'Assignments',
];

export function columnTypeInfo(type: ColumnType): ColumnTypeInfo {
  return COLUMN_TYPE_INFO[type] ?? OTHER;
}
