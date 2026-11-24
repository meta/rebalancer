'use client';

import {useCallback, useEffect, useRef, useState} from 'react';

import {Autocomplete, TextField} from '@mui/material';

import {useRebalancerHandle} from '@/lib/contexts/RebalancerHandleContext';
import {AUTOCOMPLETE_WORD_BREAK_PROPS} from '@/lib/format';
import {fetchTypeahead} from '@/lib/rebalancer-explorer-api';
import {MONO_TEXT} from '@/lib/ui-tokens';

const TYPEAHEAD_LIMIT = 10;

// Entity names are identifiers: show options in mono and let long ones wrap.
const TYPEAHEAD_SLOT_PROPS = {
  listbox: {
    sx: {
      ...AUTOCOMPLETE_WORD_BREAK_PROPS.listbox.sx,
      '& .MuiAutocomplete-option': {
        ...AUTOCOMPLETE_WORD_BREAK_PROPS.listbox.sx[
          '& .MuiAutocomplete-option'
        ],
        ...MONO_TEXT,
      },
    },
  },
} as const;
const DEBOUNCE_MS = 200;

interface EntityTypeaheadProps {
  /** The entity type to search (e.g. variableName or containerName from metadata). */
  entity: string;
  /** Label displayed on the input. */
  label: string;
  /** The currently selected value (controlled). */
  value: string | null;
  /** Called when the selected value changes. */
  onChange: (value: string | null) => void;
}

export default function EntityTypeahead({
  entity,
  label,
  value,
  onChange,
}: EntityTypeaheadProps) {
  const {handle} = useRebalancerHandle();
  const [options, setOptions] = useState<string[]>([]);
  const [inputValue, setInputValue] = useState('');
  const [loading, setLoading] = useState(false);
  const debounceTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  const doSearch = useCallback(
    async (query: string) => {
      if (handle == null) {
        setOptions([]);
        return;
      }

      setLoading(true);
      try {
        const response = await fetchTypeahead(
          handle,
          entity,
          query,
          TYPEAHEAD_LIMIT,
        );
        setOptions(response.matches);
      } catch {
        setOptions([]);
      } finally {
        setLoading(false);
      }
    },
    [handle, entity],
  );

  useEffect(() => {
    if (debounceTimer.current != null) {
      clearTimeout(debounceTimer.current);
    }

    debounceTimer.current = setTimeout(() => {
      doSearch(inputValue);
    }, DEBOUNCE_MS);

    return () => {
      if (debounceTimer.current != null) {
        clearTimeout(debounceTimer.current);
      }
    };
  }, [inputValue, doSearch]);

  return (
    <Autocomplete
      size="small"
      options={options}
      loading={loading}
      value={value}
      inputValue={inputValue}
      onInputChange={(_event, newInputValue) => {
        setInputValue(newInputValue);
      }}
      onChange={(_event, newValue) => {
        onChange(newValue);
      }}
      filterOptions={x => x}
      renderInput={params => <TextField {...params} label={label} />}
      noOptionsText="No matches"
      slotProps={TYPEAHEAD_SLOT_PROPS}
    />
  );
}
