'use client';

import {useState, type ReactNode} from 'react';

import {Box, Button} from '@mui/material';

import {useRunMoveSets} from '@/app/components/entity/useRunChanges';
import type {RunMoves} from '@/app/components/entity/useRunChanges';
import Pill from '@/app/components/ui/Pill';
import {fieldValues, type EntityField} from '@/lib/entity-fields';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import type {Handle, RowData} from '@/lib/rebalancer-explorer-types';
import {movedInGroup} from '@/lib/run-changes';
import {COLOR, EYEBROW_TEXT, TABULAR, TEXT, WEIGHT} from '@/lib/ui-tokens';
import {stringifyQs} from '@/lib/url-state';
import {encodeRunId} from '@/lib/run-id';

import EntityLink from '../EntityLink';
import ObjectiveChanges from './ObjectiveChanges';
import {PanelNote, PanelSection, PanelSkeleton, TableLink} from './parts';

function Dot({side}: {side: 'A' | 'B'}) {
  return (
    <Box
      component="span"
      aria-hidden
      sx={{
        display: 'inline-block',
        width: 8,
        height: 8,
        mr: 0.75,
        borderRadius: '50%',
        bgcolor: side === 'A' ? COLOR.baseline : COLOR.current,
      }}
    />
  );
}

/** A value that names an entity opens it; anything else is text. */
function Named({entity, id}: {entity: string | null; id: string}) {
  if (id === '' || id === 'N/A') {
    return (
      <Box component="span" sx={{color: COLOR.textTertiary}}>
        —
      </Box>
    );
  }
  return entity != null ? (
    <Box component="span" sx={{minWidth: 0, display: 'block'}}>
      <EntityLink entity={entity} id={id} />
    </Box>
  ) : (
    <Box component="span" sx={{overflowWrap: 'anywhere'}}>
      {id}
    </Box>
  );
}

const GRID = {
  display: 'grid',
  gridTemplateColumns: 'minmax(96px, 26%) minmax(0, 1fr) minmax(0, 1fr)',
  columnGap: 2,
  alignItems: 'baseline',
} as const;

/**
 * Where the object sits in every scope, Initial and Final: its container
 * first, then the scopes that changed; unchanged scopes fold away.
 */
function Placement({
  fields,
  row,
  containerName,
  entities,
}: {
  fields: readonly EntityField[];
  row: RowData;
  containerName: string;
  entities: ReadonlySet<string>;
}) {
  const [showSame, setShowSame] = useState(false);
  const items = fields
    .filter(f => f.kind === 'assignment' && f.a != null)
    .map(f => {
      const {a, b} = fieldValues(f, row);
      return {
        key: f.key,
        a: a == null ? '' : String(a),
        b: b == null ? '' : String(b),
      };
    })
    .sort(
      (x, y) =>
        Number(y.key === containerName) - Number(x.key === containerName) ||
        Number(y.a !== y.b) - Number(x.a !== x.b),
    );
  const head = items.filter(i => i.key === containerName || i.a !== i.b);
  const same = items.filter(i => i.key !== containerName && i.a === i.b);
  const line = (i: (typeof items)[number]): ReactNode => {
    const entity = entities.has(i.key) ? i.key : null;
    return (
      <Box key={i.key} sx={{...GRID, py: 0.75, fontSize: TEXT.sm}}>
        <Box
          component="span"
          sx={{
            color: COLOR.textTertiary,
            overflowWrap: 'anywhere',
            fontWeight: i.key === containerName ? WEIGHT.semibold : undefined,
          }}>
          {i.key}
        </Box>
        <Named entity={entity} id={i.a} />
        {i.a === i.b ? (
          <Box component="span" sx={{color: COLOR.textTertiary}}>
            same
          </Box>
        ) : (
          <Box component="span" sx={{fontWeight: WEIGHT.semibold, minWidth: 0}}>
            <Named entity={entity} id={i.b} />
          </Box>
        )}
      </Box>
    );
  };
  return (
    <PanelSection title="Placement" sub="where it sits in each scope">
      <Box sx={{...GRID, ...EYEBROW_TEXT, pb: 0.5}}>
        <span>Scope</span>
        <span>
          <Dot side="A" />
          Initial
        </span>
        <span>
          <Dot side="B" />
          Final
        </span>
      </Box>
      {head.map(line)}
      {showSame && same.map(line)}
      {same.length > 0 && (
        <Button
          size="small"
          onClick={() => setShowSame(v => !v)}
          sx={{mt: 0.5, ml: -0.5}}>
          {showSame
            ? 'Hide the unchanged scopes'
            : `Same in ${same.length} more ${same.length === 1 ? 'scope' : 'scopes'}`}
        </Button>
      )}
    </PanelSection>
  );
}

/** The move sets that moved the object, in solver order. */
function Moves({handle, id}: {handle: Handle; id: string}) {
  const {metadata} = useProblemMetadata();
  const {moveSets, loading} = useRunMoveSets(handle, metadata);
  if (metadata == null) {
    return null;
  }
  const rows = moveSets?.rows.filter(r => r.object === id) ?? [];
  if (!loading && rows.length === 0) {
    return null;
  }
  const container = metadata.containerName;
  return (
    <PanelSection title="Moves" sub={`from → to ${container}`}>
      {loading ? (
        <PanelSkeleton rows={1} />
      ) : (
        rows.map((r, i) => (
          <Box key={i} sx={{...GRID, py: 0.75, fontSize: TEXT.sm}}>
            <Box component="span" sx={{color: COLOR.textTertiary, ...TABULAR}}>
              {`${metadata.hasOnlySingleMoves ? 'Move' : 'Move set'} #${r.set}`}
            </Box>
            <Named entity={container} id={r.src} />
            <Box
              component="span"
              sx={{
                display: 'flex',
                gap: 0.75,
                minWidth: 0,
                fontWeight: WEIGHT.semibold,
              }}>
              <Box
                component="span"
                aria-label="to"
                sx={{color: COLOR.textDecorative}}>
                →
              </Box>
              <Named entity={container} id={r.dst} />
            </Box>
          </Box>
        ))
      )}
    </PanelSection>
  );
}

/**
 * The object's groups, one per partition, with how many of each group's
 * objects moved; a group opens its objects in the table.
 */
function Groups({
  fields,
  row,
  runId,
  moves,
}: {
  fields: readonly EntityField[];
  row: RowData;
  runId: string;
  moves: RunMoves | null;
}) {
  const {metadata} = useProblemMetadata();
  if (metadata == null) {
    return null;
  }
  const groups = fields
    .filter(f => f.kind === 'partition')
    .map(f => ({key: f.key, value: fieldValues(f, row).b}))
    .filter(g => g.value != null && g.value !== '');
  if (groups.length === 0) {
    return null;
  }
  return (
    <PanelSection title="Groups" sub="one per partition">
      {groups.map(({key, value}) => {
        const group = String(value);
        const moved =
          moves?.moved != null
            ? movedInGroup(moves.moved, key, group).length
            : null;
        const href = `/run/${encodeRunId(runId)}/entity/${encodeURIComponent(
          metadata.objectName,
        )}?${stringifyQs({
          filters: [{rules: [{stringAny: {column: key, values: [group]}}]}],
          show: 'all',
        })}`;
        return (
          <Box
            key={key}
            sx={{
              display: 'grid',
              gridTemplateColumns: 'minmax(96px, 26%) minmax(0, 1fr) auto',
              columnGap: 2,
              alignItems: 'baseline',
              py: 0.75,
              fontSize: TEXT.sm,
            }}>
            <Box
              component="span"
              sx={{color: COLOR.textTertiary, overflowWrap: 'anywhere'}}>
              {key}
            </Box>
            <Box component="span" sx={{minWidth: 0, overflowWrap: 'anywhere'}}>
              {group}
            </Box>
            <Box
              component="span"
              sx={{display: 'inline-flex', alignItems: 'center', gap: 1}}>
              {moved != null && moved > 1 && (
                <Pill tone="info" title="Objects of this group that moved">
                  {`${moved} moved`}
                </Pill>
              )}
              <TableLink href={href}>Objects</TableLink>
            </Box>
          </Box>
        );
      })}
    </PanelSection>
  );
}

/**
 * An object's overview: where it sits Initial → Final, the move sets that
 * moved it and what they changed, and its groups.
 */
export default function ObjectOverview({
  handle,
  runId,
  id,
  fields,
  row,
  moves,
}: {
  handle: Handle;
  runId: string;
  id: string;
  fields: readonly EntityField[];
  row: RowData;
  moves: RunMoves | null;
}) {
  const {metadata} = useProblemMetadata();
  const {moveSets, loading} = useRunMoveSets(handle, metadata);
  if (metadata == null) {
    return null;
  }
  const moved = moves?.moved?.has(id) ?? false;
  // A move away and back leaves it in place but still moved it.
  const inMoveSets = moveSets?.rows.some(r => r.object === id) ?? false;
  const entities = new Set([metadata.containerName, ...metadata.scopeNames]);
  const movable = fields.find(f => f.key === 'is_movable' && f.numeric);
  const fixed = movable != null && fieldValues(movable, row).b === 0;
  return (
    <Box sx={{display: 'flex', flexDirection: 'column', gap: 3}}>
      {fixed && (
        <Box sx={{display: 'flex', alignItems: 'center', gap: 1}}>
          <Pill>fixed</Pill>
          <PanelNote>The solver may not move it (is_movable is 0).</PanelNote>
        </Box>
      )}
      <Placement
        fields={fields}
        row={row}
        containerName={metadata.containerName}
        entities={entities}
      />
      {moved || inMoveSets ? (
        <>
          <Moves handle={handle} id={id} />
          <ObjectiveChanges
            handle={handle}
            objects={[id]}
            title="Objectives its moves changed"
          />
        </>
      ) : moves != null && !loading ? (
        <PanelNote>
          Not moved, so no move changed an objective through it.
        </PanelNote>
      ) : null}
      <Groups fields={fields} row={row} runId={runId} moves={moves} />
    </Box>
  );
}
