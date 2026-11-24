'use client';

import {Box} from '@mui/material';

import Tag from '@/app/components/ui/Tag';
import {COLOR, WEIGHT} from '@/lib/ui-tokens';

import {precise} from '@/lib/format';
import type {
  ExpressionProperty,
  ExpressionPropertyValue,
} from '@/lib/rebalancer-explorer-types';

function renderPropertyValue(value: ExpressionPropertyValue): string {
  if ('valueDouble' in value) {
    return precise(value.valueDouble.value);
  }
  if ('valueString' in value) {
    return value.valueString.value;
  }
  if ('valueInt' in value) {
    return String(value.valueInt.value);
  }
  if ('valueBool' in value) {
    return value.valueBool.value ? 'TRUE' : 'FALSE';
  }
  if ('valuePoint2dList' in value) {
    const points = value.valuePoint2dList.value;
    const shown = points.slice(0, 10);
    const text = shown
      .map(p => `(${precise(p.x)}, ${precise(p.y)})`)
      .join(', ');
    return points.length > 10 ? `${text} and ${points.length - 10} more` : text;
  }
  if ('valueContainerNameList' in value) {
    const names = value.valueContainerNameList.value;
    const shown = names.slice(0, 5);
    const text = shown.join(', ');
    return names.length > 5 ? `${text} and ${names.length - 5} more` : text;
  }
  if ('valueObjectNameDoubleMap' in value) {
    const entries = Object.entries(value.valueObjectNameDoubleMap.value);
    const shown = entries.slice(0, 5);
    const text = shown
      .map(([k, v]) => `${k}: ${precise(v as number)}`)
      .join(', ');
    return entries.length > 5 ? `${text} and ${entries.length - 5} more` : text;
  }
  if ('valueObjectName' in value) {
    return value.valueObjectName.value;
  }
  if ('valueContainerName' in value) {
    return value.valueContainerName.value;
  }
  return '(unknown)';
}

export function PropertyList({properties}: {properties: ExpressionProperty[]}) {
  if (properties.length === 0) {
    return null;
  }

  return (
    // Parameters as small chips under the node: the name, then the value.
    <Box sx={{display: 'flex', flexWrap: 'wrap', gap: 0.5, mt: 0.5}}>
      {properties.map(prop => (
        <Tag key={prop.name} small>
          <Box
            component="b"
            sx={{fontWeight: WEIGHT.semibold, color: COLOR.textTertiary}}>
            {prop.name}
          </Box>
          {renderPropertyValue(prop.value)}
        </Tag>
      ))}
    </Box>
  );
}
