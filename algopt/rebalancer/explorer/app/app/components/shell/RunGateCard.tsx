'use client';

import {useEffect, useState, type ReactNode} from 'react';

import {Box, Button, CircularProgress, Paper, Typography} from '@mui/material';
import {AlertCircle, Check, Circle, RotateCw} from 'lucide-react';
import Link from 'next/link';

import BrandMark from '@/app/components/shell/BrandMark';
import CopyButton from '@/app/components/ui/CopyButton';
import Pill from '@/app/components/ui/Pill';
import {useRebalancerHandle} from '@/lib/contexts/RebalancerHandleContext';
import {middleEllipsis} from '@/lib/run-page';
import {
  COLOR,
  MONO_TEXT,
  radius,
  SHADOW_MD,
  TONES,
  type ToneName,
} from '@/lib/ui-tokens';

export type GateStep = 'handle' | 'sandbox' | 'metadata';

const STEPS: Array<{key: GateStep; label: string}> = [
  {key: 'handle', label: 'Resolving handle'},
  {key: 'sandbox', label: 'Waiting for sandbox'},
  {key: 'metadata', label: 'Loading problem metadata'},
];

function useElapsedSeconds(running: boolean): number {
  const [seconds, setSeconds] = useState(0);
  useEffect(() => {
    if (!running) {
      return;
    }
    const id = setInterval(() => setSeconds(s => s + 1), 1000);
    return () => clearInterval(id);
  }, [running]);
  return seconds;
}

function StepIcon({state}: {state: 'done' | 'current' | 'failed' | 'todo'}) {
  switch (state) {
    case 'done':
      return <Check size={16} color={TONES.good.solid} />;
    case 'current':
      return <CircularProgress size={14} thickness={6} />;
    case 'failed':
      return <AlertCircle size={16} color={TONES.bad.solid} />;
    case 'todo':
      return <Circle size={14} color={COLOR.borderStrong} />;
  }
}

/**
 * Full-page card shown while a run's handle, sandbox and metadata load, and
 * when one of them fails.
 */
export default function RunGateCard({
  step,
  state = 'pending',
  title,
  status,
  statusTone = 'info',
  children,
}: {
  step: GateStep;
  state?: 'pending' | 'error' | 'warning';
  title: ReactNode;
  /** Short verbatim status shown as a pill, e.g. the sandbox status. */
  status?: string;
  statusTone?: ToneName;
  children?: ReactNode;
}) {
  const {runId} = useRebalancerHandle();
  const pending = state === 'pending';
  const elapsed = useElapsedSeconds(pending);
  const current = STEPS.findIndex(s => s.key === step);

  return (
    <Box
      sx={{
        minHeight: '100vh',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        p: 2,
        bgcolor: COLOR.bgCanvas,
      }}>
      <Paper
        variant="outlined"
        role={pending ? 'status' : 'alert'}
        sx={{
          width: '100%',
          maxWidth: 480,
          p: 3,
          borderRadius: radius('xl'),
          boxShadow: SHADOW_MD,
        }}>
        <Box sx={{display: 'flex', alignItems: 'center', gap: 1.25, mb: 2.5}}>
          <BrandMark />
          <Box sx={{minWidth: 0, flex: 1}}>
            <Typography variant="subtitle1" component="div">
              Rebalancer Explorer
            </Typography>
            {runId != null && (
              <Box sx={{display: 'flex', alignItems: 'center', gap: 0.5}}>
                <Typography
                  component="span"
                  title={runId}
                  sx={{...MONO_TEXT, color: COLOR.textTertiary}}>
                  {middleEllipsis(runId)}
                </Typography>
                <CopyButton value={runId} label="Copy run ID" size={12} />
              </Box>
            )}
          </Box>
        </Box>

        <Typography variant="h2" component="h1" sx={{mb: 1.5}}>
          {title}
        </Typography>

        <Box
          component="ol"
          sx={{listStyle: 'none', p: 0, m: 0, mb: 2, display: 'grid', gap: 1}}>
          {STEPS.map((s, i) => {
            const stepState =
              i < current
                ? 'done'
                : i > current
                  ? 'todo'
                  : pending
                    ? 'current'
                    : 'failed';
            return (
              <Box
                component="li"
                key={s.key}
                aria-current={i === current ? 'step' : undefined}
                sx={{display: 'flex', alignItems: 'center', gap: 1.25}}>
                <Box
                  sx={{
                    width: 16,
                    display: 'flex',
                    justifyContent: 'center',
                  }}>
                  <StepIcon state={stepState} />
                </Box>
                <Typography
                  variant="body2"
                  sx={{
                    color: i > current ? COLOR.textTertiary : COLOR.textPrimary,
                    fontWeight: i === current ? 600 : 400,
                  }}>
                  {s.label}
                </Typography>
              </Box>
            );
          })}
        </Box>

        {(status != null || pending) && (
          <Box sx={{display: 'flex', alignItems: 'center', gap: 1, mb: 1.5}}>
            {status != null && <Pill tone={statusTone}>{status}</Pill>}
            {pending && (
              <Typography variant="caption">{elapsed}s elapsed</Typography>
            )}
          </Box>
        )}

        {children != null && (
          <Typography
            variant="body2"
            component="div"
            sx={{color: COLOR.textSecondary}}>
            {children}
          </Typography>
        )}

        {!pending && (
          <Box sx={{display: 'flex', gap: 1, mt: 2.5}}>
            <Button
              variant="contained"
              startIcon={<RotateCw size={14} />}
              onClick={() => window.location.reload()}>
              Retry
            </Button>
            <Button variant="outlined" component={Link} href="/">
              Open another run
            </Button>
          </Box>
        )}
      </Paper>
    </Box>
  );
}
