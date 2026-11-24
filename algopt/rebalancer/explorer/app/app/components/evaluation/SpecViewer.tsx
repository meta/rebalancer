'use client';

import {useCallback, useEffect, useState} from 'react';

import {Box, Tooltip, Typography} from '@mui/material';

import CopyButton from '@/app/components/ui/CopyButton';

import {
  EmptyState,
  ErrorState,
  LoadingState,
} from '@/app/components/ui/StateViews';

import {
  fetchConstraintSpec,
  fetchGoalSpec,
} from '@/lib/rebalancer-explorer-api';
import type {
  ConstraintSpec,
  GoalSpec,
  Handle,
} from '@/lib/rebalancer-explorer-types';
import {COLOR, MONO_TEXT, radius, SURFACE_2} from '@/lib/ui-tokens';

interface SpecField {
  label: string;
  value: string;
  tooltip?: string;
}

function isGoalSpec(spec: GoalSpec | ConstraintSpec): spec is GoalSpec {
  return 'weight' in spec;
}

function getSpecFields(spec: GoalSpec | ConstraintSpec): SpecField[] {
  if (isGoalSpec(spec)) {
    return [
      {label: 'Weight', value: String(spec.weight)},
      {label: 'Tuple Index', value: String(spec.tupleIndex)},
    ];
  }
  return [
    {
      label: 'Invalid Cost',
      value: String(spec.invalidCost),
      tooltip:
        'Weight applied to the goal representing the softened version of the constraint when initially broken',
    },
    {
      label: 'Invalid State',
      value: String(spec.invalidState),
      tooltip:
        'Fixed cost added to the goal when the softened constraint is broken',
    },
  ];
}

interface SpecViewerProps {
  name: string;
  handle: Handle;
  expressionType: 'OBJECTIVE' | 'CONSTRAINT';
}

export default function SpecViewer({
  name,
  handle,
  expressionType,
}: SpecViewerProps) {
  const [spec, setSpec] = useState<GoalSpec | ConstraintSpec | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const doFetch = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      if (expressionType === 'CONSTRAINT') {
        const response = await fetchConstraintSpec(handle, name);
        setSpec(response.spec);
      } else {
        // Objectives can be either true goals or soft constraints.
        // Soft constraints are classified as OBJECTIVE by the backend but
        // their names only exist in the constraint ID map. Try goal first,
        // then fall back to constraint.
        try {
          const response = await fetchGoalSpec(handle, name);
          setSpec(response.spec);
        } catch {
          const response = await fetchConstraintSpec(handle, name);
          setSpec(response.spec);
        }
      }
    } catch (err: unknown) {
      setError(err instanceof Error ? err.message : 'Failed to fetch spec');
    } finally {
      setLoading(false);
    }
  }, [handle, name, expressionType]);

  useEffect(() => {
    doFetch();
  }, [doFetch]);

  if (loading) {
    return <LoadingState compact label="Loading spec..." />;
  }

  if (error != null) {
    return <ErrorState>{error}</ErrorState>;
  }

  if (spec == null) {
    return <EmptyState compact title="No spec data available" />;
  }

  let parsedSpec: unknown;
  try {
    parsedSpec = JSON.parse(spec.specJson);
  } catch {
    parsedSpec = spec.specJson;
  }

  const fields = getSpecFields(spec);
  const specText =
    typeof parsedSpec === 'string'
      ? parsedSpec
      : JSON.stringify(parsedSpec, null, 2);

  return (
    <Box>
      <Box
        component="dl"
        sx={{
          display: 'grid',
          gridTemplateColumns: {xs: '1fr', sm: '160px minmax(0, 1fr)'},
          columnGap: 2,
          rowGap: 1,
          m: 0,
          mb: 2,
        }}>
        {[{label: 'Name', value: spec.name}, ...fields].map(field => (
          <Box key={field.label} sx={{display: 'contents'}}>
            <Typography
              component="dt"
              variant="body2"
              sx={{color: COLOR.textTertiary}}>
              {'tooltip' in field && field.tooltip != null ? (
                <Tooltip title={field.tooltip} placement="top-start">
                  <Box
                    component="span"
                    sx={{
                      cursor: 'help',
                      borderBottom: 1,
                      borderStyle: 'dotted',
                      borderColor: COLOR.textDecorative,
                    }}>
                    {field.label}
                  </Box>
                </Tooltip>
              ) : (
                field.label
              )}
            </Typography>
            <Typography
              component="dd"
              variant="body2"
              sx={{m: 0, overflowWrap: 'anywhere'}}>
              {field.value}
            </Typography>
          </Box>
        ))}
      </Box>
      <Box sx={{display: 'flex', alignItems: 'center', mb: 0.75}}>
        <Typography variant="subtitle2" component="h3" sx={{flex: 1}}>
          Spec
        </Typography>
        <CopyButton value={specText} label="Copy JSON" />
      </Box>
      <Box
        component="pre"
        sx={{
          m: 0,
          px: 1.5,
          py: 1.25,
          maxHeight: '60vh',
          overflow: 'auto',
          whiteSpace: 'pre-wrap',
          wordBreak: 'break-word',
          ...MONO_TEXT,
          lineHeight: 1.5,
          color: COLOR.textSecondary,
          bgcolor: SURFACE_2,
          border: `1px solid ${COLOR.borderDefault}`,
          borderRadius: radius('md'),
        }}>
        {specText}
      </Box>
    </Box>
  );
}
