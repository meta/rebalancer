import {slugifyMetricName} from './metric-slug';
import type {ProblemMetadata} from './rebalancer-explorer-types';
import {stringifyQs} from './url-state';

export type CommandGroup = 'Page' | 'Entity' | 'Objective' | 'Metric';

/** One destination in the command palette. */
export interface CommandItem {
  group: CommandGroup;
  label: string;
  // Secondary text, e.g. the entity kind.
  detail?: string;
  href: string;
}

/**
 * Every page of a run, from the metadata alone: the fixed pages, entity and
 * dynamic-dimension tables, metric collections, and each objective on
 * Constraints & Objectives (filtered to that name, unchanged rows shown).
 */
export function buildCommandItems(
  basePath: string,
  metadata: ProblemMetadata,
): CommandItem[] {
  const dstBase = metadata.hasFinalAssignment ? 'FINAL' : 'INITIAL';
  const co = `${basePath}/constraints-objectives`;
  const items: CommandItem[] = [
    {group: 'Page', label: 'Summary', href: basePath},
    {
      group: 'Page',
      label: 'Constraints & Objectives',
      href: `${co}?${stringifyQs({dstBase})}`,
    },
    {
      group: 'Page',
      label: 'Local Search',
      href: `${basePath}/local-search-profiling`,
    },
  ];
  const entity = (name: string, detail: string) =>
    items.push({
      group: 'Entity',
      label: name,
      detail,
      href: `${basePath}/entity/${encodeURIComponent(name)}`,
    });
  entity(metadata.objectName, 'objects');
  entity(metadata.containerName, 'containers');
  for (const scope of metadata.scopeNames ?? []) {
    entity(scope, 'scope');
  }
  for (const name of metadata.dynamicDimensionNames ?? []) {
    items.push({
      group: 'Entity',
      label: name,
      detail: 'dynamic dimension',
      href: `${basePath}/dynamic-dimensions/${encodeURIComponent(name)}`,
    });
  }
  for (const name of metadata.metricCollectionNames ?? []) {
    items.push({
      group: 'Metric',
      label: name,
      href: `${basePath}/metrics/${slugifyMetricName(name)}`,
    });
  }
  for (const name of metadata.objectiveNames ?? []) {
    items.push({
      group: 'Objective',
      label: name,
      detail: 'Constraints & Objectives',
      href: `${co}?${stringifyQs({
        dstBase,
        hideUnchanged: '0',
        evalFilters: [{rules: [{stringAny: {column: 'Name', values: [name]}}]}],
      })}`,
    });
  }
  return items;
}

/**
 * Items whose label or detail contains every space-separated word of
 * `query`, case-insensitively. Labels starting with the query come first;
 * otherwise the input order is kept. An empty query returns all items.
 */
export function filterCommandItems(
  items: readonly CommandItem[],
  query: string,
  limit = 50,
): CommandItem[] {
  const q = query.trim().toLowerCase();
  if (q === '') {
    return items.slice(0, limit);
  }
  const words = q.split(/\s+/);
  const prefix: CommandItem[] = [];
  const rest: CommandItem[] = [];
  for (const item of items) {
    const label = item.label.toLowerCase();
    const text = `${label} ${(item.detail ?? '').toLowerCase()}`;
    if (!words.every(w => text.includes(w))) {
      continue;
    }
    (label.startsWith(q) ? prefix : rest).push(item);
  }
  return [...prefix, ...rest].slice(0, limit);
}
