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

import {middleEllipsis, runPageTitle} from '@/lib/run-page';

describe('runPageTitle', () => {
  const metrics = ['Scope Item Utilization Values', 'Other'];

  it.each([
    ['', 'Summary'],
    ['/', 'Summary'],
    ['/constraints-objectives', 'Constraints & Objectives'],
    ['/local-search-profiling', 'Local Search'],
    ['/entity/placement', 'placement'],
    ['/entity/scope%3Aserver', 'scope:server'],
    ['/dynamic-dimensions/ram_bytes', 'ram_bytes'],
    ['/metrics/Scope-Item-Utilization-Values', 'Scope Item Utilization Values'],
    ['/metrics/unknown-slug', 'unknown-slug'],
  ])('%s -> %s', (subPath, title) => {
    expect(runPageTitle(subPath, metrics)).toBe(title);
  });
});

describe('middleEllipsis', () => {
  it('keeps short ids whole', () => {
    expect(middleEllipsis('abc')).toBe('abc');
    expect(middleEllipsis('0123456789abcdefg')).toBe('0123456789abcdefg');
  });

  it('keeps the first and last 8 characters of long ids', () => {
    expect(middleEllipsis('34c2b71a-0de3-4d19-a585-abefe38b3937')).toBe(
      '34c2b71a…e38b3937',
    );
  });
});
