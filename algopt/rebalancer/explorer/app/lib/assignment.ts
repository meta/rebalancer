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

import type {UIAssignment} from '@/app/components/evaluation/AssignmentCard';
import type {Assignment, AssignmentBase} from './rebalancer-explorer-types';

const BASE_MAP: Record<UIAssignment['base'], AssignmentBase> = {
  INITIAL: 0,
  FINAL: 1,
  INTERMEDIATE: 2,
};

/**
 * Short name of an assignment for column headers and the detail pane:
 * "Initial", "Final" or "Step 12", plus "+3 moves" when it has overrides.
 */
export function assignmentShortLabel(ui: UIAssignment): string {
  const base =
    ui.base === 'INITIAL'
      ? 'Initial'
      : ui.base === 'FINAL'
        ? 'Final'
        : `Step ${ui.step ?? 0}`;
  const n = ui.overrides.length;
  return n === 0 ? base : `${base} +${n} ${n === 1 ? 'move' : 'moves'}`;
}

export function toThriftAssignment(ui: UIAssignment): Assignment {
  const overrides: Record<string, string> = {};
  for (const o of ui.overrides) {
    overrides[o.variable] = o.container;
  }
  return {
    base: BASE_MAP[ui.base],
    variableToContainerOverride: overrides,
    ...(ui.base === 'INTERMEDIATE' && ui.step != null
      ? {searchStep: ui.step}
      : {}),
  };
}
