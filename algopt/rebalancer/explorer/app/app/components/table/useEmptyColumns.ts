'use client';

import {useMemo, useState} from 'react';

import type {CellData, RowData} from '@/lib/rebalancer-explorer-types';

// The metrics backend fills missing string values with this literal.
const NOT_APPLICABLE = 'N/A';

function isEmptyCell(cell: CellData | undefined | null): boolean {
  if (cell == null) {
    return true;
  }
  if (cell.doubleValue != null) {
    return false;
  }
  const s = cell.stringValue;
  return s == null || s === '' || s === NOT_APPLICABLE;
}

/**
 * Indices (of `candidates`) whose cells are empty in every row of the
 * current page. Protected columns (entity names, group-by columns) are never
 * reported. A page with no rows has no empty columns.
 */
export function findEmptyColumns(
  rows: readonly RowData[],
  candidates: readonly number[],
  isProtected: (index: number) => boolean,
): number[] {
  if (rows.length === 0) {
    return [];
  }
  return candidates.filter(
    index =>
      !isProtected(index) && rows.every(row => isEmptyCell(row.cells[index])),
  );
}

/**
 * Hides columns that are empty on the current page until the user asks for
 * them. Local state only; nothing is written to the URL or to showColumns.
 */
export function useEmptyColumns(
  rows: readonly RowData[] | undefined,
  candidates: readonly number[],
  isProtected: (index: number) => boolean,
) {
  const [showEmpty, setShowEmpty] = useState(false);
  const empty = useMemo(
    () => findEmptyColumns(rows ?? [], candidates, isProtected),
    [rows, candidates, isProtected],
  );
  const rendered = useMemo(() => {
    if (showEmpty || empty.length === 0) {
      return candidates;
    }
    const hidden = new Set(empty);
    return candidates.filter(index => !hidden.has(index));
  }, [candidates, empty, showEmpty]);
  return {
    renderedIndices: rendered,
    emptyCount: empty.length,
    showEmpty,
    setShowEmpty,
  };
}
