'use client';

import {useState} from 'react';

import {Box, IconButton, TextField, Tooltip} from '@mui/material';
import {Plus, X} from 'lucide-react';

import Tag from '@/app/components/ui/Tag';

import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';

import EntityTypeahead from './EntityTypeahead';

interface Move {
  variable: string;
  container: string;
}

interface MoveListProps {
  moves: Move[];
  onChange: (moves: Move[]) => void;
}

export default function MoveList({moves, onChange}: MoveListProps) {
  const {metadata} = useProblemMetadata();
  const variableName = metadata?.variableName;
  const containerName = metadata?.containerName;

  const [variable, setVariable] = useState<string | null>(null);
  const [container, setContainer] = useState<string | null>(null);

  const isDuplicate =
    variable != null &&
    container != null &&
    moves.some(m => m.variable === variable && m.container === container);
  const canAdd = variable != null && container != null && !isDuplicate;

  const handleAdd = () => {
    if (variable == null || container == null) {
      return;
    }
    onChange([...moves, {variable, container}]);
    setVariable(null);
    setContainer(null);
  };

  return (
    <div>
      {moves.length > 0 && (
        <Box
          sx={{
            maxHeight: 200,
            overflowY: 'auto',
            mb: 1,
          }}>
          <div className="flex flex-wrap gap-1">
            {moves.map((move, index) => (
              <Tag
                key={`${index}|${move.variable}|${move.container}`}
                mono
                sx={{userSelect: 'text', gap: 0.5, pr: 0.25}}>
                <span>
                  {move.variable} → {move.container}
                </span>
                <IconButton
                  size="small"
                  aria-label={`Remove move ${move.variable} to ${move.container}`}
                  onClick={() => onChange(moves.filter((_, i) => i !== index))}
                  sx={{p: 0.25, color: 'text.disabled'}}>
                  <X size={12} />
                </IconButton>
              </Tag>
            ))}
          </div>
        </Box>
      )}
      <div className="mt-2 grid grid-cols-1 items-end gap-2 sm:grid-cols-[1fr_1fr_max-content]">
        {variableName != null && containerName != null ? (
          <>
            <EntityTypeahead
              entity={variableName}
              label={variableName}
              value={variable}
              onChange={setVariable}
            />
            <EntityTypeahead
              entity={containerName}
              label={`Destination ${containerName}`}
              value={container}
              onChange={setContainer}
            />
          </>
        ) : (
          <>
            <TextField size="small" label="Variable" disabled />
            <TextField size="small" label="Container" disabled />
          </>
        )}
        <Tooltip title={isDuplicate ? 'This move already exists' : 'Add move'}>
          <span>
            <IconButton
              size="small"
              color="primary"
              aria-label="Add move"
              disabled={!canAdd}
              onClick={handleAdd}>
              <Plus size={18} />
            </IconButton>
          </span>
        </Tooltip>
      </div>
    </div>
  );
}
