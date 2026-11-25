import {isNumericColumn} from './format';
import {
  ColumnType,
  type CellData,
  type ColumnDescription,
  type FilterRule,
  type RowData,
} from './rebalancer-explorer-types';

/**
 * What a field of an entity row holds. Pairs (initial and final values of the
 * same thing) are one field.
 */
export type FieldKind =
  | 'name'
  | 'count'
  | 'assignment'
  | 'partition'
  | 'scope'
  | 'dimension'
  | 'utilization'
  | 'other';

export interface EntityField {
  // Stable id, kept in the URL: the column name, or a pair's base name.
  key: string;
  label: string;
  kind: FieldKind;
  numeric: boolean;
  // Column index of the initial value of a pair; null for a single column.
  a: number | null;
  // Column index of the value, or of the final value of a pair.
  b: number;
  // The column the backend orders by: the final value of a pair.
  sortColumn: string;
  // Every backend column the field covers.
  columns: string[];
  primaryKey: boolean;
}

export const ROW_COUNT_COLUMN = 'Row_Count';

function kindOf(col: ColumnDescription): FieldKind {
  if (col.name === ROW_COUNT_COLUMN) {
    return 'count';
  }
  switch (col.type) {
    case ColumnType.ENTITY_NAME:
      return 'name';
    case ColumnType.ASSIGNMENT:
      return 'assignment';
    case ColumnType.PARTITION:
      return 'partition';
    case ColumnType.SCOPE:
      return 'scope';
    case ColumnType.DIMENSION:
      return 'dimension';
    case ColumnType.UTILIZATION:
      return 'utilization';
    default:
      return 'other';
  }
}

// "src.x" / "dst.x" and "x.initUtil" / "x.finalUtil" name one value at the
// initial and the final assignment.
function pairOf(name: string): {base: string; side: 'a' | 'b'} | null {
  if (name.startsWith('src.')) {
    return {base: name.slice(4), side: 'a'};
  }
  if (name.startsWith('dst.')) {
    return {base: name.slice(4), side: 'b'};
  }
  if (name.endsWith('.initUtil')) {
    return {base: name.slice(0, -'.initUtil'.length), side: 'a'};
  }
  if (name.endsWith('.finalUtil')) {
    return {base: name.slice(0, -'.finalUtil'.length), side: 'b'};
  }
  return null;
}

function pairName(base: string, side: 'a' | 'b', util: boolean): string {
  if (util) {
    return `${base}.${side === 'a' ? 'initUtil' : 'finalUtil'}`;
  }
  return `${side === 'a' ? 'src' : 'dst'}.${base}`;
}

/** The result's columns as fields, in column order, pairs merged. */
export function buildFields(
  columns: readonly ColumnDescription[],
): EntityField[] {
  const index = new Map(columns.map((c, i) => [c.name, i]));
  const used = new Set<number>();
  const fields: EntityField[] = [];
  columns.forEach((col, i) => {
    if (used.has(i)) {
      return;
    }
    const pair = pairOf(col.name);
    const util =
      col.name.endsWith('.initUtil') || col.name.endsWith('.finalUtil');
    const other =
      pair == null
        ? undefined
        : index.get(pairName(pair.base, pair.side === 'a' ? 'b' : 'a', util));
    if (pair != null && other != null && columns[other].type === col.type) {
      const a = pair.side === 'a' ? i : other;
      const b = pair.side === 'a' ? other : i;
      used.add(i).add(other);
      fields.push({
        key: index.has(pair.base) ? `${pair.base} (A → B)` : pair.base,
        label: pair.base,
        kind: kindOf(col),
        numeric: isNumericColumn(col.type),
        a,
        b,
        sortColumn: columns[b].name,
        columns: [columns[a].name, columns[b].name],
        primaryKey: false,
      });
      return;
    }
    used.add(i);
    fields.push({
      key: col.name,
      label: col.name === ROW_COUNT_COLUMN ? 'Rows' : col.name,
      kind: kindOf(col),
      numeric: isNumericColumn(col.type),
      a: null,
      b: i,
      sortColumn: col.name,
      columns: [col.name],
      primaryKey: col.primaryKey,
    });
  });
  return fields;
}

// The metrics backend fills missing string values with this literal.
const NOT_APPLICABLE = 'N/A';

export type CellValue = number | string | null;

export function cellValue(cell: CellData | undefined): CellValue {
  if (cell == null) {
    return null;
  }
  if (cell.doubleValue != null) {
    return Number(cell.doubleValue);
  }
  const s = cell.stringValue;
  return s == null || s === '' || s === NOT_APPLICABLE ? null : s;
}

/** A field's initial and final value in a row; equal for a single column. */
export function fieldValues(
  field: EntityField,
  row: RowData,
): {a: CellValue; b: CellValue} {
  const b = cellValue(row.cells[field.b]);
  return {a: field.a == null ? b : cellValue(row.cells[field.a]), b};
}

export function isBlank(v: CellValue): boolean {
  return v == null || v === 0;
}

/** How a field looks across the rows of a page. */
export interface FieldProfile {
  // Blank (0 or no value) on both sides in every row.
  blank: boolean;
  // The same value in every row, unchanged between initial and final.
  constant: boolean;
  value: CellValue;
  // Rows whose initial and final values differ.
  changed: number;
  // Distinct final values.
  distinct: number;
  // Rows with a value (not blank) on either side.
  filled: number;
}

export function profileField(
  field: EntityField,
  rows: readonly RowData[],
): FieldProfile {
  let blank = true;
  let constant = true;
  let changed = 0;
  let filled = 0;
  const first = rows.length > 0 ? fieldValues(field, rows[0]).b : null;
  const distinct = new Set<CellValue>();
  for (const row of rows) {
    const {a, b} = fieldValues(field, row);
    if (!isBlank(a) || !isBlank(b)) {
      blank = false;
      filled++;
    }
    if (a !== b) {
      changed++;
      constant = false;
    }
    if (b !== first) {
      constant = false;
    }
    distinct.add(b);
  }
  return {
    blank,
    constant,
    value: first,
    changed,
    distinct: distinct.size,
    filled,
  };
}

export function profileFields(
  fields: readonly EntityField[],
  rows: readonly RowData[],
): Map<string, FieldProfile> {
  return new Map(fields.map(f => [f.key, profileField(f, rows)]));
}

// Below this many rows, "the same on every row" says nothing.
export const MIN_ROWS_TO_FOLD = 3;

const KIND_RANK: Record<FieldKind, number> = {
  name: 0,
  count: 0,
  assignment: 1,
  partition: 2,
  scope: 3,
  utilization: 4,
  dimension: 5,
  other: 6,
};

/** Name and row-count fields are always shown; the rest can be picked. */
export function isPickable(field: EntityField): boolean {
  return field.kind !== 'name' && field.kind !== 'count';
}

/** Whether a field only repeats one value on this page, or is blank. */
export function isFolded(p: FieldProfile, rowCount: number): boolean {
  return p.blank || (p.constant && rowCount >= MIN_ROWS_TO_FOLD);
}

// A text column (nearly) unique to every row names the row again.
function isIdLike(p: FieldProfile, rowCount: number): boolean {
  return rowCount >= MIN_ROWS_TO_FOLD && p.distinct >= 0.8 * rowCount;
}

// Which kinds the automatic columns take first, a column of each in turn.
const PICK_RANK: Record<FieldKind, number> = {
  name: 0,
  count: 0,
  partition: 1,
  scope: 2,
  utilization: 3,
  dimension: 4,
  assignment: 5,
  other: 6,
};

/**
 * The fields worth a column, best first: the ones that changed, then one of
 * each kind in turn (partitions, scopes, utilization, dimensions,
 * assignments), the fullest of a kind first. Fields that are blank or the
 * same on every row are left out; a text field (nearly) unique to every row
 * (an id in disguise) and the `demote` fields come last.
 */
export function rankFields(
  fields: readonly EntityField[],
  profiles: ReadonlyMap<string, FieldProfile>,
  rowCount: number,
  exclude: ReadonlySet<string> = new Set(),
  demote: ReadonlySet<string> = new Set(),
): string[] {
  const items = fields
    .map((f, order) => ({f, p: profiles.get(f.key), order}))
    .filter(
      (x): x is {f: EntityField; p: FieldProfile; order: number} =>
        isPickable(x.f) &&
        !exclude.has(x.f.key) &&
        x.p != null &&
        !isFolded(x.p, rowCount),
    )
    .map(({f, p, order}) => ({
      key: f.key,
      rank: PICK_RANK[f.kind],
      changed: p.changed > 0,
      late: demote.has(f.key) || (!f.numeric && isIdLike(p, rowCount)),
      filled: p.filled,
      order,
    }));
  type Item = (typeof items)[number];
  const byFullness = (x: Item, y: Item) =>
    y.filled - x.filled || x.order - y.order;
  const changed = items
    .filter(x => x.changed)
    .sort((x, y) => x.rank - y.rank || x.order - y.order);
  const rest = items.filter(x => !x.changed && !x.late);
  const late = items.filter(x => !x.changed && x.late).sort(byFullness);
  const kinds = Array.from(new Set(rest.map(x => x.rank))).sort(
    (x, y) => x - y,
  );
  const queues = kinds.map(k =>
    rest.filter(x => x.rank === k).sort(byFullness),
  );
  const turns: Item[] = [];
  for (let i = 0; turns.length < rest.length; i++) {
    for (const q of queues) {
      if (i < q.length) {
        turns.push(q[i]);
      }
    }
  }
  return [...changed, ...turns, ...late].map(x => x.key);
}

/** Column width in px for a field. */
export function fieldWidth(field: EntityField): number {
  if (field.kind === 'name') {
    return 240;
  }
  if (field.kind === 'count') {
    return 96;
  }
  if (field.numeric) {
    return field.a != null ? 168 : 116;
  }
  return field.a != null ? 260 : 180;
}

/** The first keys whose columns fit `budget` px; at least `min` of them. */
export function fitToWidth(
  keys: readonly string[],
  widthOf: (key: string) => number,
  budget: number,
  min = 2,
): string[] {
  const out: string[] = [];
  let used = 0;
  for (const key of keys) {
    const w = widthOf(key);
    if (out.length >= min && used + w > budget) {
      break;
    }
    out.push(key);
    used += w;
  }
  return out;
}

/** The pair whose value changed most, relative to its size, in a row. */
export function largestChange(
  fields: readonly EntityField[],
  row: RowData,
): {field: EntityField; a: number; b: number; changed: number} | null {
  let best: {field: EntityField; a: number; b: number; rel: number} | null =
    null;
  let changed = 0;
  for (const field of fields) {
    if (field.a == null || !field.numeric) {
      continue;
    }
    const {a, b} = fieldValues(field, row);
    const x = typeof a === 'number' ? a : 0;
    const y = typeof b === 'number' ? b : 0;
    if (x === y) {
      continue;
    }
    changed++;
    const rel = Math.abs(y - x) / Math.max(Math.abs(x), Math.abs(y));
    if (
      best == null ||
      rel > best.rel ||
      (rel === best.rel && Math.abs(y - x) > Math.abs(best.b - best.a))
    ) {
      best = {field, a: x, b: y, rel};
    }
  }
  return best == null
    ? null
    : {field: best.field, a: best.a, b: best.b, changed};
}

/**
 * Field keys from the URL's column list. Older links name backend columns
 * ("src.reservation", "cpu.finalUtil"); those map to their pair.
 */
export function normalizeShown(
  shown: readonly string[],
  fields: readonly EntityField[],
): string[] {
  const byColumn = new Map<string, string>();
  for (const f of fields) {
    byColumn.set(f.key, f.key);
    for (const c of f.columns) {
      if (!byColumn.has(c)) {
        byColumn.set(c, f.key);
      }
    }
  }
  const out: string[] = [];
  for (const name of shown) {
    const key = byColumn.get(name);
    if (key != null && !out.includes(key)) {
      out.push(key);
    }
  }
  return out;
}

function escapeRegex(text: string): string {
  return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

/** Case-insensitive "contains" on a text column (the backend uses RE2). */
export function searchRule(column: string, text: string): FilterRule {
  return {regex: {column, regex: `(?i)${escapeRegex(text)}`}};
}

/** The fields with these keys, grouped by kind, each kind in column order. */
export function inDisplayOrder(
  fields: readonly EntityField[],
  keys: ReadonlySet<string>,
): EntityField[] {
  return fields
    .map((f, order) => ({f, order}))
    .filter(({f}) => keys.has(f.key))
    .sort(
      (x, y) => KIND_RANK[x.f.kind] - KIND_RANK[y.f.kind] || x.order - y.order,
    )
    .map(({f}) => f);
}
