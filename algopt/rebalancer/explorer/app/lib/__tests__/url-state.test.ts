/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import {describe, expect, it} from 'vitest';

import {parseQs, stringifyQs} from '@/lib/url-state';

// The browser hands the query string to `parseQs` through URLSearchParams,
// which decodes it once and re-encodes it, so round-trips go through it too.
function roundTrip(obj: Record<string, unknown>): ReturnType<typeof parseQs> {
  return parseQs(new URLSearchParams(stringifyQs(obj)));
}

function overrides(n: number): Array<{variable: string; container: string}> {
  return Array.from({length: n}, (_, i) => ({
    variable: `object_${i}`,
    container: `container_${i}`,
  }));
}

describe('stringifyQs / parseQs', () => {
  it.each([25, 100])('round-trips %i overrides', n => {
    const srcOverrides = overrides(n);
    expect(roundTrip({srcOverrides}).srcOverrides).toEqual(srcOverrides);
  });

  it('round-trips 270 column names', () => {
    const showColumns = Array.from({length: 270}, (_, i) => `column_${i}`);
    expect(roundTrip({showColumns}).showColumns).toEqual(showColumns);
  });

  it('round-trips values with URL-reserved characters', () => {
    const evalFilters = [
      {rules: [{regex: {column: 'Name', regex: 'a+b&c#d%e=f [x]'}}]},
    ];
    expect(roundTrip({evalFilters, msScope: 'a b'})).toEqual({
      evalFilters,
      msScope: 'a b',
    });
  });

  it('keeps keys readable', () => {
    expect(stringifyQs({srcOverrides: overrides(1)})).toBe(
      'srcOverrides[0][variable]=object_0&srcOverrides[0][container]=container_0',
    );
  });

  it('parses links written before values were encoded', () => {
    const legacy =
      'dstBase=FINAL&srcOverrides[0][variable]=host.example.com:1&' +
      'srcOverrides[0][container]=pool/a&msObjectives[0]=Spread Goal&' +
      'filters[0][rules][0][stringAny][column]=Name&' +
      'filters[0][rules][0][stringAny][values][0]=x';
    expect(parseQs(new URLSearchParams(legacy))).toEqual({
      dstBase: 'FINAL',
      srcOverrides: [{variable: 'host.example.com:1', container: 'pool/a'}],
      msObjectives: ['Spread Goal'],
      filters: [{rules: [{stringAny: {column: 'Name', values: ['x']}}]}],
    });
  });
});
