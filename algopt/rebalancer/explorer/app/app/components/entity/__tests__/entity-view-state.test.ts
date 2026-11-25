import {describe, expect, it} from 'vitest';

import {
  searchParamsFromViewState,
  viewStateFromSearchParams,
  type ViewState,
} from '@/app/components/entity/entity-view-state';
import {OrderDirection} from '@/lib/rebalancer-explorer-types';

describe('entity view state in the URL', () => {
  it('writes nothing for the defaults', () => {
    const state = viewStateFromSearchParams(new URLSearchParams());
    expect(searchParamsFromViewState(state)).toBe('');
    expect(state).toMatchObject({search: '', movedOnly: null, limit: 25});
  });

  it('round-trips every setting', () => {
    const state: ViewState = {
      offset: 50,
      limit: 50,
      orderColumn: 'dst.host',
      orderDirection: OrderDirection.DESCENDING,
      filters: [{rules: [{stringAny: {column: 'job', values: ['j1']}}]}],
      groupByColumns: ['job'],
      showColumns: ['host', 'cpu'],
      search: 'web',
      movedOnly: true,
    };
    const qs = searchParamsFromViewState(state);
    expect(viewStateFromSearchParams(new URLSearchParams(qs))).toEqual(state);
  });

  it('keeps "all" as the way out of the default changed rows', () => {
    const all = viewStateFromSearchParams(new URLSearchParams('show=all'));
    expect(all.movedOnly).toBe(false);
    expect(searchParamsFromViewState(all)).toBe('show=all');
  });

  it('reads an unknown order direction as ascending', () => {
    const state = viewStateFromSearchParams(
      new URLSearchParams('orderDirection=7&show=other'),
    );
    expect(state.orderDirection).toBe(OrderDirection.ASCENDING);
    expect(state.movedOnly).toBeNull();
  });
});
