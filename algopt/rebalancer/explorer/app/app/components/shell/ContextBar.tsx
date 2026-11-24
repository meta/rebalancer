'use client';

import {useEffect} from 'react';

import {Box, Tooltip} from '@mui/material';
import {useParams, usePathname} from 'next/navigation';

import {usePageComparison} from '@/app/components/shell/CompareContext';
import RunStatus from '@/app/components/shell/RunStatus';
import CompareControl from '@/app/components/ui/CompareControl';
import CopyButton from '@/app/components/ui/CopyButton';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {decodeRunId, encodeRunId} from '@/lib/run-id';
import {middleEllipsis, runPageTitle} from '@/lib/run-page';
import {COLOR, MONO_TEXT, SHELL, TEXT, WEIGHT, Z} from '@/lib/ui-tokens';

const WIDE_PAD = `max(28px, calc((100% - ${SHELL.pageMaxWide}px) / 2 + 28px))`;

/**
 * The run bar under the top bar (sticky, blurred): the breadcrumb with copy,
 * the run's status and runtime, and the page's Compare control.
 */
export default function ContextBar() {
  const params = useParams<{runId: string}>();
  const pathname = usePathname();
  const {metadata} = useProblemMetadata();
  const comparison = usePageComparison();

  const runId = decodeRunId(params.runId);
  const basePath = `/run/${encodeRunId(runId)}`;
  const subPath = pathname.startsWith(basePath)
    ? pathname.slice(basePath.length)
    : '';
  const page = runPageTitle(subPath, metadata?.metricCollectionNames ?? []);
  const serviceName = metadata?.serviceName ?? '';

  useEffect(() => {
    document.title = [page, serviceName, 'Rebalancer Explorer']
      .filter(s => s.length > 0)
      .join(' · ');
  }, [page, serviceName]);

  // Summary always compares the initial and the final assignment.
  const onSummary = subPath === '' || subPath === '/';
  const shown =
    comparison ??
    (onSummary && metadata?.hasFinalAssignment === true
      ? {baseline: 'Initial', current: 'Final'}
      : null);

  return (
    <Box
      data-shell="context-bar"
      sx={{
        position: 'sticky',
        top: SHELL.topBar,
        zIndex: Z.contextbar,
        display: 'flex',
        alignItems: 'center',
        flexWrap: 'wrap',
        columnGap: 1.5,
        rowGap: 1,
        minHeight: 53,
        py: 1.25,
        px: 3.5,
        bgcolor: `color-mix(in srgb, ${COLOR.bgCanvas} 88%, transparent)`,
        backdropFilter: 'blur(8px)',
        borderBottom: `1px solid ${COLOR.borderDefault}`,
        '@media (min-width: 1900px)': {pl: WIDE_PAD, pr: WIDE_PAD},
        '@media (max-width: 760px)': {px: 2},
      }}>
      <Box
        component="nav"
        aria-label="Breadcrumb"
        sx={{
          display: 'flex',
          alignItems: 'center',
          gap: 0.75,
          minWidth: 0,
          color: COLOR.textTertiary,
          fontSize: TEXT.sm,
        }}>
        {serviceName.length > 0 && (
          <>
            <Box
              component="span"
              sx={{
                whiteSpace: 'nowrap',
                overflow: 'hidden',
                textOverflow: 'ellipsis',
                minWidth: 0,
              }}>
              {serviceName}
            </Box>
            <Box
              component="span"
              aria-hidden
              sx={{color: COLOR.textDecorative}}>
              /
            </Box>
          </>
        )}
        <Tooltip title={runId}>
          <Box
            component="b"
            aria-label={`Run ${runId}`}
            sx={{
              ...MONO_TEXT,
              color: COLOR.textPrimary,
              fontWeight: WEIGHT.semibold,
              whiteSpace: 'nowrap',
            }}>
            {middleEllipsis(runId)}
          </Box>
        </Tooltip>
        <CopyButton value={runId} label="Copy run ID" toast="Run ID copied" />
      </Box>
      <RunStatus />
      <Box sx={{flex: 1}} />
      {shown != null && (
        <CompareControl
          baseline={shown.baseline}
          current={shown.current}
          onEdit={'onEdit' in shown ? shown.onEdit : undefined}
        />
      )}
    </Box>
  );
}
