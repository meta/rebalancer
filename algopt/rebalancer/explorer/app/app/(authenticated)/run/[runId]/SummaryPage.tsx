'use client';

import type {ReactNode} from 'react';

import {Box, Link, Paper, Tooltip, Typography} from '@mui/material';
import {ExternalLink} from 'lucide-react';
import {useParams} from 'next/navigation';

import BundleExpirationButton from '@/app/components/BundleExpirationButton';
import {constraintsObjectivesHref} from '@/app/components/overview/links';
import OverviewTiles from '@/app/components/overview/OverviewTiles';
import PriorityLadder from '@/app/components/overview/PriorityLadder';
import ProblemMap from '@/app/components/overview/ProblemMap';
import {useRunOverviewData} from '@/app/components/overview/useRunOverviewData';
import PageHead, {PageContainer} from '@/app/components/shell/PageHead';
import {endReasonTone} from '@/app/components/shell/RunStatus';
import CopyButton from '@/app/components/ui/CopyButton';
import Pill from '@/app/components/ui/Pill';
import SectionHeader from '@/app/components/ui/SectionHeader';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {useRebalancerHandle} from '@/lib/contexts/RebalancerHandleContext';
import {formatCount, formatRuntime} from '@/lib/format';
import {decodeRunId, encodeRunId} from '@/lib/run-id';
import {COLOR, MONO_TEXT} from '@/lib/ui-tokens';
import {tupperwareTaskLink} from '@platform/internal-links';

/** One key–value row: tertiary key, primary value. */
function Field({label, children}: {label: string; children: ReactNode}) {
  return (
    <Box
      sx={{
        display: 'grid',
        gridTemplateColumns: '160px minmax(0, 1fr)',
        alignItems: 'center',
        columnGap: 2,
        minHeight: 28,
        '@media (max-width: 760px)': {gridTemplateColumns: '1fr'},
      }}>
      <Typography
        component="dt"
        variant="body2"
        sx={{color: COLOR.textTertiary}}>
        {label}
      </Typography>
      <Typography
        component="dd"
        variant="body2"
        sx={{m: 0, minWidth: 0, overflowWrap: 'anywhere'}}>
        {children}
      </Typography>
    </Box>
  );
}

function FieldGroup({title, children}: {title: string; children: ReactNode}) {
  return (
    <Box sx={{minWidth: 0}}>
      <Typography
        variant="overline"
        component="h3"
        sx={{display: 'block', mb: 0.75}}>
        {title}
      </Typography>
      <Box
        component="dl"
        sx={{m: 0, display: 'flex', flexDirection: 'column', rowGap: 0.75}}>
        {children}
      </Box>
    </Box>
  );
}

export default function SummaryPage() {
  const {metadata} = useProblemMetadata();
  const {handle} = useRebalancerHandle();
  const params = useParams<{runId: string}>();
  const {summary, moveSetCount, moveSetDeltas} = useRunOverviewData(
    handle,
    metadata,
  );

  // ProblemMetadataProvider renders children only once metadata has loaded.
  if (metadata == null) {
    return null;
  }

  const twTask = handle != null ? tupperwareTaskLink(handle.taskId) : null;
  const basePath = `/run/${encodeRunId(decodeRunId(params.runId))}`;
  const coHref = constraintsObjectivesHref(basePath, {
    dstBase: metadata.hasFinalAssignment ? 'FINAL' : 'INITIAL',
  });

  return (
    <PageContainer>
      <PageHead
        title="Summary"
        sub={[metadata.serviceName, metadata.serviceScope, metadata.solverType]
          .filter(x => x.length > 0)
          .join(' · ')}
      />
      <OverviewTiles
        metadata={metadata}
        summary={summary}
        moveSetCount={moveSetCount}
        coHref={coHref}
      />
      <PriorityLadder
        basePath={basePath}
        summary={summary}
        moveSetDeltas={moveSetDeltas}
        hasFinalAssignment={metadata.hasFinalAssignment}
      />
      <Paper variant="outlined">
        <SectionHeader title="Run details" />
        <Box
          sx={{
            display: 'grid',
            gridTemplateColumns: 'repeat(2, minmax(0, 1fr))',
            columnGap: 4,
            rowGap: 2,
            px: 2,
            pt: 0.5,
            pb: 2,
            '@media (max-width: 1180px)': {gridTemplateColumns: '1fr'},
          }}>
          <FieldGroup title="Run">
            <Field label="Run ID">
              <Box
                component="span"
                sx={{display: 'inline-flex', alignItems: 'center', gap: 0.5}}>
                <Box
                  component="span"
                  sx={{...MONO_TEXT, wordBreak: 'break-all'}}>
                  {metadata.runId}
                </Box>
                <CopyButton value={metadata.runId} label="Copy run ID" />
              </Box>
            </Field>
            {handle != null && (
              <Field label="Manifold Expiration">
                <BundleExpirationButton handle={handle} />
              </Field>
            )}
            <Field label="Service Name">{metadata.serviceName}</Field>
            <Field label="Service Scope">{metadata.serviceScope}</Field>
            {twTask != null && (
              <Field label="TW Task">
                <Link
                  href={twTask.href}
                  target="_blank"
                  rel="noopener noreferrer"
                  sx={{
                    display: 'inline-flex',
                    alignItems: 'center',
                    gap: 0.5,
                  }}>
                  {twTask.label}
                  <ExternalLink size={12} aria-hidden />
                </Link>
              </Field>
            )}
          </FieldGroup>
          <FieldGroup title="Solver & size">
            <Field label="Solver Type">
              <Pill>{metadata.solverType}</Pill>
            </Field>
            <Field label="Solver End Reason">
              <Pill
                tone={endReasonTone(metadata.solverEndReason)}
                sx={{height: 'auto', minHeight: 20, whiteSpace: 'normal'}}>
                {metadata.solverEndReason}
              </Pill>
            </Field>
            <Field label="Total Runtime">
              <Tooltip title={`${metadata.totalRuntime} s`}>
                <span>{formatRuntime(metadata.totalRuntime)}</span>
              </Tooltip>
            </Field>
            <Field label="Number of Objects">
              {formatCount(metadata.numObjects)}
            </Field>
            <Field label="Number of Containers">
              {formatCount(metadata.numContainers)}
            </Field>
            <Field label="Number of Dimensions">
              {formatCount(metadata.numDimensions)}
            </Field>
            <Field label="Number of Scopes">
              {formatCount(metadata.numScopes)}
            </Field>
          </FieldGroup>
        </Box>
      </Paper>
      <ProblemMap basePath={basePath} metadata={metadata} />
    </PageContainer>
  );
}
