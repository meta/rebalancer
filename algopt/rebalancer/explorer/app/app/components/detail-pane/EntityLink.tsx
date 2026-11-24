'use client';

import {useContext} from 'react';

import {Box} from '@mui/material';

import {ELLIPSIS_SX} from '@/app/components/table/tableSx';
import {useRebalancerHandle} from '@/lib/contexts/RebalancerHandleContext';
import {COLOR} from '@/lib/ui-tokens';

import {InPaneContext, useDetailPaneActions} from './DetailPaneContext';

/**
 * An entity name in a table cell that opens its row in the side pane.
 * Click and Enter open the pane instead of copying the cell.
 */
export default function EntityLink({entity, id}: {entity: string; id: string}) {
  const {open} = useDetailPaneActions();
  // Inside a panel, the entity opens as the next panel, with "Back to …".
  const inPane = useContext(InPaneContext);
  const {handle, runId} = useRebalancerHandle();
  if (handle == null || runId == null || id === '' || id === '-') {
    return (
      <Box component="span" data-overflow sx={ELLIPSIS_SX}>
        {id}
      </Box>
    );
  }
  return (
    <Box
      component="button"
      type="button"
      data-overflow
      aria-label={`Show ${entity} ${id}`}
      onClick={e => {
        e.stopPropagation();
        open(
          {kind: 'entity', payload: {entity, id, handle, runId}},
          {push: inPane},
        );
      }}
      onKeyDown={e => e.stopPropagation()}
      sx={{
        ...ELLIPSIS_SX,
        maxWidth: '100%',
        p: 0,
        border: 0,
        bgcolor: 'transparent',
        font: 'inherit',
        color: COLOR.action,
        textAlign: 'inherit',
        cursor: 'pointer',
        '&:hover': {textDecoration: 'underline'},
      }}>
      {id}
    </Box>
  );
}
