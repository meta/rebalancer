'use client';

import {useMemo, useState} from 'react';

import {
  Alert,
  Button,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  TextField,
  useMediaQuery,
  type Theme,
} from '@mui/material';

import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {MONO_TEXT} from '@/lib/ui-tokens';

import type {UIAssignment} from './AssignmentCard';

interface BulkMovesDialogProps {
  assignment: UIAssignment;
  onAssignmentChange: (assignment: UIAssignment) => void;
  onClose: () => void;
}

export default function BulkMovesDialog({
  assignment,
  onAssignmentChange,
  onClose,
}: BulkMovesDialogProps) {
  const {metadata} = useProblemMetadata();
  const variableName = metadata?.variableName ?? 'variable';
  const containerName = metadata?.containerName ?? 'container';

  const initialText = useMemo(
    () =>
      assignment.overrides
        .map(({variable, container}) => `${variable} ${container}`)
        .join('\n'),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [],
  );

  const fullScreen = useMediaQuery((theme: Theme) =>
    theme.breakpoints.down('sm'),
  );
  const [text, setText] = useState(initialText);
  const [error, setError] = useState<string | null>(null);
  const [temporaryOverrides, setTemporaryOverrides] = useState(
    assignment.overrides,
  );

  const handleTextChange = (newText: string) => {
    setText(newText);
    const table = newText
      .split('\n')
      .map(line => line.trim())
      .filter(line => line.length !== 0)
      .map(line => line.split(/\s+/));

    const invalidIndex = table.findIndex(row => row.length !== 2);
    if (invalidIndex === -1) {
      const moves = table.map(row => ({variable: row[0], container: row[1]}));
      setError(null);
      setTemporaryOverrides(moves);
    } else {
      setError(
        `Invalid format in line ${invalidIndex + 1}, expected 2 columns but got ${table[invalidIndex].length}: "${table[invalidIndex].join(', ')}"`,
      );
    }
  };

  const handleSave = () => {
    onAssignmentChange({...assignment, overrides: temporaryOverrides});
    onClose();
  };

  return (
    <Dialog
      open
      onClose={onClose}
      maxWidth="md"
      fullWidth
      fullScreen={fullScreen}>
      <DialogTitle>Bulk moves editor</DialogTitle>
      <DialogContent>
        {error != null && (
          <Alert severity="error" sx={{mb: 2}}>
            {error}
          </Alert>
        )}
        <TextField
          multiline
          fullWidth
          minRows={8}
          maxRows={20}
          label={`One move per line. Format: {${variableName}} {destination ${containerName}}`}
          placeholder={`Enter one move per line in format: {${variableName}} {destination ${containerName}}`}
          value={text}
          onChange={e => handleTextChange(e.target.value)}
          sx={{
            mt: 1,
            '& textarea': MONO_TEXT,
          }}
        />
      </DialogContent>
      <DialogActions>
        <Button onClick={onClose}>Cancel</Button>
        <Button
          variant="contained"
          disabled={error != null}
          onClick={handleSave}>
          Save
        </Button>
      </DialogActions>
    </Dialog>
  );
}
