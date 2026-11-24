import {
  asArray,
  normalizeFilters,
  parseQs,
  safeInt,
  stringifyQs,
} from '@/lib/url-state';
import type {Filter} from '@/lib/rebalancer-explorer-types';
import {OrderDirection} from '@/lib/rebalancer-explorer-types';

/** The entity page's whole state, kept in the URL. */
export interface ViewState {
  offset: number;
  limit: number;
  orderColumn: string | null;
  orderDirection: OrderDirection;
  filters: Filter[];
  groupByColumns: string[];
  // Picked columns (field keys); empty picks them automatically.
  showColumns: string[];
  // Case-insensitive part of the row's name.
  search: string;
  // Only the rows that moved (objects) or that moves touched (containers).
  movedOnly: boolean;
}

export const DEFAULT_PAGE_SIZE = 25;

export function viewStateFromSearchParams(
  searchParams: URLSearchParams,
): ViewState {
  const parsed = parseQs(searchParams);
  return {
    offset: safeInt(parsed.offset, 0),
    limit: safeInt(parsed.limit, DEFAULT_PAGE_SIZE),
    orderColumn:
      typeof parsed.orderColumn === 'string' ? parsed.orderColumn : null,
    orderDirection:
      safeInt(parsed.orderDirection, 0) === OrderDirection.DESCENDING
        ? OrderDirection.DESCENDING
        : OrderDirection.ASCENDING,
    filters: normalizeFilters(parsed.filters),
    groupByColumns: asArray<string>(parsed.groupBy, []),
    showColumns: asArray<string>(parsed.showColumns, []),
    search: typeof parsed.q === 'string' ? parsed.q : '',
    movedOnly: parsed.show === 'moved',
  };
}

/** Only what differs from the defaults, so a plain page has a plain URL. */
export function searchParamsFromViewState(viewState: ViewState): string {
  const obj: Record<string, unknown> = {};
  if (viewState.offset !== 0) {
    obj.offset = viewState.offset;
  }
  if (viewState.limit !== DEFAULT_PAGE_SIZE) {
    obj.limit = viewState.limit;
  }
  if (viewState.orderColumn != null) {
    obj.orderColumn = viewState.orderColumn;
  }
  if (viewState.orderDirection !== OrderDirection.ASCENDING) {
    obj.orderDirection = viewState.orderDirection;
  }
  if (viewState.filters.length > 0) {
    obj.filters = viewState.filters;
  }
  if (viewState.groupByColumns.length > 0) {
    obj.groupBy = viewState.groupByColumns;
  }
  if (viewState.showColumns.length > 0) {
    obj.showColumns = viewState.showColumns;
  }
  if (viewState.search !== '') {
    obj.q = viewState.search;
  }
  if (viewState.movedOnly) {
    obj.show = 'moved';
  }
  return stringifyQs(obj);
}
