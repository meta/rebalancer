import {describe, expect, it} from 'vitest';

import {
  formatNumber,
  valueTickFormatter,
} from '@/app/components/profiling/LocalSearchProfileCard';

type Rows = Parameters<typeof valueTickFormatter>[0];

describe('valueTickFormatter', () => {
  it('keeps formatNumber for spans of 0.01 and more', () => {
    const rows: Rows = [
      {time: 0, SINGLE: 0},
      {time: 1, SINGLE: 264},
    ];
    expect(valueTickFormatter(rows)).toBe(formatNumber);
  });

  it('keeps formatNumber for a flat series', () => {
    expect(valueTickFormatter([{time: 0, SINGLE: 3.1}])).toBe(formatNumber);
  });

  it('prints enough decimals to tell ticks apart on a tiny span', () => {
    const rows: Rows = [
      {time: 0, SINGLE: 3.113754638},
      {time: 1, SINGLE: null},
      {time: 2, SWAP: 3.113753959},
    ];
    const format = valueTickFormatter(rows);
    expect(format(3.113754638)).toBe('3.113754638');
    expect(format(3.1137535)).not.toBe(format(3.1137545));
  });
});
