import {
  ACCENT,
  BORDER_STRONG,
  DIVIDER,
  PINNED_SHADOW,
  SURFACE,
  SURFACE_2,
  ACCENT_50,
  TEXT_TERTIARY,
} from '@/lib/ui-tokens';

/**
 * The design language's table (plain `<table>` elements): sticky head in
 * 12px / 600 tertiary on surface-2, 13px cells with 8 / 12px padding, row
 * hover on surface-2 (opaque, so pinned cells stay solid), selection in the
 * action tint. Numeric cells opt in to right-aligned tabular figures with the
 * `num` class.
 */
export const TABLE_SX = {
  width: '100%',
  borderCollapse: 'separate',
  borderSpacing: 0,
  '& th': {
    position: 'sticky',
    top: 0,
    zIndex: 1,
    bgcolor: SURFACE_2,
    color: TEXT_TERTIARY,
    fontSize: 12,
    fontWeight: 600,
    lineHeight: 1.35,
    textAlign: 'left',
    verticalAlign: 'bottom',
    px: 1.5,
    py: 1,
    borderBottom: `1px solid ${DIVIDER}`,
    userSelect: 'none',
  },
  '& td': {
    fontSize: 13,
    lineHeight: 1.45,
    px: 1.5,
    py: 1,
    borderBottom: `1px solid ${DIVIDER}`,
    verticalAlign: 'middle',
  },
  '& tbody tr': {bgcolor: SURFACE},
  '& tbody tr:hover': {bgcolor: SURFACE_2},
  '& tbody tr[aria-selected="true"]': {bgcolor: ACCENT_50},
  '& th.sortable': {cursor: 'pointer'},
  '& td.copyable': {position: 'relative', pr: 3, cursor: 'pointer'},
  '& td.empty': {p: 0},
  '& .select': {width: 36, px: 0.75, textAlign: 'center'},
  '& tfoot td': {borderTop: `2px solid ${BORDER_STRONG}`, borderBottom: 0},
  '& .num': {textAlign: 'right', fontVariantNumeric: 'tabular-nums'},
  // Pinned columns: callers set position/left/zIndex inline (offsets
  // depend on column widths); these keep the cell opaque and shaded.
  '& td.pinned-cell': {bgcolor: 'inherit'},
  '& td.primary': {fontWeight: 600},
  '& .last-pinned': {boxShadow: PINNED_SHADOW},
  // Column actions (pin, hide) float in the header's corner, on the side
  // away from the label, and show on hover or keyboard focus, so they take
  // no space from the label. Hidden actions neither cover nor catch clicks.
  '& th .col-actions': {
    position: 'absolute',
    top: 4,
    right: 6,
    display: 'flex',
    gap: '2px',
    borderRadius: 1,
    pointerEvents: 'none',
  },
  '& th.num .col-actions': {right: 'auto', left: 6},
  '& th:hover .col-actions, & th:focus-within .col-actions, & th .col-actions:has(.on)':
    {bgcolor: SURFACE_2},
  '& th .col-action': {
    opacity: 0,
    pointerEvents: 'none',
    transition: 'opacity 120ms',
  },
  '& th:hover .col-action, & th:focus-within .col-action, & th .col-action.on':
    {opacity: 1, pointerEvents: 'auto'},
  // Touch screens have no hover: show the actions after the label.
  '@media (hover: none) and (pointer: coarse)': {
    '& th .col-actions': {position: 'static', flexShrink: 0},
    '& th .col-action': {opacity: 1, pointerEvents: 'auto'},
  },
  '& .resize-handle': {
    position: 'absolute',
    right: 0,
    top: 0,
    height: '100%',
    width: 4,
    cursor: 'col-resize',
  },
  '& .resize-handle:hover': {bgcolor: BORDER_STRONG},
  '& .resize-handle.active': {bgcolor: ACCENT},
} as const;

/** Single line with an ellipsis; pair with `data-overflow` for the hover card. */
export const ELLIPSIS_SX = {
  display: 'block',
  minWidth: 0,
  overflow: 'hidden',
  textOverflow: 'ellipsis',
  whiteSpace: 'nowrap',
} as const;

/** Up to two lines, then an ellipsis; pair with `data-overflow`. */
export const CLAMP_2_SX = {
  display: '-webkit-box',
  WebkitLineClamp: 2,
  WebkitBoxOrient: 'vertical',
  overflow: 'hidden',
  overflowWrap: 'anywhere',
} as const;
