'use client';

import {useCallback, useRef, useState} from 'react';

import {
  Box,
  Button,
  Collapse,
  IconButton,
  Paper,
  TextField,
  ToggleButton,
  ToggleButtonGroup,
  Tooltip,
  Typography,
} from '@mui/material';
import {ArrowLeftRight, ChevronDown} from 'lucide-react';

import {useRegisterComparison} from '@/app/components/shell/CompareContext';
import HintIcon from '@/app/components/ui/HintIcon';
import {Count} from '@/app/components/ui/Pill';
import {COLOR, radius, TEXT, WEIGHT} from '@/lib/ui-tokens';

import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';

import AssignmentSelectorMenu from './AssignmentSelectorMenu';
import LocalSearchNavigator from './LocalSearchNavigator';
import MoveList from './MoveList';

/**
 * UI-level assignment type, mirroring the www RebalancerExplorerAssignment.
 * Converted to the thrift Assignment type when making API calls.
 */
export interface UIAssignment {
  base: 'INITIAL' | 'FINAL' | 'INTERMEDIATE';
  overrides: Array<{variable: string; container: string}>;
  step: number | null;
}

export interface Assignments {
  src: UIAssignment;
  dst: UIAssignment;
}

interface AssignmentSelectorProps {
  header: string;
  helpMessage: string;
  assignment: UIAssignment;
  onChange: (assignment: UIAssignment) => void;
  hasFinalAssignment: boolean;
  hasIntermediateAssignment: boolean;
  numSteps: number;
  showMenu: boolean;
}

function AssignmentSelector({
  header,
  helpMessage,
  assignment,
  onChange,
  hasFinalAssignment,
  hasIntermediateAssignment,
  numSteps,
  showMenu,
}: AssignmentSelectorProps) {
  const handleBaseChange = (newBase: UIAssignment['base']) => {
    const newAssignment: UIAssignment = {...assignment, base: newBase};

    if (newBase === 'INTERMEDIATE' && assignment.step == null) {
      if (assignment.base === 'INITIAL') {
        newAssignment.step = 0;
      }
      if (assignment.base === 'FINAL') {
        newAssignment.step = numSteps;
      }
    }

    if (newBase === 'INITIAL' || newBase === 'FINAL') {
      newAssignment.step = null;
    }

    onChange(newAssignment);
  };

  const disabledTip = {
    FINAL: hasFinalAssignment
      ? 'Assignment solution proposed by Rebalancer'
      : 'No final assignment available',
    INTERMEDIATE: hasIntermediateAssignment
      ? 'Intermediate assignment after applying the first k moveSets'
      : 'No intermediate assignments available',
  };

  return (
    // An editable area: dashed, on surface-2.
    <Box
      sx={{
        p: 2,
        minWidth: 0,
        border: `1px dashed ${COLOR.borderStrong}`,
        borderRadius: radius('md'),
        bgcolor: COLOR.bgSurface2,
      }}>
      <Box sx={{display: 'flex', alignItems: 'center', gap: 0.75, mb: 1.5}}>
        <Typography variant="subtitle1" component="h3">
          {header}
        </Typography>
        <HintIcon title={helpMessage} label={`About ${header}`} />
        {showMenu && (
          <Box sx={{ml: 'auto'}}>
            <AssignmentSelectorMenu
              assignment={assignment}
              hasFinalAssignment={hasFinalAssignment}
              onAssignmentChange={onChange}
            />
          </Box>
        )}
      </Box>
      <Box sx={{display: 'flex', flexDirection: 'column', gap: 2}}>
        <Box>
          <Typography variant="overline" component="div" sx={{mb: 0.5}}>
            Base
          </Typography>
          <ToggleButtonGroup
            size="small"
            exclusive
            aria-label={`${header} base`}
            value={assignment.base}
            onChange={(_e, value: UIAssignment['base'] | null) => {
              if (value != null) {
                handleBaseChange(value);
              }
            }}>
            <ToggleButton value="INITIAL">Initial</ToggleButton>
            {/* The span keeps the tooltip working on a disabled button. */}
            <Tooltip title={disabledTip.FINAL}>
              <span style={{display: 'flex'}}>
                <ToggleButton value="FINAL" disabled={!hasFinalAssignment}>
                  Final
                </ToggleButton>
              </span>
            </Tooltip>
            <Tooltip title={disabledTip.INTERMEDIATE}>
              <span style={{display: 'flex'}}>
                <ToggleButton
                  value="INTERMEDIATE"
                  disabled={!hasIntermediateAssignment}>
                  Intermediate
                </ToggleButton>
              </span>
            </Tooltip>
          </ToggleButtonGroup>
          {hasIntermediateAssignment && assignment.base === 'INTERMEDIATE' && (
            <TextField
              label="MoveSet #"
              type="number"
              size="small"
              value={assignment.step ?? 0}
              onChange={e => {
                const val = parseInt(e.target.value, 10);
                if (!isNaN(val)) {
                  onChange({
                    ...assignment,
                    step: Math.max(0, Math.min(numSteps, val)),
                  });
                }
              }}
              slotProps={{
                htmlInput: {min: 0, max: numSteps, step: 1},
              }}
              sx={{mt: 1.5, width: 120, display: 'block'}}
            />
          )}
        </Box>

        <Box>
          <Box sx={{display: 'flex', alignItems: 'center', gap: 1, mb: 0.5}}>
            <Typography variant="overline" component="div">
              Moves
            </Typography>
            <Count>{assignment.overrides.length}</Count>
          </Box>
          <MoveList
            moves={assignment.overrides}
            onChange={newOverrides =>
              onChange({...assignment, overrides: newOverrides})
            }
          />
        </Box>
      </Box>
    </Box>
  );
}

function baseLabel(assignment: UIAssignment): string {
  switch (assignment.base) {
    case 'INITIAL':
      return 'Initial';
    case 'FINAL':
      return 'Final';
    case 'INTERMEDIATE':
      return `Intermediate · step ${assignment.step ?? 0}`;
  }
}

/**
 * One side of the comparison: "A ● Initial +2 moves", with the baseline (A,
 * grey) or current (B, blue) dot of the Compare control.
 */
function AssignmentSummary({
  label,
  assignment,
}: {
  label: 'A' | 'B';
  assignment: UIAssignment;
}) {
  const moves = assignment.overrides.length;
  return (
    <Box sx={{display: 'flex', alignItems: 'center', gap: 0.75, minWidth: 0}}>
      <Box
        component="span"
        sx={{color: COLOR.textTertiary, fontWeight: WEIGHT.semibold}}>
        {label}
      </Box>
      <Box
        component="span"
        aria-hidden
        sx={{
          width: 8,
          height: 8,
          borderRadius: '50%',
          flexShrink: 0,
          bgcolor: label === 'A' ? COLOR.baseline : COLOR.current,
        }}
      />
      <Box component="span" sx={{fontWeight: WEIGHT.semibold}}>
        {baseLabel(assignment)}
      </Box>
      <Box
        component="span"
        sx={{
          color: COLOR.textTertiary,
          fontSize: TEXT.xs,
          whiteSpace: 'nowrap',
        }}>
        {`+${moves} ${moves === 1 ? 'move' : 'moves'}`}
      </Box>
    </Box>
  );
}

/** "Initial", "Step 12", with "+N moves" when it has overrides. */
function compareLabel(side: 'A' | 'B', assignment: UIAssignment): string {
  const moves = assignment.overrides.length;
  return `${side} · ${baseLabel(assignment)}${moves > 0 ? ` +${moves}` : ''}`;
}

interface AssignmentCardProps {
  assignments: Assignments;
  onAssignmentsChange: (assignments: Assignments) => void;
}

export default function AssignmentCard({
  assignments,
  onAssignmentsChange,
}: AssignmentCardProps) {
  const {metadata} = useProblemMetadata();

  const hasFinalAssignment = metadata?.hasFinalAssignment ?? false;
  const hasIntermediateAssignment =
    metadata?.hasIntermediateAssignment ?? false;
  const numSteps = metadata?.numSteps ?? 0;

  // Collapsed by default only for the plain comparison; a link with moves or
  // an intermediate step opens with the editor showing.
  const [editorOpen, setEditorOpen] = useState(
    () =>
      assignments.src.overrides.length > 0 ||
      assignments.dst.overrides.length > 0 ||
      assignments.src.base === 'INTERMEDIATE' ||
      assignments.dst.base === 'INTERMEDIATE',
  );

  const swap = () =>
    onAssignmentsChange({src: assignments.dst, dst: assignments.src});

  // The run bar's Compare control shows these sides and opens this editor.
  const cardRef = useRef<HTMLDivElement>(null);
  const openEditor = useCallback(() => {
    setEditorOpen(true);
    cardRef.current?.scrollIntoView({block: 'nearest', behavior: 'smooth'});
  }, []);
  useRegisterComparison(
    compareLabel('A', assignments.src),
    compareLabel('B', assignments.dst),
    openEditor,
  );

  return (
    <Paper variant="outlined" ref={cardRef} sx={{scrollMarginTop: 120}}>
      <Box
        sx={{
          display: 'flex',
          alignItems: 'center',
          flexWrap: 'wrap',
          columnGap: 2,
          rowGap: 1,
          px: 2,
          py: 1.25,
        }}>
        <Typography variant="h6" component="h2">
          Assignments to compare
        </Typography>
        <Box
          sx={{
            display: 'flex',
            alignItems: 'center',
            flexWrap: 'wrap',
            gap: 1,
            minWidth: 0,
          }}>
          <AssignmentSummary label="A" assignment={assignments.src} />
          <Tooltip title="Exchange assignments">
            <IconButton
              size="small"
              aria-label="Exchange assignments"
              onClick={swap}>
              <ArrowLeftRight size={16} />
            </IconButton>
          </Tooltip>
          <AssignmentSummary label="B" assignment={assignments.dst} />
        </Box>
        <Box
          sx={{
            ml: 'auto',
            display: 'flex',
            alignItems: 'center',
            flexWrap: 'wrap',
            gap: 1,
          }}>
          {hasIntermediateAssignment && (
            <LocalSearchNavigator
              assignments={assignments}
              onAssignmentsChange={onAssignmentsChange}
              numSteps={numSteps}
            />
          )}
          <Button
            size="small"
            variant="outlined"
            aria-expanded={editorOpen}
            endIcon={
              <ChevronDown
                size={16}
                style={{
                  transform: editorOpen ? 'rotate(180deg)' : undefined,
                  transition: 'transform 150ms',
                }}
              />
            }
            onClick={() => setEditorOpen(open => !open)}>
            {editorOpen ? 'Hide editor' : 'Edit comparison'}
          </Button>
        </Box>
      </Box>
      <Collapse in={editorOpen} unmountOnExit>
        <Box
          sx={{
            display: 'grid',
            gridTemplateColumns: {xs: '1fr', md: '1fr auto 1fr'},
            alignItems: 'start',
            gap: 2,
            p: 2,
            borderTop: `1px solid ${COLOR.borderDefault}`,
          }}>
          <AssignmentSelector
            header="Assignment A"
            helpMessage="The first of two assignments to compare"
            assignment={assignments.src}
            onChange={src => onAssignmentsChange({...assignments, src})}
            hasFinalAssignment={hasFinalAssignment}
            hasIntermediateAssignment={hasIntermediateAssignment}
            numSteps={numSteps}
            showMenu
          />
          <Box sx={{alignSelf: 'center', justifySelf: 'center'}}>
            <Tooltip title="Exchange assignments">
              <IconButton
                size="small"
                aria-label="Exchange assignments"
                onClick={swap}
                sx={{transform: {xs: 'rotate(90deg)', md: 'none'}}}>
                <ArrowLeftRight size={16} />
              </IconButton>
            </Tooltip>
          </Box>
          <AssignmentSelector
            header="Assignment B"
            helpMessage="The second of two assignments to compare"
            assignment={assignments.dst}
            onChange={dst => onAssignmentsChange({...assignments, dst})}
            hasFinalAssignment={hasFinalAssignment}
            hasIntermediateAssignment={hasIntermediateAssignment}
            numSteps={numSteps}
            showMenu
          />
        </Box>
      </Collapse>
    </Paper>
  );
}
