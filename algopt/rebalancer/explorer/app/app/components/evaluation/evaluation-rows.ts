import type {ExpressionResult} from '@/lib/rebalancer-explorer-types';

/** Merged row joining src and dst expression data. */
export interface EvaluationRow {
  expressionId: number;
  name: string;
  description: string;
  srcValue: number;
  dstValue: number;
  delta: number;
  percentage: number;
  // Separated local-search penalty; null when the objective has none.
  srcPenalty: number | null;
  dstPenalty: number | null;
  penaltyDelta: number;
}

/**
 * Compute percentage change: (B - A) / |A| * 100.
 * Returns 0 when both are zero, 100 when only A is zero.
 */
export function calculatePercentageChange(src: number, dst: number): number {
  const delta = dst - src;
  if (src === 0) {
    return dst === 0 ? 0 : 100;
  }
  return (delta / Math.abs(src)) * 100;
}

/**
 * The percent change as a short label: "from 0" when A is 0 (a percentage of
 * 0 means nothing), "<0.01%" or "-<0.01%" for a nonzero change that rounds
 * to 0.00%, otherwise two decimals. `percentage` is
 * calculatePercentageChange(src, dst).
 */
export function formatPercentChange(
  src: number,
  dst: number,
  percentage: number,
): string {
  if (src === dst) {
    return '0.00%';
  }
  if (src === 0) {
    return 'from 0';
  }
  if (Math.abs(percentage) < 0.005) {
    return percentage < 0 ? '-<0.01%' : '<0.01%';
  }
  return `${percentage.toFixed(2)}%`;
}

/** Joins the A and B results by expression id, in A's order. */
export function joinEvaluationRows(
  srcExpressions: ExpressionResult[],
  dstExpressions: ExpressionResult[],
): EvaluationRow[] {
  const dstMap = new Map<number, ExpressionResult>();
  for (const expr of dstExpressions) {
    dstMap.set(expr.id, expr);
  }

  return srcExpressions
    .map(src => {
      const dst = dstMap.get(src.id);
      if (dst == null) {
        return null;
      }
      const srcPenalty = src.penaltyValue ?? null;
      const dstPenalty = dst.penaltyValue ?? null;
      return {
        expressionId: src.id,
        name: src.name,
        description: src.description,
        srcValue: src.value,
        dstValue: dst.value,
        delta: dst.value - src.value,
        percentage: calculatePercentageChange(src.value, dst.value),
        srcPenalty,
        dstPenalty,
        penaltyDelta: (dstPenalty ?? 0) - (srcPenalty ?? 0),
      };
    })
    .filter((row): row is EvaluationRow => row != null);
}

/** Whether any row carries a separated penalty. */
export function rowsHavePenalty(rows: EvaluationRow[]): boolean {
  return rows.some(row => row.srcPenalty != null || row.dstPenalty != null);
}

/** "0, 2, 4–6" for [0, 2, 4, 5, 6]. */
export function formatIndexRanges(indices: number[]): string {
  const sorted = [...indices].sort((a, b) => a - b);
  const parts: string[] = [];
  let start = 0;
  for (let i = 1; i <= sorted.length; i++) {
    if (i === sorted.length || sorted[i] !== sorted[i - 1] + 1) {
      const from = sorted[start];
      const to = sorted[i - 1];
      parts.push(
        from === to
          ? String(from)
          : to === from + 1
            ? `${from}, ${to}`
            : `${from}–${to}`,
      );
      start = i;
    }
  }
  return parts.join(', ');
}

/**
 * One line naming several C&O tables, e.g. "Constraints, Objectives at
 * tuple index 0, 2, 4–6". Labels stay as the tables show them.
 */
export function describeTables(
  tables: Array<{
    type: 'CONSTRAINT' | 'OBJECTIVE';
    label: string;
    tupleIndex: number;
  }>,
): string {
  const parts: string[] = [];
  const tupled: number[] = [];
  for (const t of tables) {
    if (t.type === 'OBJECTIVE' && t.label !== 'Objectives') {
      tupled.push(t.tupleIndex);
    } else {
      parts.push(t.label);
    }
  }
  if (tupled.length > 0) {
    parts.push(`Objectives at tuple index ${formatIndexRanges(tupled)}`);
  }
  return parts.join(', ');
}
