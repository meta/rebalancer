import {describe, expect, it} from 'vitest';

import {formatCellValue} from '@/app/components/evaluation/MoveSetsTable.cells';

describe('formatCellValue', () => {
  it('prints integers and strings as they are', () => {
    expect(formatCellValue(3)).toBe('3');
    expect(formatCellValue(-1)).toBe('-1');
    expect(formatCellValue('p_123')).toBe('p_123');
  });

  it('keeps four decimals for ordinary values', () => {
    expect(formatCellValue(0.5)).toBe('0.5000');
    expect(formatCellValue(-12.34567)).toBe('-12.3457');
  });

  it('shows the significant digits of a tiny nonzero change', () => {
    expect(formatCellValue(-2.98e-9)).toBe('-2.980e-9');
    expect(formatCellValue(0.00004321)).toBe('0.00004321');
  });
});
