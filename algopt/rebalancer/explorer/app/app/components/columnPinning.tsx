'use client';

import {useCallback, useState} from 'react';

import {IconButton, Tooltip} from '@mui/material';
import {Pin, PinOff} from 'lucide-react';
import type {ColumnPinningState, Table, Updater} from '@tanstack/react-table';
import {ICON_MUTED} from '@/lib/ui-tokens';

// Tracks which columns are pinned (kept fixed on the left while the other
// columns scroll sideways). Pass the result into the table.
export function useColumnPinning(defaultPinnedIds: string[]) {
  // Defaults come from the columns, which are empty until data loads, so seeding
  // state once would leave them unpinned. Derive from the defaults until the user
  // or table overrides, then honor the override.
  const [override, setOverride] = useState<ColumnPinningState | null>(null);
  const columnPinning: ColumnPinningState = override ?? {
    left: defaultPinnedIds,
    right: [],
  };
  const setColumnPinning = useCallback(
    (updater: Updater<ColumnPinningState>) => {
      setOverride(prev => {
        const base = prev ?? {left: defaultPinnedIds, right: []};
        return typeof updater === 'function' ? updater(base) : updater;
      });
    },
    [defaultPinnedIds],
  );
  const togglePin = useCallback(
    (colId: string) => {
      setColumnPinning(old => {
        const left = old.left ?? [];
        return left.includes(colId)
          ? {...old, left: left.filter(id => id !== colId)}
          : {...old, left: [...left, colId]};
      });
    },
    [setColumnPinning],
  );
  return {columnPinning, setColumnPinning, togglePin};
}

// Works out where each pinned column sits so they line up against the left edge
// as the rest scroll. Call during render (needs the table).
export function getPinnedColumnHelpers<T>(
  table: Table<T>,
  pinnedLeft: string[],
) {
  const offsets: Record<string, number> = {};
  let acc = 0;
  for (const colId of pinnedLeft) {
    const col = table.getColumn(colId);
    if (col) {
      offsets[colId] = acc;
      acc += col.getSize();
    }
  }
  const stickyStyle = (colId: string, zIndex: number): React.CSSProperties =>
    pinnedLeft.includes(colId)
      ? {position: 'sticky', left: offsets[colId] ?? 0, zIndex}
      : {};
  const isLastPinned = (colId: string) =>
    pinnedLeft.length > 0 && pinnedLeft[pinnedLeft.length - 1] === colId;
  return {stickyStyle, isLastPinned};
}

export function ColumnPinButton({
  pinned,
  onToggle,
  label,
}: {
  pinned: boolean;
  onToggle: () => void;
  label: string;
}) {
  return (
    <Tooltip title={label}>
      <IconButton
        size="small"
        className={pinned ? 'col-action on' : 'col-action'}
        onClick={e => {
          e.stopPropagation();
          onToggle();
        }}
        sx={{
          p: 0.25,
          flexShrink: 0,
          color: pinned ? 'primary.main' : ICON_MUTED,
        }}
        aria-label={label}>
        {pinned ? <PinOff size={14} /> : <Pin size={14} />}
      </IconButton>
    </Tooltip>
  );
}
