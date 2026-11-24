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

import {formatCount, formatRuntime, toTitleCase} from '@/lib/format';

describe('toTitleCase', () => {
  it('converts UPPER_SNAKE_CASE to Title Case', () => {
    expect(toTitleCase('TOTAL_UTILIZATION')).toBe('Total Utilization');
  });

  it('handles single-word UPPER case', () => {
    expect(toTitleCase('UTILIZATION')).toBe('Utilization');
  });

  it('preserves space-separated names that are already Title Case', () => {
    expect(toTitleCase('Container Utilizations')).toBe(
      'Container Utilizations',
    );
  });

  it('lower-cases trailing characters of each space-separated word', () => {
    expect(toTitleCase('CONTAINER UTILIZATIONS')).toBe(
      'Container Utilizations',
    );
  });

  it('handles mixed underscores and spaces', () => {
    expect(toTitleCase('CONTAINER_GROUP UTILIZATIONS')).toBe(
      'Container Group Utilizations',
    );
  });

  it('collapses repeated separators without producing empty words', () => {
    expect(toTitleCase('FOO__BAR')).toBe('Foo Bar');
    expect(toTitleCase('FOO  BAR')).toBe('Foo Bar');
    expect(toTitleCase('FOO _ BAR')).toBe('Foo Bar');
  });

  it('strips leading and trailing separators', () => {
    expect(toTitleCase('_FOO_BAR_')).toBe('Foo Bar');
    expect(toTitleCase('  foo bar  ')).toBe('Foo Bar');
  });

  it('returns an empty string for empty input', () => {
    expect(toTitleCase('')).toBe('');
  });

  it('returns an empty string when input is only separators', () => {
    expect(toTitleCase('___')).toBe('');
    expect(toTitleCase('   ')).toBe('');
  });

  it('lower-cases mixed-case input within each word', () => {
    expect(toTitleCase('totalUtilization')).toBe('Totalutilization');
    expect(toTitleCase('total_Utilization')).toBe('Total Utilization');
  });

  it('handles tabs and newlines as whitespace separators', () => {
    expect(toTitleCase('FOO\tBAR')).toBe('Foo Bar');
    expect(toTitleCase('FOO\nBAR')).toBe('Foo Bar');
  });
});

describe('formatCount', () => {
  it('groups thousands', () => {
    expect(formatCount(5225)).toBe('5,225');
    expect(formatCount(12)).toBe('12');
    expect(formatCount(1234567)).toBe('1,234,567');
  });
});

describe('formatRuntime', () => {
  it.each([
    [0, '0 s'],
    [5.974290405, '5.97 s'],
    [0.5, '0.5 s'],
    [59.999, '60 s'],
    [60, '1 m'],
    [3723.44, '1 h 2 m 3.4 s'],
    [7200, '2 h'],
  ])('%s -> %s', (seconds, text) => {
    expect(formatRuntime(seconds)).toBe(text);
  });
});
