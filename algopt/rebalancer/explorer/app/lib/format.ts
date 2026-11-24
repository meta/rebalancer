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

const MINUS = '−';

const COMPACT_UNITS = [
  [1e12, 'T'],
  [1e9, 'B'],
  [1e6, 'M'],
  [1e3, 'K'],
] as const;

function trimZeros(s: string): string {
  return s.includes('.') ? s.replace(/\.?0+$/, '') : s;
}

/**
 * A short, readable number: K/M/B/T from 10,000, an exponent below 1e-4,
 * at most 3 significant digits below 1, and a true minus sign.
 * 3.113754638 -> "3.11", 264 -> "264", 1234567 -> "1.23M", -6.8e-7 -> "−6.8e−7".
 */
export function formatCompact(v: number): string {
  if (Number.isNaN(v)) {
    return '—';
  }
  if (!Number.isFinite(v)) {
    return v > 0 ? '∞' : `${MINUS}∞`;
  }
  const a = Math.abs(v);
  let s: string;
  if (a === 0) {
    s = '0';
  } else if (a < 1e-4) {
    s = a.toExponential(1).replace('e-', `e${MINUS}`).replace('.0e', 'e');
  } else if (a >= 1e15) {
    s = a.toExponential(2).replace('e+', 'e');
  } else if (a >= 1e4) {
    const [unit, suffix] =
      COMPACT_UNITS.find(([u]) => a >= u) ?? COMPACT_UNITS[3];
    const x = a / unit;
    s = trimZeros(x.toFixed(x >= 100 ? 0 : x >= 10 ? 1 : 2)) + suffix;
  } else if (a >= 100) {
    s = formatCount(Number(a.toFixed(1)));
  } else if (a >= 1) {
    s = trimZeros(a.toFixed(2));
  } else {
    s = trimZeros(a.toPrecision(3));
  }
  return (v < 0 ? MINUS : '') + s;
}

/** A compact change with its sign: "+264", "−6.8e−7"; 0 stays "0". */
export function formatSignedCompact(delta: number): string {
  if (delta === 0) {
    return '0';
  }
  return (delta > 0 ? '+' : MINUS) + formatCompact(Math.abs(delta));
}

/**
 * A change as a percentage of |base|: "−12%", "+1.5%", "−<0.01%"; "new"
 * when a zero base grows, and "" when there is no change or no base.
 */
export function formatPercentChange(delta: number, base: number): string {
  if (delta === 0 || Number.isNaN(delta)) {
    return '';
  }
  if (base === 0) {
    return delta > 0 ? 'new' : '';
  }
  const p = (delta / Math.abs(base)) * 100;
  const sign = p > 0 ? '+' : MINUS;
  const a = Math.abs(p);
  if (a < 0.01) {
    return `${sign}<0.01%`;
  }
  return `${sign}${trimZeros(a.toFixed(a >= 10 ? 0 : a >= 1 ? 1 : 2))}%`;
}
