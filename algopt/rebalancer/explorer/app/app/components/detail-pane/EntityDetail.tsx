'use client';

import {useEffect, useMemo, useState} from 'react';

import {Box, Tab, Tabs} from '@mui/material';

import {useRunMoves} from '@/app/components/entity/useRunChanges';
import {Count} from '@/app/components/ui/Pill';
import {
  EmptyState,
  ErrorState,
  LoadingState,
} from '@/app/components/ui/StateViews';
import {buildFields} from '@/lib/entity-fields';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {fetchEntityData} from '@/lib/rebalancer-explorer-api';
import type {
  ColumnDescription,
  Handle,
  Result,
} from '@/lib/rebalancer-explorer-types';
import {COLOR} from '@/lib/ui-tokens';

import EntityValues from './entity/EntityValues';
import ItemOverview from './entity/ItemOverview';
import ObjectOverview from './entity/ObjectOverview';

// Primary-key column per (sandbox, entity), so a second open needs one RPC.
const primaryKeyCache = new Map<string, string>();

type EntityTab = 'overview' | 'values';
// The tab stays put while j / k step through the rows.
let lastTab: EntityTab = 'overview';

async function loadRow(
  handle: Handle,
  entity: string,
  id: string,
): Promise<{primaryKey: string; result: Result}> {
  const cacheKey = `${JSON.stringify(handle)}|${entity}`;
  let primaryKey = primaryKeyCache.get(cacheKey);
  if (primaryKey == null) {
    const schema = await fetchEntityData(handle, {
      entity,
      page: {offset: 0, limit: 1},
    });
    const pk = schema.result.columns.find(
      (col: ColumnDescription) => col.primaryKey,
    );
    if (pk == null) {
      throw new Error(`${entity} has no primary-key column`);
    }
    primaryKey = pk.name;
    primaryKeyCache.set(cacheKey, primaryKey);
  }
  const response = await fetchEntityData(handle, {
    entity,
    filter: {rules: [{stringAny: {column: primaryKey, values: [id]}}]},
    page: {offset: 0, limit: 1},
  });
  return {primaryKey, result: response.result};
}

/**
 * One object, container or scope item as a node of the run: an overview of
 * how it changed and what it connects to (its placement, the objects that
 * moved through it, the objectives those moves changed, its scopes and
 * groups), and every value of its row.
 */
export default function EntityDetail({
  handle,
  runId,
  entity,
  id,
}: {
  handle: Handle;
  runId: string;
  entity: string;
  id: string;
}) {
  const [state, setState] = useState<
    | {status: 'loading'}
    | {status: 'error'; message: string}
    | {status: 'done'; primaryKey: string; result: Result}
  >({status: 'loading'});
  const {metadata} = useProblemMetadata();
  const [tab, setTab] = useState<EntityTab>(lastTab);

  const kind =
    metadata == null
      ? null
      : entity === metadata.objectName
        ? 'object'
        : entity === metadata.containerName ||
            metadata.scopeNames.includes(entity)
          ? 'item'
          : null;
  const {moves} = useRunMoves(handle, metadata, kind != null);

  useEffect(() => {
    let cancelled = false;
    loadRow(handle, entity, id).then(
      ({primaryKey, result}) => {
        if (!cancelled) {
          setState({status: 'done', primaryKey, result});
        }
      },
      (err: unknown) => {
        if (!cancelled) {
          setState({
            status: 'error',
            message:
              err instanceof Error ? err.message : 'Failed to load the row',
          });
        }
      },
    );
    return () => {
      cancelled = true;
    };
  }, [handle, entity, id]);

  const fields = useMemo(
    () => (state.status === 'done' ? buildFields(state.result.columns) : []),
    [state],
  );

  if (state.status === 'loading') {
    return <LoadingState compact label={`Loading ${entity}...`} />;
  }
  if (state.status === 'error') {
    return <ErrorState>{state.message}</ErrorState>;
  }
  if (state.result.rows.length === 0) {
    return <EmptyState compact title={`No ${entity} named ${id}`} />;
  }
  const row = state.result.rows[0];
  const values = (
    <EntityValues
      runId={runId}
      entity={entity}
      id={id}
      primaryKey={state.primaryKey}
      result={state.result}
    />
  );
  if (kind == null) {
    return values;
  }
  return (
    <Box>
      <Box
        sx={{
          position: 'sticky',
          top: -16,
          zIndex: 1,
          mx: -2.5,
          mt: -2,
          mb: 2,
          px: 1.5,
          bgcolor: COLOR.bgSurface,
          borderBottom: `1px solid ${COLOR.borderDefault}`,
        }}>
        <Tabs
          value={tab}
          onChange={(_e, value: EntityTab) => {
            lastTab = value;
            setTab(value);
          }}>
          <Tab value="overview" label="Overview" />
          <Tab
            value="values"
            label={
              <Box
                component="span"
                sx={{display: 'inline-flex', alignItems: 'center', gap: 0.75}}>
                Values
                <Count>{fields.filter(f => !f.primaryKey).length}</Count>
              </Box>
            }
          />
        </Tabs>
      </Box>
      {tab === 'values' ? (
        values
      ) : kind === 'object' ? (
        <ObjectOverview
          handle={handle}
          runId={runId}
          id={id}
          fields={fields}
          row={row}
          moves={moves}
        />
      ) : (
        <ItemOverview
          handle={handle}
          runId={runId}
          entity={entity}
          id={id}
          fields={fields}
          row={row}
          moves={moves}
        />
      )}
    </Box>
  );
}
