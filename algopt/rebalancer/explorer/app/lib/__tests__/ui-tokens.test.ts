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

import {readFileSync} from 'node:fs';

import {describe, expect, it} from 'vitest';

import {
  CONTROL,
  DURATION,
  FONT,
  OVERLAY,
  RADIUS,
  SEMANTIC_COLORS,
  SHADOW,
  SPACE,
  TEXT,
  WEIGHT,
  Z,
} from '@/lib/ui-tokens';

const css = readFileSync(
  new URL('../../app/globals.css', import.meta.url),
  'utf8',
);

/** Declarations of the first rule whose selector is exactly `selector`. */
function block(selector: string): Map<string, string> {
  const escaped = selector.replace(/[[\]"=]/g, m => `\\${m}`);
  const body =
    css.match(new RegExp(`(?:^|\\n)${escaped}\\s*{([^}]*)}`))?.[1] ?? '';
  const decls = new Map<string, string>();
  for (const m of body.matchAll(/--([\w-]+):\s*([^;]+);/g)) {
    decls.set(m[1], m[2].replace(/\s+/g, ' ').trim());
  }
  return decls;
}

const root = block(':root');
const dark = block('[data-theme="dark"]');

describe('globals.css declares the design-language tokens of lib/ui-tokens.ts', () => {
  it.each(Object.entries(SEMANTIC_COLORS))('--color-%s', (name, value) => {
    expect(root.get(`color-${name}`)).toBe(value.light);
    // Dark mode swaps only what differs.
    expect(dark.get(`color-${name}`)).toBe(
      value.dark === value.light ? undefined : value.dark,
    );
  });

  it('has no dark value without a token', () => {
    for (const name of dark.keys()) {
      expect(name.replace(/^color-/, '') in SEMANTIC_COLORS).toBe(true);
    }
  });

  it('sizes, weights, radii, controls, shadows, motion and layers', () => {
    const expected: Array<[string, string]> = [
      ...Object.entries(TEXT).map(([k, v]): [string, string] => [
        `text-${k}`,
        `${v}px`,
      ]),
      ...Object.entries(WEIGHT).map(([k, v]): [string, string] => [
        `weight-${k}`,
        String(v),
      ]),
      ...Object.entries(SPACE).map(([k, v]): [string, string] => [
        `space-${k.replace('.', '-')}`,
        `${v}px`,
      ]),
      ...(['xs', 'sm', 'md', 'lg', 'xl', 'full'] as const).map(
        (k): [string, string] => [`radius-${k}`, `${RADIUS[k]}px`],
      ),
      ...Object.entries(CONTROL).map(([k, v]): [string, string] => [
        `control-${k}`,
        `${v}px`,
      ]),
      ...Object.entries(SHADOW).map(([k, v]): [string, string] => [
        `shadow-${k}`,
        v,
      ]),
      ...Object.entries(DURATION).map(([k, v]): [string, string] => [
        `duration-${k}`,
        `${v}ms`,
      ]),
      ...Object.entries(Z).map(([k, v]): [string, string] => [
        `z-${k}`,
        String(v),
      ]),
      ['overlay-scrim', OVERLAY.scrim],
      ['overlay-modal', OVERLAY.modal],
      ['font-sans', FONT.sans],
      ['font-mono', FONT.mono],
    ];
    for (const [name, value] of expected) {
      expect([name, root.get(name)]).toEqual([name, value]);
    }
  });

  it('keeps the legacy names as aliases of the semantic tokens', () => {
    const aliases = block(':root,\n[data-theme="dark"]');
    expect(aliases.get('background')).toBe('var(--color-bg-canvas)');
    expect(aliases.get('rbx-accent')).toBe('var(--color-action)');
    expect(aliases.get('rbx-surface')).toBe('var(--color-bg-surface)');
  });

  it('follows the theme attribute, not the OS', () => {
    expect(css).not.toContain('prefers-color-scheme');
  });
});
