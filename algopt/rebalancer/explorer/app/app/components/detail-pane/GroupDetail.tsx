'use client';

import {useEffect, useMemo, useState} from 'react';

import {Box, MenuItem, TextField} from '@mui/material';

import useGroupSizes from '@/app/components/entity/useGroupSizes';
import {useRunMoves} from '@/app/components/entity/useRunChanges';
import PanelValue from '@/app/components/ui/PanelValue';
import {formatCount} from '@/lib/format';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {fetchEntityData} from '@/lib/rebalancer-explorer-api';
import type {Handle} from '@/lib/rebalancer-explorer-types';
import {flows, movedInGroup, spreadScopeOf} from '@/lib/run-changes';
import {encodeRunId} from '@/lib/run-id';
import {COLOR, TABULAR, TEXT, WEIGHT} from '@/lib/ui-tokens';
import {stringifyQs} from '@/lib/url-state';

import EntityLink from './EntityLink';
import ObjectiveChanges from './entity/ObjectiveChanges';
import {PanelNote, PanelSection, TableLink} from './entity/parts';

const MOVED_PREVIEW = 8;
const FLOW_PREVIEW = 6;

/** How many items of `key` the group's objects sit in, Initial and Final. */
function useSpread(
  handle: Handle,
  objectName: string | null,
  partition: string,
  group: string,
  key: string | null,
): {key: string; initial: number; final: number} | null {
  const [spread, setSpread] = useState<{
    key: string;
    initial: number;
    final: number;
  } | null>(null);
  useEffect(() => {
    if (objectName == null || key == null) {
      return;
    }
    let cancelled = false;
    const distinct = (column: string) =>
      fetchEntityData(handle, {
        entity: objectName,
        // Objects in no item of the scope are not an item.
        filter: {
          rules: [
            {stringAny: {column: partition, values: [group]}},
            {stringNe: {column, value: ''}},
          ],
        },
        group: {columns: [column]},
        page: {offset: 0, limit: 1},
      }).then(r => r.result.totalCount);
    Promise.all([distinct(`src.${key}`), distinct(`dst.${key}`)]).then(
      ([initial, final]) => {
        if (!cancelled) {
          setSpread({key, initial, final});
        }
      },
      () => {},
    );
    return () => {
      cancelled = true;
    };
  }, [handle, objectName, partition, group, key]);
  return spread?.key === key ? spread : null;
}

/**
 * One partition group: its size, how many of its objects moved, how far it
 * spreads over a scope's items Initial → Final and where its moves went,
 * and which objectives those moves changed.
 */
export default function GroupDetail({
  handle,
  runId,
  partition,
  group,
}: {
  handle: Handle;
  runId: string;
  partition: string;
  group: string;
}) {
  const {metadata} = useProblemMetadata();
  const {moves} = useRunMoves(handle, metadata);
  const sizes = useGroupSizes(handle, metadata, partition, [group]);
  const members = sizes.get(group) ?? null;
  const movedMembers = useMemo(
    () =>
      moves?.moved != null ? movedInGroup(moves.moved, partition, group) : [],
    [moves, partition, group],
  );
  const keys = useMemo(
    () =>
      metadata == null ? [] : [metadata.containerName, ...metadata.scopeNames],
    [metadata],
  );
  const [picked, setPicked] = useState<string | null>(null);
  const spreadKey =
    picked ??
    (metadata != null && moves?.moved != null
      ? spreadScopeOf(moves.moved, movedMembers, keys, metadata.containerName)
      : (metadata?.containerName ?? null));
  const spread = useSpread(
    handle,
    metadata?.objectName ?? null,
    partition,
    group,
    spreadKey,
  );
  const groupFlows = useMemo(
    () =>
      moves?.moved != null && spreadKey != null
        ? flows(moves.moved, spreadKey, new Set(movedMembers))
        : [],
    [moves, spreadKey, movedMembers],
  );
  const [allFlows, setAllFlows] = useState(false);

  if (metadata == null || spreadKey == null) {
    return null;
  }
  const objectsHref = (show: 'all' | 'moved') =>
    `/run/${encodeRunId(runId)}/entity/${encodeURIComponent(
      metadata.objectName,
    )}?${stringifyQs({
      filters: [{rules: [{stringAny: {column: partition, values: [group]}}]}],
      show,
    })}`;
  const shownFlows = allFlows ? groupFlows : groupFlows.slice(0, FLOW_PREVIEW);

  return (
    <Box sx={{display: 'flex', flexDirection: 'column', gap: 3}}>
      <Box
        sx={{
          display: 'grid',
          gridTemplateColumns: 'repeat(3, minmax(0, 1fr))',
          gap: 1,
        }}>
        <PanelValue label={`Members · ${metadata.objectName}`}>
          {members != null ? formatCount(members) : '—'}
        </PanelValue>
        <PanelValue label="Moved">
          {moves?.moved != null ? formatCount(movedMembers.length) : '—'}
        </PanelValue>
        <PanelValue label={`Spread · ${spreadKey} items`}>
          {spread != null ? (
            spread.initial === spread.final ? (
              formatCount(spread.final)
            ) : (
              <>
                <Box
                  component="span"
                  sx={{color: COLOR.textTertiary, fontWeight: WEIGHT.medium}}>
                  {formatCount(spread.initial)}
                </Box>
                <Box
                  component="span"
                  aria-label="to"
                  sx={{color: COLOR.textDecorative, mx: 0.5}}>
                  →
                </Box>
                {formatCount(spread.final)}
              </>
            )
          ) : (
            '—'
          )}
        </PanelValue>
      </Box>

      <PanelSection
        title="Where its moves went"
        sub={
          groupFlows.length > 0
            ? `${formatCount(movedMembers.length)} moved, by ${spreadKey}`
            : undefined
        }
        action={
          <TextField
            select
            size="small"
            value={spreadKey}
            onChange={e => setPicked(e.target.value)}
            slotProps={{htmlInput: {'aria-label': 'Scope'}}}
            sx={{minWidth: 180, '& .MuiInputBase-input': {py: 0.5}}}>
            {keys.map(k => (
              <MenuItem key={k} value={k}>
                {k}
              </MenuItem>
            ))}
          </TextField>
        }>
        {movedMembers.length === 0 ? (
          <PanelNote>{`None of its ${metadata.objectName} rows moved.`}</PanelNote>
        ) : groupFlows.length === 0 ? (
          <PanelNote>{`Its moves stayed inside the same ${spreadKey} items.`}</PanelNote>
        ) : (
          <>
            {shownFlows.map(f => (
              <Box
                key={`${f.src}\u0000${f.dst}`}
                sx={{
                  display: 'grid',
                  gridTemplateColumns:
                    'minmax(0, 1fr) 16px minmax(0, 1fr) 40px',
                  columnGap: 1,
                  alignItems: 'baseline',
                  py: 0.5,
                  fontSize: TEXT.sm,
                }}>
                <Box component="span" sx={{minWidth: 0}}>
                  {f.src !== '' ? (
                    <EntityLink entity={spreadKey} id={f.src} />
                  ) : (
                    <Box component="span" sx={{color: COLOR.textTertiary}}>
                      none
                    </Box>
                  )}
                </Box>
                <Box
                  component="span"
                  aria-label="to"
                  sx={{color: COLOR.textDecorative, textAlign: 'center'}}>
                  →
                </Box>
                <Box component="span" sx={{minWidth: 0}}>
                  {f.dst !== '' ? (
                    <EntityLink entity={spreadKey} id={f.dst} />
                  ) : (
                    <Box component="span" sx={{color: COLOR.textTertiary}}>
                      none
                    </Box>
                  )}
                </Box>
                <Box
                  component="span"
                  sx={{
                    textAlign: 'right',
                    fontWeight: WEIGHT.semibold,
                    ...TABULAR,
                  }}>
                  {formatCount(f.count)}
                </Box>
              </Box>
            ))}
            {groupFlows.length > FLOW_PREVIEW && (
              <Box
                component="button"
                type="button"
                onClick={() => setAllFlows(v => !v)}
                sx={{
                  mt: 0.5,
                  p: 0,
                  border: 0,
                  bgcolor: 'transparent',
                  font: 'inherit',
                  fontSize: TEXT.xs,
                  color: COLOR.action,
                  cursor: 'pointer',
                }}>
                {allFlows
                  ? 'Show fewer'
                  : `Show all ${formatCount(groupFlows.length)} flows`}
              </Box>
            )}
          </>
        )}
      </PanelSection>

      {movedMembers.length > 0 && (
        <PanelSection
          title="Moved members"
          sub={`${metadata.containerName}, Initial → Final`}
          action={
            movedMembers.length > MOVED_PREVIEW ? (
              <TableLink href={objectsHref('moved')}>
                {`All ${formatCount(movedMembers.length)}`}
              </TableLink>
            ) : undefined
          }>
          {movedMembers.slice(0, MOVED_PREVIEW).map(o => {
            const m = moves?.moved?.get(o);
            return (
              <Box
                key={o}
                sx={{
                  display: 'grid',
                  gridTemplateColumns:
                    'minmax(0, 1fr) minmax(0, 1fr) 16px minmax(0, 1fr)',
                  columnGap: 1,
                  alignItems: 'baseline',
                  py: 0.5,
                  fontSize: TEXT.sm,
                }}>
                <Box component="span" sx={{minWidth: 0}}>
                  <EntityLink entity={metadata.objectName} id={o} />
                </Box>
                <Box component="span" sx={{minWidth: 0}}>
                  {m?.src ? (
                    <EntityLink entity={metadata.containerName} id={m.src} />
                  ) : (
                    '—'
                  )}
                </Box>
                <Box
                  component="span"
                  aria-label="to"
                  sx={{color: COLOR.textDecorative, textAlign: 'center'}}>
                  →
                </Box>
                <Box component="span" sx={{minWidth: 0}}>
                  {m?.dst ? (
                    <EntityLink entity={metadata.containerName} id={m.dst} />
                  ) : (
                    '—'
                  )}
                </Box>
              </Box>
            );
          })}
        </PanelSection>
      )}

      {movedMembers.length > 0 && (
        <ObjectiveChanges
          handle={handle}
          objects={movedMembers}
          title="Objectives its moves changed"
        />
      )}

      <Box>
        <TableLink href={objectsHref('all')}>
          {members != null
            ? `All ${formatCount(members)} members in the ${metadata.objectName} table`
            : `Its members in the ${metadata.objectName} table`}
        </TableLink>
      </Box>
    </Box>
  );
}
