'use client';

import {Button} from '@mui/material';
import {EyeOff} from 'lucide-react';

/** "3 empty columns hidden · Show" next to a table's row count. */
export default function EmptyColumnsToggle({
  count,
  shown,
  onToggle,
}: {
  count: number;
  shown: boolean;
  onToggle: () => void;
}) {
  if (count === 0) {
    return null;
  }
  const noun = count === 1 ? 'column' : 'columns';
  return (
    <Button
      size="small"
      variant="text"
      startIcon={<EyeOff size={14} />}
      onClick={onToggle}
      title="Columns with no value on this page"
      sx={{color: 'text.secondary', fontWeight: 500}}>
      {shown
        ? `Hide ${count} empty ${noun}`
        : `${count} empty ${noun} hidden · Show`}
    </Button>
  );
}
