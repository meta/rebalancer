'use client';

import {forwardRef, useContext} from 'react';

import {
  Alert,
  Box,
  Button,
  CircularProgress,
  MenuItem,
  Select,
  Typography,
} from '@mui/material';
import {TreeItem} from '@mui/x-tree-view/TreeItem';
import type {TreeItemProps} from '@mui/x-tree-view/TreeItem';

import {precise} from '@/lib/format';
import {COLOR, CONTROL, radius, TABULAR, TEXT, WEIGHT} from '@/lib/ui-tokens';
import type {
  OrderDirection,
  TreeNodeOrderMetric,
} from '@/lib/rebalancer-explorer-types';

import CollapsibleText from '../CollapsibleText';

import {
  CONTROLS_MORE_PREFIX,
  CONTROLS_PREFIX,
  DEFAULT_SORT,
  PLACEHOLDER_SUFFIX,
  SHOW_MORE_PREFIX,
  SORT_CONTROLS_PREFIX,
} from './constants';
import {NodeDelta} from './NodeDelta';
import {NodeSearchInput} from './NodeSearchInput';
import {PropertyList} from './PropertyList';
import {TreeDataContext} from './TreeDataContext';

// Small standard select for the per-node "Order by" controls.
const SORT_SELECT_SX = {
  minWidth: 140,
  height: CONTROL.sm,
  fontSize: TEXT.xs,
  color: COLOR.textPrimary,
  '& .MuiSelect-select': {py: 0.25, pl: 1},
} as const;

/**
 * The operator glyph of an expression node: mono, in the alternative
 * (structure) color.
 */
function operatorGlyph(type: string): string {
  if (/Sum/i.test(type)) {
    return 'Σ';
  }
  if (/Product|Multiply/i.test(type)) {
    return '×';
  }
  if (/Max|Largest/i.test(type)) {
    return 'max';
  }
  if (/Min|Smallest/i.test(type)) {
    return 'min';
  }
  if (/Power|Exp/i.test(type)) {
    return 'xⁿ';
  }
  if (/Lookup/i.test(type)) {
    return '[ ]';
  }
  if (/Partition|Group/i.test(type)) {
    return '{ }';
  }
  if (/Vector/i.test(type)) {
    return 'v';
  }
  if (/Variable/i.test(type)) {
    return 'x';
  }
  if (/Piecewise|Step|Threshold/i.test(type)) {
    return 'f';
  }
  if (/Constant/i.test(type)) {
    return 'c';
  }
  return type.slice(0, 2).toLowerCase();
}

function OperatorIcon({type}: {type: string}) {
  return (
    <Box
      component="span"
      aria-hidden
      sx={{
        flexShrink: 0,
        minWidth: 22,
        height: 18,
        px: 0.5,
        display: 'inline-grid',
        placeItems: 'center',
        borderRadius: radius('xs'),
        bgcolor: COLOR.altBg,
        color: COLOR.altText,
        fontFamily: 'var(--font-mono)',
        fontSize: TEXT['2xs'],
        fontWeight: WEIGHT.bold,
        lineHeight: 1,
      }}>
      {operatorGlyph(type)}
    </Box>
  );
}

export const CustomTreeItem = forwardRef<HTMLLIElement, TreeItemProps>(
  function CustomTreeItem(props, ref) {
    const {itemId, ...rest} = props;
    const {
      nodeData,
      nodeMeta,
      expandedItems,
      onShowMore,
      nodeSearch,
      onSearchChange,
      nodeSort,
      onSortChange,
    } = useContext(TreeDataContext);

    // Legacy prefixes (kept for safety, shouldn't be reached)
    if (
      itemId.startsWith(SHOW_MORE_PREFIX) ||
      itemId.startsWith(SORT_CONTROLS_PREFIX)
    ) {
      return <TreeItem {...rest} ref={ref} itemId={itemId} label={null} />;
    }

    // Combined controls row: "Show more" + sort selectors
    if (
      itemId.startsWith(CONTROLS_PREFIX) ||
      itemId.startsWith(CONTROLS_MORE_PREFIX)
    ) {
      const hasMore = itemId.startsWith(CONTROLS_MORE_PREFIX);
      const parentId = itemId.slice(
        hasMore ? CONTROLS_MORE_PREFIX.length : CONTROLS_PREFIX.length,
      );
      const currentSort = nodeSort[parentId] ?? DEFAULT_SORT;
      const cached = nodeData[parentId];
      const hasSort = cached?.children != null && cached.children.length > 1;

      return (
        <TreeItem
          {...rest}
          ref={ref}
          itemId={itemId}
          className="tree-controls-node"
          label={
            <div onClick={e => e.stopPropagation()}>
              <Box
                sx={{display: 'flex', alignItems: 'center', gap: 1, py: 0.5}}>
                {hasMore && (
                  <Button
                    size="small"
                    variant="text"
                    onClick={e => {
                      e.stopPropagation();
                      onShowMore(parentId);
                    }}>
                    Show more
                  </Button>
                )}
                {hasSort && (
                  <>
                    <Typography
                      variant="caption"
                      color="text.secondary"
                      sx={{ml: hasMore ? 1 : 0}}>
                      Order by:
                    </Typography>
                    <Select
                      size="small"
                      value={currentSort.metric}
                      onChange={e =>
                        onSortChange(
                          parentId,
                          Number(e.target.value) as TreeNodeOrderMetric,
                          currentSort.direction,
                        )
                      }
                      sx={SORT_SELECT_SX}>
                      <MenuItem value={0}>Source value</MenuItem>
                      <MenuItem value={1}>Destination value</MenuItem>
                      <MenuItem value={2}>Delta value</MenuItem>
                    </Select>
                    <Select
                      size="small"
                      value={currentSort.direction}
                      onChange={e =>
                        onSortChange(
                          parentId,
                          currentSort.metric,
                          Number(e.target.value) as OrderDirection,
                        )
                      }
                      sx={{...SORT_SELECT_SX, minWidth: 110}}>
                      <MenuItem value={0}>Ascending</MenuItem>
                      <MenuItem value={1}>Descending</MenuItem>
                    </Select>
                  </>
                )}
              </Box>
            </div>
          }
        />
      );
    }

    // Loading placeholder
    if (itemId.endsWith(PLACEHOLDER_SUFFIX)) {
      return (
        <TreeItem
          {...rest}
          ref={ref}
          itemId={itemId}
          label={
            <Box sx={{display: 'flex', alignItems: 'center', py: 0.5}}>
              <CircularProgress size={14} sx={{mr: 1}} />
              <Typography variant="body2" color="text.secondary">
                Loading...
              </Typography>
            </Box>
          }
        />
      );
    }

    // Regular node
    const cached = nodeData[itemId];
    const meta = nodeMeta[itemId];

    // No data yet — show loading or error
    if (cached?.node == null) {
      return (
        <TreeItem
          {...rest}
          ref={ref}
          itemId={itemId}
          label={
            cached?.loading ? (
              <Box sx={{display: 'flex', alignItems: 'center', py: 0.5}}>
                <CircularProgress size={14} sx={{mr: 1}} />
                <Typography variant="body2" color="text.secondary">
                  Loading...
                </Typography>
              </Box>
            ) : cached?.error != null ? (
              <Alert severity="error" sx={{my: 0.5}}>
                {cached.error}
              </Alert>
            ) : (
              <Typography variant="body2" color="text.secondary">
                ...
              </Typography>
            )
          }
        />
      );
    }

    const node = cached.node;
    const coeff = meta?.coefficient ?? 1;
    const isMinimizing = meta?.minimizing ?? true;
    const sourceValue = coeff * node.sourceValue;
    const destinationValue = coeff * node.destinationValue;
    const isExpanded = expandedItems.includes(itemId);
    const isLeaf = cached.fetched && cached.children.length === 0;

    return (
      <TreeItem
        {...rest}
        ref={ref}
        itemId={itemId}
        label={
          <Box>
            {/* Node heading + inline search */}
            <Box
              sx={{
                display: 'flex',
                alignItems: 'center',
                flexWrap: 'wrap',
                columnGap: 0.75,
                py: 0.5,
              }}>
              <OperatorIcon type={node.expressionType} />
              <Box
                component="span"
                sx={{
                  fontFamily: 'var(--font-mono)',
                  fontSize: TEXT.xs,
                  color: COLOR.altText,
                }}>
                {node.expressionType}
              </Box>
              {coeff !== 1 && (
                <Box
                  component="span"
                  title="Coefficient in its parent"
                  sx={{
                    px: 0.5,
                    borderRadius: radius('xs'),
                    bgcolor: COLOR.altBg,
                    color: COLOR.altText,
                    fontSize: TEXT['2xs'],
                    fontWeight: WEIGHT.semibold,
                    ...TABULAR,
                  }}>
                  × {precise(coeff).replace(/-/g, '\u2212')}
                </Box>
              )}
              <Typography
                component="span"
                variant="subtitle2"
                sx={{ml: 0.75, color: COLOR.textTertiary, ...TABULAR}}>
                {precise(sourceValue).replace(/-/g, '\u2212')}
              </Typography>
              <NodeDelta
                sourceValue={sourceValue}
                destinationValue={destinationValue}
                minimizing={isMinimizing}
              />
              {isExpanded && (
                <NodeSearchInput
                  itemId={itemId}
                  nodeSearch={nodeSearch}
                  onSearchChange={onSearchChange}
                />
              )}
            </Box>
            {/* Description */}
            {node.description !== '' && (
              <Box
                sx={{color: COLOR.textSecondary}}
                onClick={e => e.stopPropagation()}>
                <CollapsibleText text={node.description} clampLines={3} />
              </Box>
            )}
            {/* Properties (shown when expanded, or always for leaf nodes) */}
            {node.properties != null &&
              node.properties.length > 0 &&
              (isExpanded || isLeaf) && (
                <PropertyList properties={node.properties} />
              )}
            {/* Updating indicator */}
            {cached.loading && (
              <Box sx={{display: 'flex', alignItems: 'center', py: 0.5}}>
                <CircularProgress size={14} sx={{mr: 0.5}} />
                <Typography variant="caption" color="text.secondary">
                  Updating...
                </Typography>
              </Box>
            )}
          </Box>
        }
      />
    );
  },
);
