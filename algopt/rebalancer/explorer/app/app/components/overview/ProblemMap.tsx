'use client';

import type {ReactNode} from 'react';

import {Box, Button, Paper} from '@mui/material';
import Link from 'next/link';

import SectionHeader from '@/app/components/ui/SectionHeader';
import {formatCount} from '@/lib/format';
import type {ProblemMetadata} from '@/lib/rebalancer-explorer-types';
import {
  COLOR,
  cssColor,
  DURATION,
  EYEBROW_TEXT,
  focusRing,
  radius,
  SHADOW_MD,
  TABULAR,
  TEXT,
  TRACKING,
  WEIGHT,
} from '@/lib/ui-tokens';

const STACKED = '@media (max-width: 760px)';

/**
 * An entity box: 5px edge in the entity's categorical color, uppercase title,
 * the count and a one-line sub. With `href`, the whole box opens its page.
 */
function Node({
  label,
  count,
  sub,
  color,
  href,
}: {
  label: string;
  count: number;
  sub: string;
  color: string;
  href?: string;
}) {
  const content: ReactNode = (
    <>
      <Box sx={EYEBROW_TEXT}>{label}</Box>
      <Box
        sx={{
          mt: 0.25,
          fontSize: TEXT['2xl'],
          fontWeight: WEIGHT.bold,
          letterSpacing: TRACKING['2xl'],
          lineHeight: 1.3,
          ...TABULAR,
        }}>
        {formatCount(count)}
      </Box>
      <Box
        title={sub}
        sx={{
          color: COLOR.textTertiary,
          fontSize: TEXT.xs,
          whiteSpace: 'nowrap',
          overflow: 'hidden',
          textOverflow: 'ellipsis',
        }}>
        {sub}
      </Box>
    </>
  );
  const sx = {
    display: 'block',
    minWidth: 0,
    border: `1px solid ${COLOR.borderStrong}`,
    borderLeft: `5px solid ${color}`,
    borderRadius: radius('lg'),
    px: 1.5,
    py: 1.25,
    bgcolor: COLOR.bgSurface,
    color: COLOR.textPrimary,
    textDecoration: 'none',
    transition: `box-shadow ${DURATION.fast}ms, border-color ${DURATION.fast}ms`,
  };
  if (href == null) {
    return <Box sx={sx}>{content}</Box>;
  }
  return (
    <Box
      component={Link}
      href={href}
      sx={{
        ...sx,
        '&:hover': {boxShadow: SHADOW_MD, borderColor: color},
        '&:focus-visible': focusRing(-2),
      }}>
      {content}
    </Box>
  );
}

const EDGE_LABEL = {
  position: 'relative',
  px: 0.75,
  borderRadius: radius('full'),
  bgcolor: COLOR.bgSubtle,
  color: COLOR.textTertiary,
  fontSize: TEXT['2xs'],
  whiteSpace: 'nowrap',
} as const;

/** A labelled connector between two boxes, across or down. */
function Edge({label, down = false}: {label: string; down?: boolean}) {
  return (
    <Box
      aria-hidden
      sx={{
        position: 'relative',
        display: 'grid',
        placeItems: 'center',
        '&::before': down
          ? {
              content: '""',
              position: 'absolute',
              top: 2,
              bottom: 2,
              left: '50%',
              borderLeft: `1.5px solid ${COLOR.borderStrong}`,
            }
          : {
              content: '""',
              position: 'absolute',
              left: 4,
              right: 4,
              top: '50%',
              borderTop: `1.5px solid ${COLOR.borderStrong}`,
            },
        [STACKED]: {display: 'none'},
      }}>
      <Box component="span" sx={EDGE_LABEL}>
        {label}
      </Box>
    </Box>
  );
}

function names(list: readonly string[]): string {
  return list.length > 0 ? list.join(', ') : 'none';
}

/**
 * The problem model at a glance, from the run metadata: objects are assigned
 * to containers, which scopes group; partitions group objects, dimensions
 * measure objects and containers, and dynamic dimensions are kept per
 * partition and scope. A box with a page opens it.
 */
export default function ProblemMap({
  basePath,
  metadata,
}: {
  basePath: string;
  metadata: ProblemMetadata;
}) {
  const entityHref = (name: string) =>
    `${basePath}/entity/${encodeURIComponent(name)}`;
  // Listed scopes only; metadata.numScopes also counts the containers.
  const scopes = metadata.scopeNames ?? [];
  const partitions = metadata.partitionNames ?? [];
  const dynamicDimensions = metadata.dynamicDimensionNames ?? [];
  const objectsHref = entityHref(metadata.objectName);
  return (
    <Paper variant="outlined" sx={{minWidth: 0}}>
      <SectionHeader
        title="Problem at a glance"
        meta="Click any box to explore"
        actions={
          <Button
            size="small"
            variant="outlined"
            component={Link}
            href={objectsHref}>
            Open model
          </Button>
        }
      />
      <Box
        sx={{
          px: 2,
          pb: 2,
          display: 'grid',
          gridTemplateColumns:
            'minmax(0, 1fr) 64px minmax(0, 1fr) 64px minmax(0, 1fr)',
          gridTemplateRows: 'auto 34px auto',
          [STACKED]: {
            gridTemplateColumns: '1fr',
            gridTemplateRows: 'none',
            gap: 1.5,
            '& > .gap': {display: 'none'},
          },
        }}>
        <Node
          label="Objects"
          count={metadata.numObjects}
          sub={metadata.objectName}
          color={cssColor('cat-1')}
          href={objectsHref}
        />
        <Edge label="assigned to" />
        <Node
          label="Containers"
          count={metadata.numContainers}
          sub={metadata.containerName}
          color={cssColor('cat-2')}
          href={entityHref(metadata.containerName)}
        />
        <Edge label="grouped by" />
        <Node
          label="Scopes"
          count={scopes.length}
          sub={names(scopes)}
          color={cssColor('cat-3')}
          href={scopes.length > 0 ? entityHref(scopes[0]) : undefined}
        />
        <Edge label="group objects" down />
        <Box className="gap" />
        <Edge label="measured on" down />
        <Box className="gap" />
        <Edge label="per partition × scope" down />
        <Node
          label="Partitions"
          count={partitions.length}
          sub={names(partitions)}
          color={cssColor('cat-5')}
          href={
            partitions.length > 0
              ? `${basePath}/partition/${encodeURIComponent(partitions[0])}`
              : undefined
          }
        />
        <Box className="gap" />
        <Node
          label="Dimensions"
          count={metadata.numDimensions}
          sub="measured per object and container"
          color={cssColor('cat-4')}
        />
        <Box className="gap" />
        <Node
          label="Dynamic dims"
          count={dynamicDimensions.length}
          sub={names(dynamicDimensions)}
          color={cssColor('cat-6')}
          href={
            dynamicDimensions.length > 0
              ? `${basePath}/dynamic-dimensions/${encodeURIComponent(dynamicDimensions[0])}`
              : undefined
          }
        />
      </Box>
    </Paper>
  );
}
