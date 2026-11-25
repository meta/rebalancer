'use client';

import {useMemo} from 'react';

import {Box} from '@mui/material';

import {
  FINAL,
  INITIAL,
  MAX_MOVE_ROWS,
  useRunMoveSets,
  useRunObjectives,
} from '@/app/components/entity/useRunChanges';
import Delta, {ChangeIcon, changeTone} from '@/app/components/ui/Delta';
import Pill from '@/app/components/ui/Pill';
import TupleBadge from '@/app/components/ui/TupleBadge';
import {formatCount, formatSignedCompact} from '@/lib/format';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import type {Handle} from '@/lib/rebalancer-explorer-types';
import {objectiveChanges, setsOf} from '@/lib/run-changes';
import {COLOR, focusRing, radius, TEXT} from '@/lib/ui-tokens';

import {useDetailPaneActions} from '../DetailPaneContext';
import {PanelNote, PanelSection, PanelSkeleton} from './parts';

/**
 * The objectives that the move sets of `objects` changed, highest priority
 * first: the link from an entity to the goals. Each set counts once, and a
 * row opens the objective in the next panel.
 */
export default function ObjectiveChanges({
  handle,
  objects,
  title,
}: {
  handle: Handle;
  objects: readonly string[];
  title: string;
}) {
  const {metadata} = useProblemMetadata();
  const {moveSets, loading, failed} = useRunMoveSets(handle, metadata);
  const {objectives} = useRunObjectives(handle, metadata);
  const {open} = useDetailPaneActions();

  const view = useMemo(() => {
    if (moveSets == null) {
      return null;
    }
    const sets = setsOf(moveSets, objects);
    const own = new Set(objects);
    const shared = new Set(
      moveSets.rows
        .filter(r => !own.has(r.object) && sets.includes(r.set))
        .map(r => r.set),
    ).size;
    const rows = objectiveChanges(moveSets, sets)
      .map(c => ({...c, info: objectives?.byName.get(c.name) ?? null}))
      .sort(
        (x, y) =>
          (x.info?.tupleIndex ?? Infinity) - (y.info?.tupleIndex ?? Infinity) ||
          Math.abs(y.delta) - Math.abs(x.delta),
      );
    return {
      sets,
      shared,
      rows,
      unchanged: moveSets.objectiveNames.length - rows.length,
    };
  }, [moveSets, objects, objectives]);

  if (metadata == null) {
    return null;
  }
  const unit = metadata.hasOnlySingleMoves ? 'move' : 'move set';
  const setNoun = (n: number) =>
    `${formatCount(n)} ${unit}${n === 1 ? '' : 's'}`;

  let body;
  if (loading) {
    body = <PanelSkeleton rows={2} />;
  } else if (!metadata.canDisplayObjChangesInMoveSetsTable) {
    body = (
      <PanelNote>
        {`This run was saved without each ${unit}’s objective changes.`}
      </PanelNote>
    );
  } else if (view == null) {
    body = (
      <PanelNote>
        {failed
          ? 'The move sets couldn’t be loaded.'
          : `This run has more than ${formatCount(MAX_MOVE_ROWS)} moves, too many to connect here. Constraints & Objectives lists them under Move sets.`}
      </PanelNote>
    );
  } else if (view.sets.length === 0) {
    body = <PanelNote>{`No ${unit} moved these objects.`}</PanelNote>;
  } else if (view.rows.length === 0) {
    body = (
      <PanelNote>{`These ${setNoun(view.sets.length)} left every objective unchanged.`}</PanelNote>
    );
  } else {
    body = (
      <Box component="ul" sx={{m: 0, p: 0, listStyle: 'none'}}>
        {view.rows.map(({name, delta, info}) => {
          const tone = changeTone(delta);
          const total = info != null ? info.delta : null;
          return (
            <Box component="li" key={name}>
              <Box
                component="button"
                type="button"
                disabled={info == null}
                onClick={() => {
                  if (info == null || objectives == null) {
                    return;
                  }
                  open(
                    {
                      kind: 'expression',
                      tab: 'tree',
                      payload: {
                        ...info,
                        expressionType: 'OBJECTIVE',
                        handle,
                        sourceAssignment: INITIAL,
                        destinationAssignment: FINAL,
                        srcLabel: 'Initial',
                        dstLabel: 'Final',
                        maxTupleIndex: objectives.maxTupleIndex,
                      },
                    },
                    {push: true},
                  );
                }}
                sx={{
                  width: '100%',
                  display: 'grid',
                  gridTemplateColumns: '30px minmax(0, 1fr) auto',
                  alignItems: 'center',
                  columnGap: 1.25,
                  px: 1,
                  py: 0.75,
                  mx: -1,
                  border: 0,
                  borderRadius: radius('md'),
                  bgcolor: 'transparent',
                  font: 'inherit',
                  color: 'inherit',
                  textAlign: 'left',
                  cursor: info != null ? 'pointer' : 'default',
                  '&:hover:not(:disabled)': {bgcolor: COLOR.bgSurface2},
                  '&:focus-visible': focusRing(-2),
                }}>
                {info != null ? (
                  <TupleBadge
                    tupleIndex={info.tupleIndex}
                    maxTupleIndex={objectives?.maxTupleIndex ?? 0}
                    hint={null}
                  />
                ) : (
                  <span />
                )}
                <Box
                  component="span"
                  title={name}
                  sx={{
                    minWidth: 0,
                    fontSize: TEXT.sm,
                    color: info != null ? COLOR.action : undefined,
                    display: '-webkit-box',
                    WebkitLineClamp: 2,
                    WebkitBoxOrient: 'vertical',
                    overflow: 'hidden',
                    overflowWrap: 'anywhere',
                  }}>
                  {name}
                </Box>
                <Box
                  component="span"
                  sx={{
                    display: 'flex',
                    flexDirection: 'column',
                    alignItems: 'flex-end',
                  }}>
                  <Box
                    component="span"
                    sx={{
                      display: 'inline-flex',
                      alignItems: 'center',
                      gap: 0.75,
                    }}>
                    <ChangeIcon delta={delta} tone={tone} />
                    <Delta tone={tone}>{formatSignedCompact(delta)}</Delta>
                  </Box>
                  {total != null &&
                    Math.abs(total - delta) >
                      1e-9 * Math.max(1, Math.abs(total)) && (
                      <Box
                        component="span"
                        sx={{fontSize: TEXT.xs, color: COLOR.textTertiary}}
                        title="The objective's change over the whole run, Initial → Final">
                        {`of ${formatSignedCompact(total)} in the run`}
                      </Box>
                    )}
                </Box>
              </Box>
            </Box>
          );
        })}
      </Box>
    );
  }

  return (
    <PanelSection
      title={title}
      sub={
        view != null && view.sets.length > 0
          ? `Summed over ${setNoun(view.sets.length)} · lower is better`
          : undefined
      }
      action={
        view != null && view.unchanged > 0 && view.rows.length > 0 ? (
          <Pill title="Objectives these move sets did not change">
            {`${formatCount(view.unchanged)} unchanged`}
          </Pill>
        ) : undefined
      }>
      {body}
      {view != null && view.shared > 0 && view.rows.length > 0 && (
        <Box sx={{mt: 0.75, fontSize: TEXT.xs, color: COLOR.textTertiary}}>
          {`${setNoun(view.shared)} also moved other objects; a set’s change covers all of its moves.`}
        </Box>
      )}
    </PanelSection>
  );
}
