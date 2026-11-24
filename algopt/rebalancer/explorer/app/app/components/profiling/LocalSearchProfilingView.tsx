'use client';

import {useEffect, useState} from 'react';

import {
  Box,
  ButtonBase,
  Collapse,
  Paper,
  Skeleton,
  Typography,
} from '@mui/material';
import {Activity, ChevronRight} from 'lucide-react';

import PageHead, {PageContainer} from '@/app/components/shell/PageHead';
import {Count} from '@/app/components/ui/Pill';
import SectionHeader from '@/app/components/ui/SectionHeader';
import Tag from '@/app/components/ui/Tag';
import {formatCount} from '@/lib/format';
import {ACCENT} from '@/lib/ui-tokens';

import {useRebalancerHandle} from '@/lib/contexts/RebalancerHandleContext';
import {fetchLocalSearchProfiles} from '@/lib/rebalancer-explorer-api';
import type {LocalSearchProfile} from '@/lib/rebalancer-explorer-types';

import LocalSearchProfileCard, {
  buildDecreaseByMoveType,
  buildTimeByMoveType,
  formatNumber,
} from './LocalSearchProfileCard';
import {EmptyState, ErrorState} from '@/app/components/ui/StateViews';

export default function LocalSearchProfilingView() {
  const {
    handle,
    loading: handleLoading,
    error: handleError,
  } = useRebalancerHandle();

  const [profiles, setProfiles] = useState<LocalSearchProfile[] | null>(null);
  const [loading, setLoading] = useState(handle != null);
  const [error, setError] = useState<string | null>(null);

  // A new handle starts a new load; reset during render, not in the effect.
  const [prevHandle, setPrevHandle] = useState(handle);
  if (handle !== prevHandle) {
    setPrevHandle(handle);
    setProfiles(null);
    setError(null);
    setLoading(handle != null);
  }

  useEffect(() => {
    if (handle == null) {
      return;
    }

    let cancelled = false;
    fetchLocalSearchProfiles(handle)
      .then(response => {
        if (!cancelled) {
          setProfiles(response.profiles);
          setLoading(false);
        }
      })
      .catch((err: unknown) => {
        if (!cancelled) {
          setError(
            err instanceof Error
              ? err.message
              : 'Failed to fetch local search profiles',
          );
          setLoading(false);
        }
      });

    return () => {
      cancelled = true;
    };
  }, [handle]);

  if (handleLoading) {
    return (
      <PageContainer>
        <PageHead
          title="Local Search"
          sub="Per-objective local search solver telemetry."
        />
        <Skeleton variant="rectangular" height={48} />
        <Skeleton variant="rectangular" height={400} />
      </PageContainer>
    );
  }

  if (handleError != null) {
    return (
      <div className="p-4">
        <ErrorState>{handleError}</ErrorState>
      </div>
    );
  }

  if (handle == null) {
    return (
      <div className="p-4">
        <EmptyState title="No handle available" />
      </div>
    );
  }

  if (loading && profiles == null) {
    return (
      <PageContainer>
        <PageHead
          title="Local Search"
          sub="Per-objective local search solver telemetry."
        />
        <Skeleton variant="rectangular" height={48} />
        <Skeleton variant="rectangular" height={400} />
      </PageContainer>
    );
  }

  if (error != null) {
    return (
      <PageContainer>
        <PageHead
          title="Local Search"
          sub="Per-objective local search solver telemetry."
        />
        <ErrorState>{error}</ErrorState>
      </PageContainer>
    );
  }

  const firstExpandable =
    profiles?.findIndex(p => p.moveTypeEvents.length > 1) ?? -1;

  return (
    <PageContainer>
      <PageHead
        title="Local Search"
        sub="Per-objective local search solver telemetry."
      />
      {profiles != null && profiles.length === 0 ? (
        <Paper variant="outlined">
          <EmptyState
            icon={Activity}
            title="No local search profiling data is available for this run.">
            This typically means one of the following:
            <ul className="mt-1 inline-block list-disc pl-5 text-left">
              <li>The run did not use the local-search solver.</li>
              <li>
                The solver encountered an exception before producing a solution.
              </li>
            </ul>
          </EmptyState>
        </Paper>
      ) : (
        <Paper variant="outlined">
          <SectionHeader
            title="Profiles"
            meta={profiles != null && <Count>{profiles.length}</Count>}
          />
          {profiles?.map((profile, i) => (
            <ProfileRow
              key={i}
              goalIndex={i}
              profile={profile}
              defaultExpanded={i === firstExpandable}
            />
          ))}
        </Paper>
      )}
    </PageContainer>
  );
}

function ProfileRow({
  goalIndex,
  profile,
  defaultExpanded,
}: {
  goalIndex: number;
  profile: LocalSearchProfile;
  defaultExpanded: boolean;
}) {
  const [expanded, setExpanded] = useState(defaultExpanded);
  const events = profile.moveTypeEvents;
  const totalSeconds = buildTimeByMoveType(profile).reduce(
    (sum, d) => sum + d.value,
    0,
  );
  const totalDecrease = buildDecreaseByMoveType(profile).reduce(
    (sum, d) => sum + d.value,
    0,
  );
  const usedMoveTypes = Array.from(
    new Set(events.map(e => profile.moveTypeNames[e.moveTypeIndex])),
  ).filter((name): name is string => name != null);

  return (
    <Box sx={{borderTop: 1, borderColor: 'divider'}}>
      <ButtonBase
        onClick={() => setExpanded(v => !v)}
        aria-expanded={expanded}
        sx={{
          width: '100%',
          display: 'flex',
          flexWrap: 'wrap',
          alignItems: 'center',
          justifyContent: 'flex-start',
          columnGap: 2,
          rowGap: 0.5,
          px: 2,
          py: 1.25,
          textAlign: 'left',
          '&:hover': {bgcolor: 'action.hover'},
          '&.Mui-focusVisible': {
            outline: `2px solid ${ACCENT}`,
            outlineOffset: -2,
          },
        }}>
        <ChevronRight
          size={16}
          style={{
            transform: expanded ? 'rotate(90deg)' : undefined,
            transition: 'transform 150ms',
            flexShrink: 0,
          }}
        />
        <Typography variant="subtitle2" component="span" sx={{minWidth: 110}}>
          {`Goal index ${goalIndex}`}
        </Typography>
        <Typography variant="body2" component="span" color="text.secondary">
          {`${formatCount(events.length)} ${events.length === 1 ? 'event' : 'events'}`}
        </Typography>
        <Typography variant="body2" component="span" color="text.secondary">
          {`${formatNumber(totalSeconds)} s`}
        </Typography>
        <Typography variant="body2" component="span" color="text.secondary">
          {`objective decrease ${formatNumber(totalDecrease)}`}
        </Typography>
        <Box sx={{display: 'flex', gap: 0.5, flexWrap: 'wrap'}}>
          {usedMoveTypes.map(name => (
            <Tag key={name} mono>
              {name}
            </Tag>
          ))}
        </Box>
      </ButtonBase>
      <Collapse in={expanded} unmountOnExit>
        <Box sx={{px: 2, pb: 2}}>
          {events.length > 1 ? (
            <LocalSearchProfileCard profile={profile} />
          ) : events.length === 1 ? (
            <Typography variant="body2" color="text.secondary" sx={{pl: 4}}>
              {`One event: ${
                profile.moveTypeNames[events[0].moveTypeIndex] ??
                `unknown_${events[0].moveTypeIndex}`
              } ran for ${formatNumber(events[0].duration)} s, objective ${formatNumber(
                events[0].initialValue,
              )} → ${formatNumber(events[0].finalValue)}.`}
            </Typography>
          ) : (
            <EmptyState
              compact
              title="No events recorded for this objective."
            />
          )}
        </Box>
      </Collapse>
    </Box>
  );
}
