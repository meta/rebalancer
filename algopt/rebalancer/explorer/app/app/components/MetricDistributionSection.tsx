'use client';

import {useMemo, useState} from 'react';

import {Autocomplete, Box, Chip, Paper, TextField} from '@mui/material';

import SectionHeader from '@/app/components/ui/SectionHeader';
import {TAG_CHIP_SX} from '@/app/components/ui/Tag';

import {AUTOCOMPLETE_WORD_BREAK_PROPS} from '@/lib/format';
import type {ColumnDescription} from '@/lib/rebalancer-explorer-types';
import {columnTypeInfo} from '@/lib/column-types';
import {ColumnType} from '@/lib/rebalancer-explorer-types';

import MetricDistributionGraph from './MetricDistributionGraph';

interface MetricDistributionSectionProps {
  entityName: string;
  columnDescriptions: ColumnDescription[];
}

const METRIC_TYPES = new Set([ColumnType.DIMENSION, ColumnType.UTILIZATION]);

const GROUP_ORDER = ['Dimensions', 'Utilizations'];

export default function MetricDistributionSection({
  entityName,
  columnDescriptions,
}: MetricDistributionSectionProps) {
  const metricColumns = useMemo(
    () => columnDescriptions.filter(col => METRIC_TYPES.has(col.type)),
    [columnDescriptions],
  );

  const [selectedMetrics, setSelectedMetrics] = useState<ColumnDescription[]>(
    [],
  );

  if (metricColumns.length === 0) {
    return null;
  }

  const selectedNames = selectedMetrics.map(col => col.name);

  return (
    <Paper variant="outlined">
      <SectionHeader title="Metric distributions" />
      <Box sx={{p: 2, display: 'flex', flexDirection: 'column', gap: 2}}>
        <Autocomplete
          multiple
          options={metricColumns}
          getOptionLabel={option => option.name}
          groupBy={option => columnTypeInfo(option.type).group}
          value={selectedMetrics}
          onChange={(_event, newValue) => setSelectedMetrics(newValue)}
          isOptionEqualToValue={(option, value) => option.name === value.name}
          size="small"
          sx={{maxWidth: 640}}
          renderInput={params => (
            <TextField {...params} label="Metrics to plot" />
          )}
          renderTags={(value, getTagProps) =>
            value.map((option, index) => {
              const {key, ...rest} = getTagProps({index});
              return (
                <Chip
                  key={key}
                  label={option.name}
                  size="small"
                  sx={TAG_CHIP_SX}
                  {...rest}
                />
              );
            })
          }
          slotProps={{
            listbox: {
              sx: {
                ...AUTOCOMPLETE_WORD_BREAK_PROPS.listbox.sx,
                '& .MuiAutocomplete-groupLabel': {
                  typography: 'overline',
                  lineHeight: 2.5,
                },
              },
            },
          }}
          filterOptions={(options, {inputValue}) => {
            const lowerInput = inputValue.toLowerCase();
            const filtered = options.filter(opt =>
              opt.name.toLowerCase().includes(lowerInput),
            );
            filtered.sort((a, b) => {
              const groupA = columnTypeInfo(a.type).group;
              const groupB = columnTypeInfo(b.type).group;
              const orderDiff =
                GROUP_ORDER.indexOf(groupA) - GROUP_ORDER.indexOf(groupB);
              if (orderDiff !== 0) return orderDiff;
              return a.name.localeCompare(b.name);
            });
            return filtered;
          }}
        />

        {selectedNames.length > 0 && (
          <MetricDistributionGraph
            entityName={entityName}
            selectedMetrics={selectedNames}
          />
        )}
      </Box>
    </Paper>
  );
}
