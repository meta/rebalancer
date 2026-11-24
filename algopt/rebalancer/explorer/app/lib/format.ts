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

/**
 * Whether a column type represents a numeric value (right-aligned, monospace).
 */
export function isNumericColumn(type: ColumnType): boolean {
  return (
    type === ColumnType.DIMENSION ||
    type === ColumnType.DOUBLE ||
    type === ColumnType.INTEGER ||
    type === ColumnType.IDENTIFIER ||
    type === ColumnType.UTILIZATION
  );
}

/**
 * Whether a column type contains entity names that can be looked up via the
 * backend's getTypeahead RPC.
 */
export function isTypeaheadColumn(type: ColumnType): boolean {
  return (
    type === ColumnType.ENTITY_NAME ||
    type === ColumnType.SCOPE ||
    type === ColumnType.PARTITION ||
    type === ColumnType.ASSIGNMENT
  );
}

/**
 * Whether a column type should be rendered with colored chips.
 */
export function isHighlightColumn(type: ColumnType): boolean {
  return (
    type === ColumnType.DOUBLE ||
    type === ColumnType.UTILIZATION ||
    type === ColumnType.DIMENSION
  );
}

/**
 * Format a number with significant digits, cleaning up cosmetic trailing
 * zeros.
 */
export function precise(input: number, precision: number = 10): string {
  return input
    .toPrecision(precision)
    .replace(/\.0+$/, '.0')
    .replace(/0+e/, '0e');
}

export type BadgeColor = 'error' | 'success' | 'default';

/**
 * Shared Autocomplete slotProps that allow long option text to wrap instead
 * of being clipped.
 */
export const AUTOCOMPLETE_WORD_BREAK_PROPS = {
  listbox: {
    sx: {
      '& .MuiAutocomplete-option': {
        whiteSpace: 'normal',
        wordBreak: 'break-word',
      },
    },
  },
} as const;

export const CHIP_WORD_BREAK_SX = {
  height: 'auto',
  maxWidth: '100%',
  '& .MuiChip-label': {
    display: 'block',
    whiteSpace: 'normal',
    overflowWrap: 'anywhere',
    wordBreak: 'break-word',
    py: '3px',
  },
} as const;

/**
 * Convert UPPER_SNAKE_CASE or space-separated names to Title Case.
 * e.g. "TOTAL_UTILIZATION" → "Total Utilization"
 *      "Container Utilizations" → "Container Utilizations"
 */
export function toTitleCase(name: string): string {
  return name
    .split(/[_\s]+/)
    .filter(word => word.length > 0)
    .map(word => word.charAt(0).toUpperCase() + word.slice(1).toLowerCase())
    .join(' ');
}

export function getBadgeColor(
  value: number,
  positiveIsBad: boolean = true,
): BadgeColor {
  if (Number.isNaN(value)) {
    return 'error';
  }
  if (value === 0) {
    return 'default';
  }
  const positiveColor: BadgeColor = positiveIsBad ? 'error' : 'success';
  const negativeColor: BadgeColor = positiveIsBad ? 'success' : 'error';
  return value > 0 ? positiveColor : negativeColor;
}

const COUNT_FORMAT = new Intl.NumberFormat('en-US');

/** Whole counts with thousands separators: 5225 -> "5,225". */
export function formatCount(n: number): string {
  return COUNT_FORMAT.format(n);
}

/**
 * Solver runtime in seconds, readable: "5.97 s" below a minute, otherwise
 * "1 h 2 m 3.4 s" (zero units omitted).
 */
export function formatRuntime(totalSeconds: number): string {
  if (!(totalSeconds > 0)) {
    return '0 s';
  }
  if (totalSeconds < 60) {
    return `${Number(totalSeconds.toFixed(2))} s`;
  }
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = Number((totalSeconds % 60).toFixed(1));
  const parts: string[] = [];
  if (hours > 0) {
    parts.push(`${hours} h`);
  }
  if (minutes > 0) {
    parts.push(`${minutes} m`);
  }
  if (seconds > 0) {
    parts.push(`${seconds} s`);
  }
  return parts.join(' ');
}
