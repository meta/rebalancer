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

import {useEffect} from 'react';

import qs from 'qs';

import type {Filter, FilterRule} from './rebalancer-explorer-types';

// ---------------------------------------------------------------------------
// Parsing helpers
// ---------------------------------------------------------------------------

export function safeInt(value: unknown, fallback: number): number {
  const n = Number(value);
  return isNaN(n) ? fallback : n;
}

export function asArray<T>(value: unknown, fallback: T[]): T[] {
  return Array.isArray(value) ? (value as T[]) : fallback;
}

export function asString(value: unknown): string | null {
  return typeof value === 'string' ? value : null;
}

// ---------------------------------------------------------------------------
// Filter normalization
// ---------------------------------------------------------------------------

/**
 * Normalize filter rules parsed from URL query strings.
 *
 * `qs.parse()` returns all leaf values as strings, but the Thrift backend
 * expects `comparator` (enum) and `doubleValue` to be numbers.  Without this
 * conversion the move-sets RPC fails with a deserialization error.
 */
export function normalizeFilters(raw: unknown): Filter[] {
  if (!Array.isArray(raw)) return [];
  return raw
    .map((f): Filter | null => {
      if (typeof f !== 'object' || f == null) return null;
      const filterObj = f as {rules?: unknown};
      if (!Array.isArray(filterObj.rules)) return null;
      const rules = filterObj.rules
        .map((rule): FilterRule | null => {
          if (typeof rule !== 'object' || rule == null) return null;
          const r = rule as Record<string, unknown>;
          if (
            'numeric' in r &&
            typeof r.numeric === 'object' &&
            r.numeric != null
          ) {
            const n = r.numeric as Record<string, unknown>;
            return {
              numeric: {
                column: String(n.column ?? ''),
                comparator: Number(n.comparator ?? 0),
                doubleValue: Number(n.doubleValue ?? 0),
              },
            };
          }
          if (
            'stringAny' in r &&
            typeof r.stringAny === 'object' &&
            r.stringAny != null
          ) {
            const s = r.stringAny as Record<string, unknown>;
            if (!s.column) return null;
            return {
              stringAny: {
                column: String(s.column),
                values: Array.isArray(s.values)
                  ? (s.values as string[]).map(String)
                  : [],
              },
            };
          }
          if (
            'stringNe' in r &&
            typeof r.stringNe === 'object' &&
            r.stringNe != null
          ) {
            const s = r.stringNe as Record<string, unknown>;
            if (!s.column) return null;
            return {
              stringNe: {
                column: String(s.column),
                value: String(s.value ?? ''),
              },
            };
          }
          if ('regex' in r && typeof r.regex === 'object' && r.regex != null) {
            const s = r.regex as Record<string, unknown>;
            if (!s.column) return null;
            return {
              regex: {
                column: String(s.column),
                regex: String(s.regex ?? ''),
              },
            };
          }
          return null;
        })
        .filter((r): r is FilterRule => r != null);
      return rules.length > 0 ? {rules} : null;
    })
    .filter((f): f is Filter => f != null);
}

// ---------------------------------------------------------------------------
// qs wrappers with standard options
// ---------------------------------------------------------------------------

export function parseQs(searchParams: URLSearchParams): qs.ParsedQs {
  return qs.parse(searchParams.toString(), {
    ignoreQueryPrefix: true,
    allowDots: true,
    depth: 20,
    // qs turns arrays longer than 20 into objects by default, which drops
    // overrides, shown columns and objectives on reload.
    arrayLimit: 1000,
    parameterLimit: 10000,
  });
}

export function stringifyQs(obj: Record<string, unknown>): string {
  // Use bracket notation (not allowDots) so URLs survive URL shorteners
  // that re-encode brackets and mangle dot-separated paths. Keys stay
  // readable; values are encoded so `+`, `&`, `#` and `%` survive.
  return qs.stringify(obj, {encodeValuesOnly: true});
}

/** Only `keys` of the query, re-serialized; '' when none of them is set. */
export function pickQueryKeys(
  searchParams: URLSearchParams,
  keys: readonly string[],
): string {
  const parsed = parseQs(searchParams);
  const kept: Record<string, unknown> = {};
  for (const key of keys) {
    if (parsed[key] != null) {
      kept[key] = parsed[key];
    }
  }
  return stringifyQs(kept);
}

// ---------------------------------------------------------------------------
// URL sync hook
// ---------------------------------------------------------------------------

/**
 * Keeps URL search params in sync with serialized state.
 * Uses history.replaceState directly (not router.replace) to avoid triggering
 * the Next.js router context and re-rendering useSearchParams() subscribers.
 */
export function useUrlStateSync(
  serialize: () => string,
  deps: unknown[],
): void {
  useEffect(() => {
    const newSearch = serialize();
    const currentSearch = window.location.search.replace(/^\?/, '');
    if (newSearch !== currentSearch) {
      const url =
        newSearch.length > 0
          ? `${window.location.pathname}?${newSearch}`
          : window.location.pathname;
      window.history.replaceState(null, '', url);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, deps);
}
