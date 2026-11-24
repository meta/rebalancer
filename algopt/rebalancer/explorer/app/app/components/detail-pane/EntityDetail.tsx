'use client';

import {useEffect, useMemo, useState} from 'react';

import {Box, Link, TextField, Typography} from '@mui/material';
import {ArrowUpRight} from 'lucide-react';

import CopyButton from '@/app/components/ui/CopyButton';
import {
  EmptyState,
  ErrorState,
  LoadingState,
} from '@/app/components/ui/StateViews';
import Tag from '@/app/components/ui/Tag';
import {columnTypeInfo} from '@/lib/column-types';
import {isTypeaheadColumn} from '@/lib/format';
import {COLOR} from '@/lib/ui-tokens';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {fetchEntityData} from '@/lib/rebalancer-explorer-api';
import type {
  CellData,
  ColumnDescription,
  Handle,
  Result,
} from '@/lib/rebalancer-explorer-types';
import {encodeRunId} from '@/lib/run-id';

import EntityLink from './EntityLink';
import {stringifyQs} from '@/lib/url-state';

// Primary-key column per (sandbox, entity), so a second open needs one RPC.
const primaryKeyCache = new Map<string, string>();

function formatValue(cell: CellData | undefined): string {
  if (cell == null) {
    return '-';
  }
  if (cell.doubleValue != null) {
    const num = Number(cell.doubleValue);
    return Number.isInteger(num) ? String(num) : String(num);
  }
  return cell.stringValue ?? '-';
}

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

/** One entity row (object, container or scope item), shown vertically. */
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
  const [query, setQuery] = useState('');
  const {metadata} = useProblemMetadata();

  // A value that names another object, container or scope item opens it as
  // the next panel. Partitions have no entity page.
  const linkEntityFor = useMemo(() => {
    const known = new Set(
      metadata == null
        ? []
        : [metadata.objectName, metadata.containerName, ...metadata.scopeNames],
    );
    return (col: {
      name: string;
      type: Parameters<typeof isTypeaheadColumn>[0];
    }): string | null => {
      if (!isTypeaheadColumn(col.type)) {
        return null;
      }
      if (known.has(col.name)) {
        return col.name;
      }
      const dot = col.name.indexOf('.');
      const suffix = dot >= 0 ? col.name.substring(dot + 1) : null;
      return suffix != null && known.has(suffix) ? suffix : null;
    };
  }, [metadata]);

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

  const attributes = useMemo(() => {
    if (state.status !== 'done' || state.result.rows.length === 0) {
      return [];
    }
    const row = state.result.rows[0];
    const needle = query.trim().toLowerCase();
    return state.result.columns
      .map((col, i) => ({col, value: formatValue(row.cells[i])}))
      .filter(
        ({col, value}) =>
          needle === '' ||
          col.name.toLowerCase().includes(needle) ||
          value.toLowerCase().includes(needle),
      );
  }, [state, query]);

  if (state.status === 'loading') {
    return <LoadingState compact label={`Loading ${entity}...`} />;
  }
  if (state.status === 'error') {
    return <ErrorState>{state.message}</ErrorState>;
  }
  if (state.result.rows.length === 0) {
    return <EmptyState compact title={`No ${entity} named ${id}`} />;
  }

  // Dynamic dimensions have their own page, like in the sidebar.
  const section = metadata?.dynamicDimensionNames.includes(entity)
    ? 'dynamic-dimensions'
    : 'entity';
  const tableHref = `/run/${encodeRunId(runId)}/${section}/${encodeURIComponent(
    entity,
  )}?${stringifyQs({
    filters: [{rules: [{stringAny: {column: state.primaryKey, values: [id]}}]}],
  })}`;
  const columnCount = state.result.columns.length;

  return (
    <Box>
      <Box sx={{display: 'flex', alignItems: 'center', gap: 1, mb: 1.5}}>
        <Typography variant="subtitle2" sx={{flex: 1}}>
          {`${columnCount} attributes`}
        </Typography>
        {/* A full navigation: the entity view reads its filters from the URL
            on mount, including when this is the page already open. */}
        <Link
          href={tableHref}
          sx={{display: 'inline-flex', alignItems: 'center', gap: 0.5}}>
          Open in table
          <ArrowUpRight size={14} aria-hidden />
        </Link>
      </Box>
      {columnCount > 12 && (
        <TextField
          size="small"
          fullWidth
          placeholder="Find an attribute or value"
          value={query}
          onChange={e => setQuery(e.target.value)}
          sx={{mb: 1.5}}
        />
      )}
      <Box
        component="dl"
        sx={{
          m: 0,
          display: 'grid',
          gridTemplateColumns: {xs: '1fr', sm: 'minmax(140px, 40%) 1fr'},
          columnGap: 2,
        }}>
        {attributes.map(({col, value}) => {
          const type = columnTypeInfo(col.type);
          const linkEntity = linkEntityFor(col);
          return (
            <Box key={col.name} sx={{display: 'contents'}}>
              <Box
                component="dt"
                sx={{
                  display: 'flex',
                  alignItems: 'center',
                  gap: 0.75,
                  py: 0.75,
                  minWidth: 0,
                }}>
                <Typography
                  variant="body2"
                  sx={{color: COLOR.textTertiary, overflowWrap: 'anywhere'}}>
                  {col.name}
                </Typography>
                {type.short !== '' && (
                  <Tag small title={type.full}>
                    {type.short}
                  </Tag>
                )}
              </Box>
              <Box
                component="dd"
                sx={{
                  m: 0,
                  display: 'flex',
                  alignItems: 'center',
                  gap: 0.5,
                  py: 0.75,
                  minWidth: 0,
                }}>
                {linkEntity != null &&
                value !== '' &&
                !(linkEntity === entity && value === id) ? (
                  <Box sx={{flex: 1, minWidth: 0}}>
                    <EntityLink entity={linkEntity} id={value} />
                  </Box>
                ) : (
                  <Typography
                    variant="body2"
                    sx={{
                      overflowWrap: 'anywhere',
                      fontVariantNumeric: 'tabular-nums',
                      flex: 1,
                    }}>
                    {value}
                  </Typography>
                )}
                <CopyButton
                  value={value}
                  label={`Copy ${col.name}`}
                  size={12}
                />
              </Box>
            </Box>
          );
        })}
      </Box>
    </Box>
  );
}
