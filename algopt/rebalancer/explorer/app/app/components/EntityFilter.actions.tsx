'use client';

import {Button, IconButton, Tooltip} from '@mui/material';
import {Plus, X} from 'lucide-react';
import type {
  ActionProps,
  ActionWithRulesAndAddersProps,
} from 'react-querybuilder';

export function AddFilterButton(props: ActionWithRulesAndAddersProps) {
  return (
    <Button
      size="small"
      variant="outlined"
      startIcon={<Plus size={14} />}
      onClick={props.handleOnClick}
      disabled={props.disabled}>
      Filter
    </Button>
  );
}

export function RemoveRuleButton(props: ActionProps) {
  return (
    <Tooltip title="Remove filter">
      <span>
        <IconButton
          size="small"
          aria-label="Remove filter"
          onClick={props.handleOnClick}
          disabled={props.disabled}>
          <X size={14} />
        </IconButton>
      </span>
    </Tooltip>
  );
}
