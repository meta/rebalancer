import {stringifyQs} from '@/lib/url-state';

/**
 * Link to Constraints & Objectives comparing Initial with `dstBase`. With
 * `name`, the table is filtered to that expression and unchanged rows stay
 * visible; with `expressionId`, its Spec opens in the detail pane; with
 * `tuple`, the page scrolls to that tuple index's table and shows it even when
 * none of its rows changed.
 */
export function constraintsObjectivesHref(
  basePath: string,
  {
    dstBase,
    name,
    expressionId,
    tuple,
  }: {
    dstBase: 'INITIAL' | 'FINAL';
    name?: string;
    expressionId?: number;
    tuple?: number;
  },
): string {
  const query: Record<string, unknown> = {dstBase};
  if (tuple != null) {
    query.focusTuple = tuple;
  }
  if (name != null) {
    query.hideUnchanged = '0';
    query.evalFilters = [
      {rules: [{stringAny: {column: 'Name', values: [name]}}]},
    ];
  }
  if (expressionId != null) {
    query.detailExpr = expressionId;
  }
  return `${basePath}/constraints-objectives?${stringifyQs(query)}`;
}
