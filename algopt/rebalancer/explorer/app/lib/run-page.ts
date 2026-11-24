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

import {findMetricNameBySlug} from './metric-slug';

function safeDecode(segment: string): string {
  try {
    return decodeURIComponent(segment);
  } catch {
    return segment;
  }
}

/** Keeps the first and last 8 characters of a long id. */
export function middleEllipsis(id: string, keep = 8): string {
  return id.length <= keep * 2 + 1
    ? id
    : `${id.slice(0, keep)}…${id.slice(-keep)}`;
}

/** Page title for a path under `/run/<id>`, using backend names verbatim. */
export function runPageTitle(
  subPath: string,
  metricCollectionNames: readonly string[],
): string {
  const [section, name] = subPath.split('/').filter(s => s.length > 0);
  switch (section) {
    case undefined:
      return 'Summary';
    case 'constraints-objectives':
      return 'Constraints & Objectives';
    case 'local-search-profiling':
      return 'Local Search';
    case 'metrics': {
      const slug = name == null ? '' : safeDecode(name);
      return findMetricNameBySlug(slug, metricCollectionNames) ?? slug;
    }
    case 'entity':
    case 'dynamic-dimensions':
      return name == null ? section : safeDecode(name);
    default:
      return safeDecode(section);
  }
}
