'use client';

import {Fragment, type ReactNode} from 'react';

import {Box, Link as MuiLink, Paper} from '@mui/material';
import Link from 'next/link';

import SectionHeader from '@/app/components/ui/SectionHeader';
import {formatCount} from '@/lib/format';
import {slugifyMetricName} from '@/lib/metric-slug';
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

// Names listed per box before "+N more".
const NAMES_SHOWN = 4;

/** The box's names on one line, each opening its page when it has one. */
function Names({
  names,
  hrefFor,
}: {
  names: readonly string[];
  hrefFor?: (name: string) => string;
}) {
  if (names.length === 0) {
    return <>none</>;
  }
  const shown = names.slice(0, NAMES_SHOWN);
  return (
    <>
      {shown.map((name, i) => (
        <Fragment key={name}>
          {i > 0 && ', '}
          {hrefFor != null ? (
            <MuiLink
              component={Link}
              href={hrefFor(name)}
              title={name}
              sx={{fontFamily: 'var(--font-mono)', fontSize: TEXT['2xs']}}>
              {name}
            </MuiLink>
          ) : (
            <Box
              component="span"
              title={name}
              sx={{fontFamily: 'var(--font-mono)', fontSize: TEXT['2xs']}}>
              {name}
            </Box>
          )}
        </Fragment>
      ))}
      {names.length > shown.length && ` +${names.length - shown.length}`}
    </>
  );
}

/**
 * An entity box: 5px edge in the entity's categorical color, uppercase title,
 * the count, a one-line sub. With `href`, the title opens the entity's page.
 */
function Node({
  label,
  count,
  color,
  href,
  children,
}: {
  label: string;
  count: number;
  color: string;
  href?: string;
  children?: ReactNode;
}) {
  return (
    <Box
      sx={{
        position: 'relative',
        minWidth: 0,
        border: `1px solid ${COLOR.borderStrong}`,
        borderLeft: `5px solid ${color}`,
        borderRadius: radius('lg'),
        px: 1.5,
        py: 1.25,
        bgcolor: COLOR.bgSurface,
        transition: `box-shadow ${DURATION.fast}ms, border-color ${DURATION.fast}ms`,
        ...(href != null && {
          '&:hover': {boxShadow: SHADOW_MD, borderColor: color},
          '&:focus-within': focusRing(-2),
        }),
      }}>
      {href != null ? (
        <MuiLink
          component={Link}
          href={href}
          underline="none"
          sx={{
            ...EYEBROW_TEXT,
            display: 'block',
            '&:focus-visible': {outline: 'none'},
          }}>
          {label}
        </MuiLink>
      ) : (
        <Box sx={EYEBROW_TEXT}>{label}</Box>
      )}
      <Box
        sx={{
          mt: 0.25,
          fontSize: TEXT['2xl'],
          fontWeight: WEIGHT.heading,
          letterSpacing: TRACKING['2xl'],
          lineHeight: 1.3,
          ...TABULAR,
        }}>
        {formatCount(count)}
      </Box>
      <Box
        sx={{
          color: COLOR.textTertiary,
          fontSize: TEXT.xs,
          whiteSpace: 'nowrap',
          overflow: 'hidden',
          textOverflow: 'ellipsis',
        }}>
        {children}
      </Box>
    </Box>
  );
}

/** A labelled connector between two boxes. */
function Edge({label}: {label: string}) {
  return (
    <Box
      aria-hidden
      sx={{
        position: 'relative',
        display: 'grid',
        placeItems: 'center',
        '&::before': {
          content: '""',
          position: 'absolute',
          left: 4,
          right: 4,
          top: '50%',
          borderTop: `1.5px solid ${COLOR.borderStrong}`,
        },
        '@media (max-width: 760px)': {display: 'none'},
      }}>
      <Box
        component="span"
        sx={{
          position: 'relative',
          px: 0.75,
          borderRadius: radius('full'),
          bgcolor: COLOR.bgSubtle,
          color: COLOR.textTertiary,
          fontSize: TEXT['2xs'],
          whiteSpace: 'nowrap',
        }}>
        {label}
      </Box>
    </Box>
  );
}

/**
 * The problem model at a glance, from the run metadata: objects are assigned
 * to containers, containers are grouped by scopes, and objects are grouped by
 * partitions. Each box opens its page.
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
  const dynamicHref = (name: string) =>
    `${basePath}/dynamic-dimensions/${encodeURIComponent(name)}`;
  const metricHref = (name: string) =>
    `${basePath}/metrics/${slugifyMetricName(name)}`;
  // Listed scopes only; metadata.numScopes also counts the containers.
  const scopes = metadata.scopeNames ?? [];
  const partitions = metadata.partitionNames ?? [];
  const dynamicDimensions = metadata.dynamicDimensionNames ?? [];
  const metrics = metadata.metricCollectionNames ?? [];
  return (
    <Paper variant="outlined" sx={{minWidth: 0}}>
      <SectionHeader title="Problem model" meta="Each box opens its page" />
      <Box sx={{px: 2, pb: 2, display: 'grid', gap: 1.5}}>
        <Box
          sx={{
            display: 'grid',
            gridTemplateColumns:
              'minmax(0, 1fr) 96px minmax(0, 1fr) 96px minmax(0, 1fr)',
            '@media (max-width: 760px)': {
              gridTemplateColumns: '1fr',
              gap: 1.5,
            },
          }}>
          <Node
            label="Objects"
            count={metadata.numObjects}
            color={cssColor('cat-1')}
            href={entityHref(metadata.objectName)}>
            <Names names={[metadata.objectName]} hrefFor={entityHref} />
          </Node>
          <Edge label="assigned to" />
          <Node
            label="Containers"
            count={metadata.numContainers}
            color={cssColor('cat-2')}
            href={entityHref(metadata.containerName)}>
            <Names names={[metadata.containerName]} hrefFor={entityHref} />
          </Node>
          <Edge label="grouped by" />
          <Node label="Scopes" count={scopes.length} color={cssColor('cat-3')}>
            <Names names={scopes} hrefFor={entityHref} />
          </Node>
        </Box>
        <Box
          sx={{
            display: 'grid',
            gridTemplateColumns: 'repeat(4, minmax(0, 1fr))',
            gap: 1.5,
            '@media (max-width: 1180px)': {
              gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
            },
            '@media (max-width: 760px)': {gridTemplateColumns: '1fr'},
          }}>
          <Node
            label="Partitions"
            count={partitions.length}
            color={cssColor('cat-4')}>
            <Names names={partitions} />
          </Node>
          <Node
            label="Dimensions"
            count={metadata.numDimensions}
            color={cssColor('cat-5')}>
            measured per object and container
          </Node>
          <Node
            label="Dynamic dims"
            count={dynamicDimensions.length}
            color={cssColor('cat-6')}>
            <Names names={dynamicDimensions} hrefFor={dynamicHref} />
          </Node>
          <Node
            label="Metrics"
            count={metrics.length}
            color={cssColor('cat-7')}>
            <Names names={metrics} hrefFor={metricHref} />
          </Node>
        </Box>
      </Box>
    </Paper>
  );
}
