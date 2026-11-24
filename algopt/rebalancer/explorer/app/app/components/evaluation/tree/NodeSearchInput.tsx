'use client';

import {useEffect, useRef, useState} from 'react';

import {InputAdornment, TextField} from '@mui/material';
import {Search} from 'lucide-react';

export function NodeSearchInput({
  itemId,
  nodeSearch,
  onSearchChange,
}: {
  itemId: string;
  nodeSearch: Record<string, string>;
  onSearchChange: (nodeId: string, query: string) => void;
}) {
  const [localSearch, setLocalSearch] = useState(nodeSearch[itemId] ?? '');
  // Refs let the debounced callback see the latest parent values without
  // restarting the timer on every render.
  const onSearchChangeRef = useRef(onSearchChange);
  const nodeSearchRef = useRef(nodeSearch);
  useEffect(() => {
    onSearchChangeRef.current = onSearchChange;
    nodeSearchRef.current = nodeSearch;
  });
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const handleChange = (value: string) => {
    setLocalSearch(value);
    if (debounceRef.current != null) {
      clearTimeout(debounceRef.current);
    }
    debounceRef.current = setTimeout(() => {
      if (value !== (nodeSearchRef.current[itemId] ?? '')) {
        onSearchChangeRef.current(itemId, value);
      }
    }, 500);
  };

  return (
    <div onClick={e => e.stopPropagation()}>
      <TextField
        size="small"
        placeholder="Search children..."
        value={localSearch}
        onChange={e => handleChange(e.target.value)}
        slotProps={{
          input: {
            startAdornment: (
              <InputAdornment position="start">
                <Search size={14} />
              </InputAdornment>
            ),
          },
        }}
        sx={{
          ml: 1,
          width: 220,
          '& .MuiInputBase-root': {height: 28, typography: 'caption'},
        }}
      />
    </div>
  );
}
