import {
  ColumnType,
  type ColumnDescription,
  type RowData,
} from './rebalancer-explorer-types';

/**
 * How the run changed, indexed so that any object, container, scope item or
 * partition group can list what connects it to the moves: the objects that
 * moved in or out, and the move sets that moved them.
 */

/** Where one moved object sat at Initial and at Final. */
export interface MovedObject {
  // Container, Initial → Final.
  src: string;
  dst: string;
  // Item per scope (and the container), Initial → Final, from the object's
  // src./dst. columns.
  where: ReadonlyMap<string, {src: string; dst: string}>;
  // Group per partition.
  groups: ReadonlyMap<string, string>;
}

function cellString(row: RowData, i: number): string {
  if (i < 0) {
    return '';
  }
  const cell = row.cells[i];
  if (cell?.stringValue != null) {
    return cell.stringValue;
  }
  return cell?.doubleValue != null ? String(cell.doubleValue) : '';
}

/**
 * The moved objects from their object-table rows. `destinations` (object →
 * final container) is authoritative for the container; the rows give the
 * initial container, the scope items and the groups.
 */
export function movedObjectsFromRows(
  columns: readonly ColumnDescription[],
  rows: readonly RowData[],
  destinations: Readonly<Record<string, string>>,
  containerName: string,
  partitionNames: readonly string[],
): Map<string, MovedObject> {
  const name = columns.findIndex(c => c.primaryKey);
  const pairs: Array<{key: string; src: number; dst: number}> = [];
  columns.forEach((c, i) => {
    if (c.type !== ColumnType.ASSIGNMENT || !c.name.startsWith('src.')) {
      return;
    }
    const key = c.name.substring(4);
    const dst = columns.findIndex(d => d.name === `dst.${key}`);
    if (dst >= 0) {
      pairs.push({key, src: i, dst});
    }
  });
  const partitions = new Set(partitionNames);
  const groupColumns = columns
    .map((c, i) => ({name: c.name, i}))
    .filter(c => partitions.has(c.name));

  const rowsByName = new Map<string, RowData>();
  if (name >= 0) {
    for (const row of rows) {
      rowsByName.set(cellString(row, name), row);
    }
  }
  const moved = new Map<string, MovedObject>();
  for (const [object, dst] of Object.entries(destinations)) {
    const row = rowsByName.get(object);
    const where = new Map<string, {src: string; dst: string}>();
    const groups = new Map<string, string>();
    if (row != null) {
      for (const p of pairs) {
        where.set(p.key, {
          src: cellString(row, p.src),
          dst: cellString(row, p.dst),
        });
      }
      for (const g of groupColumns) {
        groups.set(g.name, cellString(row, g.i));
      }
    }
    const src = where.get(containerName)?.src ?? '';
    where.set(containerName, {src, dst});
    moved.set(object, {src, dst, where, groups});
  }
  return moved;
}

/** Moves out of and into each item of `key` (a scope or the container). */
export interface ItemMoves {
  out: number;
  in: number;
}

/**
 * The items of `key` that moved objects left or entered, Initial → Final.
 * An object that stays in the same item (a move inside it) is not counted.
 */
export function itemMoves(
  moved: ReadonlyMap<string, MovedObject>,
  key: string,
): Map<string, ItemMoves> {
  const counts = new Map<string, ItemMoves>();
  const bump = (item: string, side: 'out' | 'in') => {
    const c = counts.get(item) ?? {out: 0, in: 0};
    c[side]++;
    counts.set(item, c);
  };
  for (const m of moved.values()) {
    const w = m.where.get(key);
    if (w == null || w.src === w.dst) {
      continue;
    }
    if (w.src !== '') {
      bump(w.src, 'out');
    }
    if (w.dst !== '') {
      bump(w.dst, 'in');
    }
  }
  return counts;
}

/** The moved objects that left and that entered one item of `key`. */
export function movedThrough(
  moved: ReadonlyMap<string, MovedObject>,
  key: string,
  item: string,
): {out: string[]; in: string[]} {
  const out: string[] = [];
  const into: string[] = [];
  for (const [object, m] of moved) {
    const w = m.where.get(key);
    if (w == null || w.src === w.dst) {
      continue;
    }
    if (w.src === item) {
      out.push(object);
    } else if (w.dst === item) {
      into.push(object);
    }
  }
  return {out, in: into};
}

/** Moved objects of one partition group. */
export function movedInGroup(
  moved: ReadonlyMap<string, MovedObject>,
  partition: string,
  group: string,
): string[] {
  const objects: string[] = [];
  for (const [object, m] of moved) {
    if (m.groups.get(partition) === group) {
      objects.push(object);
    }
  }
  return objects;
}

/** How many moved objects each group of a partition has. */
export function movedPerGroup(
  moved: ReadonlyMap<string, MovedObject>,
  partition: string,
): Map<string, number> {
  const counts = new Map<string, number>();
  for (const m of moved.values()) {
    const g = m.groups.get(partition);
    if (g != null && g !== '') {
      counts.set(g, (counts.get(g) ?? 0) + 1);
    }
  }
  return counts;
}

/**
 * The scope to show how `objects` spread over: of the scopes they moved
 * between two of its items, the coarsest (fewest items touched), then the one
 * with the most such moves; the container when they crossed no scope.
 * Spread and diversity goals are usually about coarse scopes.
 */
export function spreadScopeOf(
  moved: ReadonlyMap<string, MovedObject>,
  objects: Iterable<string>,
  scopes: readonly string[],
  containerName: string,
): string {
  const stats = new Map<string, {items: Set<string>; crossings: number}>();
  for (const o of objects) {
    const m = moved.get(o);
    if (m == null) {
      continue;
    }
    for (const scope of scopes) {
      const w = m.where.get(scope);
      // A move into or out of no item is not a move between items.
      if (w == null || w.src === w.dst || w.src === '' || w.dst === '') {
        continue;
      }
      const st = stats.get(scope) ?? {items: new Set<string>(), crossings: 0};
      st.items.add(w.src);
      st.items.add(w.dst);
      st.crossings++;
      stats.set(scope, st);
    }
  }
  let best: {scope: string; items: number; crossings: number} | null = null;
  for (const scope of scopes) {
    const st = stats.get(scope);
    if (st == null || scope === containerName) {
      continue;
    }
    const items = st.items.size;
    if (
      best == null ||
      items < best.items ||
      (items === best.items && st.crossings > best.crossings)
    ) {
      best = {scope, items, crossings: st.crossings};
    }
  }
  return best?.scope ?? containerName;
}

/** One flow of moved objects between two items of a scope. */
export interface Flow {
  src: string;
  dst: string;
  count: number;
}

/**
 * Moved objects per (initial item, final item) of `key`, most first; only
 * `only` when given.
 */
export function flows(
  moved: ReadonlyMap<string, MovedObject>,
  key: string,
  only?: ReadonlySet<string>,
): Flow[] {
  const byPair = new Map<string, Flow>();
  for (const [object, m] of moved) {
    if (only != null && !only.has(object)) {
      continue;
    }
    const w = m.where.get(key);
    if (w == null || w.src === w.dst) {
      continue;
    }
    const k = JSON.stringify([w.src, w.dst]);
    const f = byPair.get(k) ?? {src: w.src, dst: w.dst, count: 0};
    f.count++;
    byPair.set(k, f);
  }
  return Array.from(byPair.values()).sort(
    (a, b) =>
      b.count - a.count ||
      a.src.localeCompare(b.src) ||
      a.dst.localeCompare(b.dst),
  );
}

/** One row of getMoveSets: an object moved by a move set. */
export interface MoveRow {
  set: number;
  object: string;
  src: string;
  dst: string;
}

/** The run's move sets, Initial → Final, with each set's objective changes. */
export interface RunMoveSets {
  rows: MoveRow[];
  // The objectives with a change column, in metadata order.
  objectiveNames: string[];
  // Move set → change of objectiveNames[j]; empty without change columns.
  deltas: Map<number, number[]>;
}

/**
 * Reads getMoveSets rows: the move set (or move) number, the object and its source
 * and destination containers, and the `<objective> (Δ)` columns the server
 * repeats on every row of a set.
 */
export function moveSetsFromTable(
  columns: readonly ColumnDescription[],
  rows: readonly RowData[],
  objectiveNames: readonly string[],
): RunMoveSets {
  const at = (name: string) => columns.findIndex(c => c.name === name);
  // "Move #" on runs whose move sets each move one object.
  const setCol = Math.max(at('MoveSet #'), at('Move #'));
  const objCol = at('Object');
  const srcCol = at('Source Container');
  const dstCol = at('Destination Container');
  const named = objectiveNames
    .map(name => ({name, i: at(`${name} (Δ)`)}))
    .filter(x => x.i >= 0);
  const out: RunMoveSets = {
    rows: [],
    objectiveNames: named.map(x => x.name),
    deltas: new Map(),
  };
  if (setCol < 0 || objCol < 0) {
    return out;
  }
  for (const row of rows) {
    const set = row.cells[setCol]?.doubleValue;
    if (set == null) {
      continue;
    }
    out.rows.push({
      set,
      object: cellString(row, objCol),
      src: cellString(row, srcCol),
      dst: cellString(row, dstCol),
    });
    if (named.length > 0 && !out.deltas.has(set)) {
      out.deltas.set(
        set,
        named.map(x => row.cells[x.i]?.doubleValue ?? 0),
      );
    }
  }
  return out;
}

/** The distinct move sets that moved any of `objects`, in solver order. */
export function setsOf(
  moveSets: RunMoveSets,
  objects: Iterable<string>,
): number[] {
  const wanted = new Set(objects);
  const sets = new Set<number>();
  for (const r of moveSets.rows) {
    if (wanted.has(r.object)) {
      sets.add(r.set);
    }
  }
  return Array.from(sets).sort((a, b) => a - b);
}

/** One objective's total change over some move sets. */
export interface ObjectiveChange {
  name: string;
  delta: number;
}

/**
 * Each objective's change summed over `sets`, each set counted once; only
 * the objectives that changed. Changes that cancel to rounding noise (under
 * 1e-10 of the summed magnitudes, the solver's own test) count as none.
 */
export function objectiveChanges(
  moveSets: RunMoveSets,
  sets: readonly number[],
): ObjectiveChange[] {
  const totals = moveSets.objectiveNames.map(() => 0);
  const sizes = moveSets.objectiveNames.map(() => 0);
  for (const s of sets) {
    const d = moveSets.deltas.get(s);
    if (d == null) {
      continue;
    }
    d.forEach((v, j) => {
      totals[j] += v;
      sizes[j] += Math.abs(v);
    });
  }
  return moveSets.objectiveNames
    .map((name, j) => ({name, delta: totals[j], size: sizes[j]}))
    .filter(c => Math.abs(c.delta) > 1e-10 * Math.max(1, c.size))
    .map(({name, delta}) => ({name, delta}));
}
