'use client';

import {useEffect, useMemo, useState} from 'react';

import {Box, Button} from '@mui/material';

import type {RunMoves} from '@/app/components/entity/useRunChanges';
import {BaselineCurrentBars} from '@/app/components/ui/Bars';
import PanelValue from '@/app/components/ui/PanelValue';
import {fieldValues, type EntityField} from '@/lib/entity-fields';
import {formatCount, formatSignedCompact} from '@/lib/format';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {fetchEntityData} from '@/lib/rebalancer-explorer-api';
import type {
  Filter,
  Handle,
  ProblemMetadata,
  RowData,
} from '@/lib/rebalancer-explorer-types';
import {itemMoves, movedThrough} from '@/lib/run-changes';
import {encodeRunId} from '@/lib/run-id';
import {COLOR, MONO_TEXT, TABULAR, TEXT} from '@/lib/ui-tokens';
import {stringifyQs} from '@/lib/url-state';

import EntityLink from '../EntityLink';
import ObjectiveChanges from './ObjectiveChanges';
import {
  InitialFinal,
  PanelNote,
  PanelSection,
  PanelSkeleton,
  TableLink,
} from './parts';

// Counting an item's objects scans the object table; above this many
// objects the panel does not do it implicitly.
const MAX_OBJECTS_TO_COUNT = 1_000_000;
const UTIL_PREVIEW = 8;
const MOVED_PREVIEW = 6;
const MEMBERS_PREVIEW = 8;

function tableHref(
  runId: string,
  entity: string,
  query: Record<string, unknown>,
): string {
  return `/run/${encodeRunId(runId)}/entity/${encodeURIComponent(
    entity,
  )}?${stringifyQs(query)}`;
}

/** How many objects the item holds at Initial and at Final. */
function useHeldCounts(
  handle: Handle,
  metadata: ProblemMetadata | null,
  key: string,
  id: string,
): {initial: number; final: number} | null {
  const [counts, setCounts] = useState<{
    id: string;
    initial: number;
    final: number;
  } | null>(null);
  const enabled =
    metadata != null && metadata.numObjects <= MAX_OBJECTS_TO_COUNT;
  useEffect(() => {
    if (!enabled || metadata == null) {
      return;
    }
    let cancelled = false;
    const count = (column: string) =>
      fetchEntityData(handle, {
        entity: metadata.objectName,
        filter: {rules: [{stringAny: {column, values: [id]}}]},
        page: {offset: 0, limit: 1},
      }).then(r => r.result.totalCount);
    Promise.all([count(`src.${key}`), count(`dst.${key}`)]).then(
      ([initial, final]) => {
        if (!cancelled) {
          setCounts({id, initial, final});
        }
      },
      () => {
        // The tiles stay out; the rest of the panel works.
      },
    );
    return () => {
      cancelled = true;
    };
  }, [enabled, handle, metadata, key, id]);
  return counts?.id === id ? counts : null;
}

/** Utilization dimensions that changed, largest relative change first. */
function UtilizationChanges({
  fields,
  row,
}: {
  fields: readonly EntityField[];
  row: RowData;
}) {
  const [showAll, setShowAll] = useState(false);
  const {changed, total} = useMemo(() => {
    const util = fields.filter(f => f.kind === 'utilization' && f.a != null);
    const items = util
      .map(f => {
        const {a, b} = fieldValues(f, row);
        return {
          key: f.key,
          a: typeof a === 'number' ? a : 0,
          b: typeof b === 'number' ? b : 0,
        };
      })
      .filter(x => x.a !== x.b)
      .map(x => ({
        ...x,
        rel: Math.abs(x.b - x.a) / Math.max(Math.abs(x.a), Math.abs(x.b)),
      }))
      .sort(
        (x, y) => y.rel - x.rel || Math.abs(y.b - y.a) - Math.abs(x.b - x.a),
      );
    return {changed: items, total: util.length};
  }, [fields, row]);
  if (total === 0) {
    return null;
  }
  const shown = showAll ? changed : changed.slice(0, UTIL_PREVIEW);
  return (
    <PanelSection
      title="Utilization that changed"
      sub={
        changed.length > 0
          ? `${formatCount(changed.length)} of ${formatCount(total)} dimensions · initial → final, each on its own scale`
          : undefined
      }>
      {changed.length === 0 ? (
        <PanelNote>{`All ${formatCount(total)} dimensions are the same at Initial and Final.`}</PanelNote>
      ) : (
        <>
          {shown.map(x => (
            <Box
              key={x.key}
              sx={{
                display: 'grid',
                gridTemplateColumns:
                  'minmax(0, 1fr) 96px minmax(96px, auto) 64px',
                columnGap: 1.5,
                alignItems: 'center',
                py: 0.75,
                fontSize: TEXT.sm,
              }}>
              <Box
                component="span"
                title={x.key}
                sx={{
                  ...MONO_TEXT,
                  minWidth: 0,
                  overflow: 'hidden',
                  textOverflow: 'ellipsis',
                  whiteSpace: 'nowrap',
                }}>
                {x.key}
              </Box>
              <BaselineCurrentBars a={x.a} b={x.b} />
              <Box component="span" sx={{textAlign: 'right'}}>
                <InitialFinal a={x.a} b={x.b} />
              </Box>
              <Box
                component="span"
                sx={{
                  textAlign: 'right',
                  color: COLOR.textSecondary,
                  fontSize: TEXT.xs,
                  ...TABULAR,
                }}>
                {formatSignedCompact(x.b - x.a)}
              </Box>
            </Box>
          ))}
          {changed.length > UTIL_PREVIEW && (
            <Button
              size="small"
              onClick={() => setShowAll(v => !v)}
              sx={{mt: 0.5, ml: -0.5}}>
              {showAll
                ? 'Show fewer'
                : `Show ${formatCount(changed.length - UTIL_PREVIEW)} more`}
            </Button>
          )}
        </>
      )}
    </PanelSection>
  );
}

/** One side of the moves: the objects that left, or that arrived. */
function MovedList({
  title,
  objects,
  other,
  otherEntity,
  href,
}: {
  title: string;
  objects: readonly string[];
  other: (object: string) => string;
  otherEntity: string;
  href: string;
}) {
  const {metadata} = useProblemMetadata();
  if (metadata == null || objects.length === 0) {
    return null;
  }
  return (
    <Box sx={{minWidth: 0}}>
      <Box
        sx={{
          display: 'flex',
          alignItems: 'baseline',
          gap: 1,
          mb: 0.5,
          fontSize: TEXT.xs,
          color: COLOR.textTertiary,
        }}>
        <Box component="span" sx={{flex: 1}}>
          {`${title} · ${formatCount(objects.length)}`}
        </Box>
        {objects.length > MOVED_PREVIEW && (
          <TableLink
            href={href}>{`All ${formatCount(objects.length)}`}</TableLink>
        )}
      </Box>
      {objects.slice(0, MOVED_PREVIEW).map(o => (
        <Box
          key={o}
          sx={{
            display: 'grid',
            gridTemplateColumns: 'minmax(0, 1fr) auto minmax(0, 1fr)',
            columnGap: 0.75,
            alignItems: 'baseline',
            py: 0.5,
            fontSize: TEXT.sm,
          }}>
          <Box component="span" sx={{minWidth: 0}}>
            <EntityLink entity={metadata.objectName} id={o} />
          </Box>
          <Box component="span" sx={{color: COLOR.textDecorative}}>
            {title.startsWith('Out') ? '→' : '←'}
          </Box>
          <Box component="span" sx={{minWidth: 0}}>
            {other(o) !== '' ? (
              <EntityLink entity={otherEntity} id={other(o)} />
            ) : (
              '—'
            )}
          </Box>
        </Box>
      ))}
    </Box>
  );
}

/**
 * A container's or scope item's overview: how many objects it held Initial
 * → Final, what moved out and in, what changed on it and which objectives
 * those moves changed, and what it belongs to or contains.
 */
export default function ItemOverview({
  handle,
  runId,
  entity,
  id,
  fields,
  row,
  moves,
}: {
  handle: Handle;
  runId: string;
  entity: string;
  id: string;
  fields: readonly EntityField[];
  row: RowData;
  moves: RunMoves | null;
}) {
  const {metadata} = useProblemMetadata();
  const held = useHeldCounts(handle, metadata, entity, id);
  const through = useMemo(
    () => (moves?.moved != null ? movedThrough(moves.moved, entity, id) : null),
    [moves, entity, id],
  );
  const [members, setMembers] = useState<{
    id: string;
    names: string[];
    total: number;
  } | null>(null);
  const isScope = metadata?.scopeNames.includes(entity) ?? false;
  useEffect(() => {
    if (!isScope || metadata == null) {
      return;
    }
    let cancelled = false;
    fetchEntityData(handle, {
      entity: metadata.containerName,
      filter: {rules: [{stringAny: {column: entity, values: [id]}}]},
      page: {offset: 0, limit: MEMBERS_PREVIEW},
    }).then(
      ({result}) => {
        if (cancelled) {
          return;
        }
        const pk = result.columns.findIndex(c => c.primaryKey);
        setMembers({
          id,
          names: result.rows.map(r => r.cells[pk]?.stringValue ?? ''),
          total: result.totalCount,
        });
      },
      () => {},
    );
    return () => {
      cancelled = true;
    };
  }, [isScope, handle, metadata, entity, id]);
  const touchedContainers = useMemo(
    () =>
      moves?.moved != null && metadata != null
        ? itemMoves(moves.moved, metadata.containerName)
        : null,
    [moves, metadata],
  );

  if (metadata == null) {
    return null;
  }
  const objectNoun = metadata.objectName;
  const out = through?.out ?? [];
  const into = through?.in ?? [];
  const where = (o: string) => moves?.moved?.get(o)?.where.get(entity);
  const movedHref = (rules: Filter['rules']) =>
    tableHref(runId, metadata.objectName, {filters: [{rules}], show: 'moved'});
  const memberships = isScope
    ? []
    : fields
        .filter(f => f.kind === 'scope')
        .map(f => ({key: f.key, value: fieldValues(f, row).b}))
        .filter(m => m.value != null && m.value !== '');
  const contained = isScope && members?.id === id ? members : null;

  return (
    <Box sx={{display: 'flex', flexDirection: 'column', gap: 3}}>
      <Box
        sx={{
          display: 'grid',
          gridTemplateColumns: 'repeat(3, minmax(0, 1fr))',
          gap: 1,
        }}>
        <PanelValue label={`Initial · ${objectNoun}`} side="A">
          {held != null ? formatCount(held.initial) : '—'}
        </PanelValue>
        <PanelValue label={`Final · ${objectNoun}`} side="B">
          {held != null ? formatCount(held.final) : '—'}
        </PanelValue>
        <PanelValue label="Moved out · in">
          {through != null
            ? `${formatCount(out.length)} · ${formatCount(into.length)}`
            : '—'}
        </PanelValue>
      </Box>

      <UtilizationChanges fields={fields} row={row} />

      {through != null && (out.length > 0 || into.length > 0) ? (
        <PanelSection title="Moves" sub={`${objectNoun} that left or arrived`}>
          <Box
            sx={{
              display: 'grid',
              gridTemplateColumns: 'repeat(auto-fit, minmax(260px, 1fr))',
              gap: 2,
            }}>
            <MovedList
              title="Out, to"
              objects={out}
              other={o => where(o)?.dst ?? ''}
              otherEntity={entity}
              href={movedHref([
                {stringAny: {column: `src.${entity}`, values: [id]}},
                {stringNe: {column: `dst.${entity}`, value: id}},
              ])}
            />
            <MovedList
              title="In, from"
              objects={into}
              other={o => where(o)?.src ?? ''}
              otherEntity={entity}
              href={movedHref([
                {stringAny: {column: `dst.${entity}`, values: [id]}},
                {stringNe: {column: `src.${entity}`, value: id}},
              ])}
            />
          </Box>
        </PanelSection>
      ) : moves != null ? (
        <PanelNote>{`No ${objectNoun} moved in or out.`}</PanelNote>
      ) : null}

      {(out.length > 0 || into.length > 0) && (
        <ObjectiveChanges
          handle={handle}
          objects={[...out, ...into]}
          title="Objectives these moves changed"
        />
      )}

      {memberships.length > 0 && (
        <PanelSection title="Belongs to" sub="its item in each scope">
          {memberships.map(m => (
            <Box
              key={m.key}
              sx={{
                display: 'grid',
                gridTemplateColumns: 'minmax(96px, 30%) minmax(0, 1fr)',
                columnGap: 2,
                py: 0.75,
                fontSize: TEXT.sm,
              }}>
              <Box
                component="span"
                sx={{color: COLOR.textTertiary, overflowWrap: 'anywhere'}}>
                {m.key}
              </Box>
              <Box component="span" sx={{minWidth: 0}}>
                <EntityLink entity={m.key} id={String(m.value)} />
              </Box>
            </Box>
          ))}
        </PanelSection>
      )}

      {isScope && (
        <PanelSection
          title="Contains"
          sub={
            contained != null
              ? `${formatCount(contained.total)} ${metadata.containerName} ${contained.total === 1 ? 'row' : 'rows'}`
              : undefined
          }
          action={
            contained != null && contained.total > contained.names.length ? (
              <TableLink
                href={tableHref(runId, metadata.containerName, {
                  filters: [
                    {rules: [{stringAny: {column: entity, values: [id]}}]},
                  ],
                  show: 'all',
                })}>
                {`All ${formatCount(contained.total)}`}
              </TableLink>
            ) : undefined
          }>
          {contained == null ? (
            <PanelSkeleton rows={2} />
          ) : (
            contained.names.map(name => {
              const m = touchedContainers?.get(name);
              return (
                <Box
                  key={name}
                  sx={{
                    display: 'flex',
                    alignItems: 'baseline',
                    gap: 1,
                    py: 0.5,
                    fontSize: TEXT.sm,
                  }}>
                  <Box component="span" sx={{minWidth: 0, flex: 1}}>
                    <EntityLink entity={metadata.containerName} id={name} />
                  </Box>
                  {m != null && (
                    <Box
                      component="span"
                      sx={{
                        color: COLOR.textTertiary,
                        fontSize: TEXT.xs,
                        ...TABULAR,
                      }}>
                      {`${formatCount(m.out)} out · ${formatCount(m.in)} in`}
                    </Box>
                  )}
                </Box>
              );
            })
          )}
        </PanelSection>
      )}
    </Box>
  );
}
