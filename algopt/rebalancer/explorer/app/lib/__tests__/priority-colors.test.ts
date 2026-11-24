import {describe, expect, it} from 'vitest';

import {contrastWithWhite, tupleColor} from '@/lib/priority-colors';

function parse(rgb: string): [number, number, number] {
  const m = /^rgb\((\d+), (\d+), (\d+)\)$/.exec(rgb);
  if (m == null) {
    throw new Error(`not an rgb() color: ${rgb}`);
  }
  return [Number(m[1]), Number(m[2]), Number(m[3])];
}

describe('tupleColor', () => {
  it('keeps a white label readable (WCAG AA) at every point of the ramp', () => {
    for (let max = 0; max <= 40; max++) {
      for (let i = 0; i <= max; i++) {
        expect(
          contrastWithWhite(parse(tupleColor(i, max))),
        ).toBeGreaterThanOrEqual(4.5);
      }
    }
  });

  it('starts at dark indigo for the most important tuple index', () => {
    expect(tupleColor(0, 6)).toBe('rgb(49, 46, 129)');
    expect(tupleColor(0, 0)).toBe('rgb(49, 46, 129)');
  });

  it('gives one index the same color for the same run size', () => {
    expect(tupleColor(3, 6)).toBe(tupleColor(3, 6));
    expect(tupleColor(1, 6)).not.toBe(tupleColor(5, 6));
  });

  it('clamps indexes outside the run range to the ends of the ramp', () => {
    expect(tupleColor(9, 6)).toBe(tupleColor(6, 6));
    expect(tupleColor(-1, 6)).toBe(tupleColor(0, 6));
  });
});
