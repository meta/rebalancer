'use client';

import {IconButton, Tooltip} from '@mui/material';
import {X} from 'lucide-react';

import type {ColumnDescription} from '@/lib/rebalancer-explorer-types';
import {ColumnType} from '@/lib/rebalancer-explorer-types';
import {ICON_MUTED} from '@/lib/ui-tokens';

// Show/hide backed by the `showColumns` list. A column is hideable unless it's
// the entity-name column or a group-by column (those always show). An empty list
// means "show all", so we expand it to every hideable column before removing
// one. hideDisabled is true when only one hideable column is left, since hiding
// it would empty the list and flip back to showing all.
export function getColumnHiding(
  columns: ColumnDescription[],
  groupBy: string[],
  showColumns: string[],
) {
  const hideable = columns
    .filter(c => c.type !== ColumnType.ENTITY_NAME && !groupBy.includes(c.name))
    .map(c => c.name);
  const shownHideable = (
    showColumns.length === 0 ? hideable : showColumns
  ).filter(n => hideable.includes(n));
  return {
    isHideable: (name: string) => hideable.includes(name),
    hideDisabled: shownHideable.length <= 1,
    // Pass an updater's `prev` so rapid hides compose instead of overwriting.
    hideColumn: (name: string, currentShow: string[] = showColumns) => {
      const from = currentShow.length === 0 ? hideable : currentShow;
      return from.filter(n => n !== name);
    },
  };
}

// Pinned columns can't be hidden (a hidden-but-pinned column errors), and the
// last visible column can't be hidden (see getColumnHiding); in both cases the
// button is disabled and the tooltip says why.
export function ColumnHideButton({
  colName,
  pinned,
  hideDisabled,
  onHide,
}: {
  colName: string;
  pinned: boolean;
  hideDisabled: boolean;
  onHide: () => void;
}) {
  const label = pinned
    ? 'Unpin the column before hiding it'
    : hideDisabled
      ? "Can't hide the last column"
      : `Hide ${colName} column`;
  return (
    <Tooltip title={label}>
      {/* span lets the tooltip show even when the button is disabled */}
      <span>
        <IconButton
          size="small"
          disabled={pinned || hideDisabled}
          onClick={e => {
            e.stopPropagation();
            onHide();
          }}
          className="col-action"
          sx={{p: 0.25, flexShrink: 0, color: ICON_MUTED}}
          aria-label={label}>
          <X size={14} />
        </IconButton>
      </span>
    </Tooltip>
  );
}
