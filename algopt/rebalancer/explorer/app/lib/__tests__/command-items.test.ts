import {describe, expect, it} from 'vitest';

import {buildCommandItems, filterCommandItems} from '@/lib/command-items';
import {parseQs} from '@/lib/url-state';
import type {ProblemMetadata} from '@/lib/rebalancer-explorer-types';

const metadata = {
  objectName: 'placement',
  containerName: 'allotment',
  scopeNames: ['failure_domain', 'server'],
  dynamicDimensionNames: ['dyn_cpu'],
  metricCollectionNames: ['scope item utilization values'],
  objectiveNames: ['minimize_servers', 'host_spread_per_job'],
  hasFinalAssignment: true,
} as unknown as ProblemMetadata;

describe('buildCommandItems', () => {
  const items = buildCommandItems('/run/abc', metadata);

  it('lists pages, entities, metrics and objectives', () => {
    expect(items.map(i => `${i.group}:${i.label}`)).toEqual([
      'Page:Summary',
      'Page:Constraints & Objectives',
      'Page:Local Search',
      'Entity:placement',
      'Entity:allotment',
      'Entity:failure_domain',
      'Entity:server',
      'Entity:dyn_cpu',
      'Metric:scope item utilization values',
      'Objective:minimize_servers',
      'Objective:host_spread_per_job',
    ]);
  });

  it('links an objective to C&O filtered to its name', () => {
    const item = items.find(i => i.label === 'minimize_servers');
    const [path, query] = (item?.href ?? '').split('?');
    expect(path).toBe('/run/abc/constraints-objectives');
    expect(parseQs(new URLSearchParams(query))).toEqual({
      dstBase: 'FINAL',
      hideUnchanged: '0',
      evalFilters: [
        {rules: [{stringAny: {column: 'Name', values: ['minimize_servers']}}]},
      ],
    });
  });

  it('links entity and dynamic-dimension pages', () => {
    expect(items.find(i => i.label === 'server')?.href).toBe(
      '/run/abc/entity/server',
    );
    expect(items.find(i => i.label === 'dyn_cpu')?.href).toBe(
      '/run/abc/dynamic-dimensions/dyn_cpu',
    );
  });
});

describe('filterCommandItems', () => {
  const items = buildCommandItems('/run/abc', metadata);

  it('returns everything for an empty query, up to the limit', () => {
    expect(filterCommandItems(items, '  ')).toHaveLength(items.length);
    expect(filterCommandItems(items, '', 3)).toHaveLength(3);
  });

  it('puts label-prefix matches first', () => {
    const labels = filterCommandItems(items, 's').map(i => i.label);
    expect(labels.slice(0, 3)).toEqual([
      'Summary',
      'server',
      'scope item utilization values',
    ]);
  });

  it('matches every word against label and detail', () => {
    expect(
      filterCommandItems(items, 'SPREAD constraints').map(i => i.label),
    ).toEqual(['host_spread_per_job']);
    expect(filterCommandItems(items, 'scope server').map(i => i.label)).toEqual(
      ['server'],
    );
    expect(filterCommandItems(items, 'objects server')).toEqual([]);
  });
});
