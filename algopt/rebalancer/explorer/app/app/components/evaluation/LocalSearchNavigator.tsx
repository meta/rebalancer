'use client';

import {Box, Button, IconButton, Tooltip, Typography} from '@mui/material';
import {
  ChevronLeft,
  ChevronRight,
  LogOut,
  Navigation,
  RotateCcw,
} from 'lucide-react';

import type {Assignments} from './AssignmentCard';

export interface LocalSearchNavigatorProps {
  assignments: Assignments;
  onAssignmentsChange: (a: Assignments) => void;
  numSteps: number;
}

export default function LocalSearchNavigator({
  assignments,
  onAssignmentsChange,
  numSteps,
}: LocalSearchNavigatorProps) {
  const isNavigating =
    assignments.src.base === 'INTERMEDIATE' &&
    assignments.dst.base === 'INTERMEDIATE' &&
    assignments.src.step != null &&
    assignments.dst.step != null;

  const startNavigation = () => {
    onAssignmentsChange({
      src: {base: 'INTERMEDIATE', overrides: [], step: 0},
      dst: {base: 'INTERMEDIATE', overrides: [], step: 1},
    });
  };

  const exitNavigation = () => {
    onAssignmentsChange({
      src: {base: 'INITIAL', overrides: [], step: null},
      dst: {base: 'FINAL', overrides: [], step: null},
    });
  };

  const onStepNavigation = (forward: boolean) => {
    const increment = forward ? 1 : -1;
    const srcStep = Math.min(
      numSteps - 1,
      Math.max(0, (assignments.src.step ?? 0) + increment),
    );
    const dstStep = Math.min(
      numSteps,
      Math.max(1, (assignments.dst.step ?? 0) + increment),
    );
    onAssignmentsChange({
      src: {base: 'INTERMEDIATE', overrides: [], step: srcStep},
      dst: {base: 'INTERMEDIATE', overrides: [], step: dstStep},
    });
  };

  if (!isNavigating) {
    return (
      <Tooltip title="Step through the solver's move sets one at a time">
        <Button
          size="small"
          variant="outlined"
          startIcon={<Navigation size={14} />}
          onClick={startNavigation}>
          Start navigation
        </Button>
      </Tooltip>
    );
  }

  return (
    <Box
      role="group"
      aria-label="Move set navigation"
      sx={{
        display: 'flex',
        alignItems: 'center',
        gap: 0.25,
        border: 1,
        borderColor: 'divider',
        borderRadius: 1,
        px: 0.5,
        py: 0.25,
      }}>
      <Tooltip title="Restart navigation">
        <IconButton
          size="small"
          aria-label="Restart navigation"
          onClick={startNavigation}>
          <RotateCcw size={14} />
        </IconButton>
      </Tooltip>
      <Tooltip title="Previous step">
        <span>
          <IconButton
            size="small"
            aria-label="Previous step"
            onClick={() => onStepNavigation(false)}
            disabled={assignments.src.step === 0}>
            <ChevronLeft size={16} />
          </IconButton>
        </span>
      </Tooltip>
      <Typography
        variant="body2"
        sx={{mx: 0.5, fontWeight: 600, fontVariantNumeric: 'tabular-nums'}}>
        Step {assignments.src.step} vs Step {assignments.dst.step}
      </Typography>
      <Tooltip title="Next step">
        <span>
          <IconButton
            size="small"
            aria-label="Next step"
            onClick={() => onStepNavigation(true)}
            disabled={assignments.dst.step === numSteps}>
            <ChevronRight size={16} />
          </IconButton>
        </span>
      </Tooltip>
      <Tooltip title="Exit navigation">
        <IconButton
          size="small"
          aria-label="Exit navigation"
          onClick={exitNavigation}>
          <LogOut size={14} />
        </IconButton>
      </Tooltip>
    </Box>
  );
}
