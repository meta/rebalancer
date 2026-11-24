import {isHighlightColumn} from '@/lib/format';
import type {
  CellData,
  ColumnDescription,
} from '@/lib/rebalancer-explorer-types';
import {ColumnType} from '@/lib/rebalancer-explorer-types';

export const NAN_STRING = 'NaN';

/**
 * Extract a display value from a cell + column description.
 * For double-like columns where doubleValue is null, returns NAN_STRING.
 */
export function extractCellValue(
  cell: CellData | undefined,
  colDesc: ColumnDescription,
): string | number {
  if (cell == null) {
    return '-';
  }
  if (cell.doubleValue != null) {
    return cell.doubleValue;
  }
  if (isHighlightColumn(colDesc.type) && cell.doubleValue == null) {
    return NAN_STRING;
  }
  if (cell.stringValue != null) {
    // For INTEGER columns, parse the string as a number so it renders with
    // monospace formatting consistent with the right-aligned column layout.
    if (colDesc.type === ColumnType.INTEGER) {
      const parsed = Number(cell.stringValue);
      if (!Number.isNaN(parsed)) {
        return parsed;
      }
    }
    return cell.stringValue;
  }
  return '-';
}

export function formatCellValue(value: string | number): string {
  if (typeof value === 'number') {
    if (Number.isInteger(value)) {
      return String(value);
    }
    // Four decimals would print a tiny nonzero change as "0.0000" or
    // "-0.0000", which reads as no change; show its significant digits.
    if (Math.abs(value) < 0.0001) {
      return value.toPrecision(4);
    }
    return value.toFixed(4);
  }
  return value;
}
