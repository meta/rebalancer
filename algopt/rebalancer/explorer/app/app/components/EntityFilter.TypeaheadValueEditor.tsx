'use client';

import {useEffect, useMemo, useRef, useState} from 'react';

import {Autocomplete, Chip, TextField} from '@mui/material';

import {TAG_CHIP_SX} from '@/app/components/ui/Tag';
import type {ValueEditorProps} from 'react-querybuilder';

import {AUTOCOMPLETE_WORD_BREAK_PROPS} from '@/lib/format';
import {fetchTypeahead} from '@/lib/rebalancer-explorer-api';

import {OPERATOR_TO_COMPARATOR} from './EntityFilter.transform';

export function TypeaheadValueEditor(props: ValueEditorProps) {
  const {handleOnChange, value, operator, context} = props;
  const {handle, entityName, columnTypeaheadEntities, localOptions} =
    (context as {
      handle: unknown;
      entityName: string;
      columnTypeaheadEntities?: Record<string, string>;
      localOptions?: Record<string, string[]>;
    }) ?? {};

  const fieldName = props.fieldData?.name ?? props.field;

  // Check for local options first — if available, skip backend typeahead entirely
  const localList = localOptions?.[fieldName] ?? null;

  // When an explicit columnTypeaheadEntities map is provided (e.g. MoveSetsTable),
  // only use typeahead for columns with a mapping — unmapped columns like "Move #"
  // have no backend entity and would cause "Unknown entity" errors.
  // When no map is provided (entity pages), use the column name itself as the
  // typeahead entity — on entity pages column names ARE entity names (e.g. a
  // column called "rack" contains rack entity names).
  const selectedColumn =
    localList != null
      ? null
      : columnTypeaheadEntities != null
        ? (columnTypeaheadEntities[fieldName] ?? null)
        : fieldName;

  const [inputValue, setInputValue] = useState('');
  const [remoteOptions, setRemoteOptions] = useState<string[]>([]);
  const [loading, setLoading] = useState(false);
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  // Local filtering when localOptions are provided
  const localFilteredOptions = useMemo(() => {
    if (localList == null) return null;
    const query = inputValue.toLowerCase();
    return query === ''
      ? localList.slice(0, 20)
      : localList.filter(v => v.toLowerCase().includes(query)).slice(0, 20);
  }, [inputValue, localList]);

  // Debounced typeahead fetch — fetches even with empty input to show initial results
  const canFetchRemote =
    localList == null && handle != null && selectedColumn != null;

  useEffect(() => {
    if (!canFetchRemote) return;

    if (debounceRef.current != null) {
      clearTimeout(debounceRef.current);
    }

    debounceRef.current = setTimeout(() => {
      setLoading(true);
      void fetchTypeahead(
        handle as Parameters<typeof fetchTypeahead>[0],
        selectedColumn!,
        inputValue,
        20,
      )
        .then(res => setRemoteOptions(res.matches))
        .catch(() => setRemoteOptions([]))
        .finally(() => setLoading(false));
    }, 300);

    return () => {
      if (debounceRef.current != null) {
        clearTimeout(debounceRef.current);
      }
    };
  }, [inputValue, canFetchRemote, handle, selectedColumn]);

  const options = localFilteredOptions ?? remoteOptions;

  if (operator === 'in') {
    const currentValues =
      typeof value === 'string'
        ? value
            .split(',')
            .map(v => v.trim())
            .filter(Boolean)
        : [];

    return (
      <Autocomplete
        multiple
        freeSolo
        size="small"
        options={options}
        value={currentValues}
        loading={loading}
        filterOptions={x => x}
        onInputChange={(_, val) => setInputValue(val)}
        onChange={(_, vals) => handleOnChange(vals.join(','))}
        renderInput={params => (
          <TextField
            {...params}
            size="small"
            placeholder="Type to search..."
            sx={{minWidth: 200}}
          />
        )}
        renderTags={(tagValues, getTagProps) =>
          tagValues.map((option, index) => {
            const {key, ...tagProps} = getTagProps({index});
            return (
              <Chip
                key={key}
                label={option}
                size="small"
                sx={TAG_CHIP_SX}
                {...tagProps}
              />
            );
          })
        }
        slotProps={AUTOCOMPLETE_WORD_BREAK_PROPS}
      />
    );
  }

  if (operator === 'notEqual') {
    return (
      <Autocomplete
        freeSolo
        size="small"
        options={options}
        value={typeof value === 'string' ? value : ''}
        loading={loading}
        filterOptions={x => x}
        onInputChange={(_, val) => {
          setInputValue(val);
          handleOnChange(val);
        }}
        onChange={(_, val) => handleOnChange((val as string) ?? '')}
        renderInput={params => (
          <TextField
            {...params}
            size="small"
            placeholder="Type to search..."
            sx={{minWidth: 200}}
          />
        )}
        slotProps={AUTOCOMPLETE_WORD_BREAK_PROPS}
      />
    );
  }

  // Default: simple text input (regex and numeric values)
  return (
    <TextField
      size="small"
      value={typeof value === 'string' ? value : ''}
      onChange={e => handleOnChange(e.target.value)}
      placeholder={operator === 'regex' ? 'e.g. ^server-.*' : ''}
      type={
        Object.keys(OPERATOR_TO_COMPARATOR).includes(operator)
          ? 'number'
          : 'text'
      }
      sx={{minWidth: 200}}
    />
  );
}
