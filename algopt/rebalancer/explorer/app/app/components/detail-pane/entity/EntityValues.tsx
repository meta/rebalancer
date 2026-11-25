'use client';

import {useMemo, useState} from 'react';

import {Box, Button, Link, TextField, Typography} from '@mui/material';
import {ArrowUpRight} from 'lucide-react';

import CopyButton from '@/app/components/ui/CopyButton';
import {
  buildFields,
  fieldValues,
  isBlank,
  type CellValue,
  type EntityField,
  type FieldKind,
} from '@/lib/entity-fields';
import {formatCount, formatSignedCompact} from '@/lib/format';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import type {Result} from '@/lib/rebalancer-explorer-types';
import {encodeRunId} from '@/lib/run-id';
import {COLOR, EYEBROW_TEXT, TEXT} from '@/lib/ui-tokens';
import {stringifyQs} from '@/lib/url-state';

import EntityLink from '../EntityLink';

const DETAIL_SECTIONS: ReadonlyArray<{
  kind: FieldKind;
  title: string;
  sub?: string;
}> = [
  {kind: 'assignment', title: 'Assignment', sub: 'initial → final'},
  {kind: 'scope', title: 'Scopes'},
  {kind: 'partition', title: 'Partitions'},
  {kind: 'utilization', title: 'Utilization', sub: 'initial → final'},
  {kind: 'dimension', title: 'Dimensions'},
  {kind: 'name', title: 'Names'},
  {kind: 'count', title: 'Counts'},
  {kind: 'other', title: 'Other'},
];

/** Up to 6 significant digits; the exact value is in the title and copy. */
function formatDetail(v: number): string {
  if (Number.isInteger(v)) {
    return formatCount(v);
  }
  return String(Number(v.toPrecision(6))).replace('-', '−');
}

// Kinds whose values name an entity.
const NAMING_KINDS = new Set<FieldKind>(['name', 'assignment', 'scope']);

/**
 * Every value of one entity row, grouped by kind: changed values first,
 * values that are 0 or empty behind one button.
 */
export default function EntityValues({
  runId,
  entity,
  id,
  primaryKey,
  result,
}: {
  runId: string;
  entity: string;
  id: string;
  primaryKey: string;
  result: Result;
}) {
  const [query, setQuery] = useState('');
  const [showBlank, setShowBlank] = useState(false);
  const {metadata} = useProblemMetadata();

  // A value that names another object, container or scope item opens it as
  // the next panel.
  const linkEntityFor = useMemo(() => {
    const known = new Set(
      metadata == null
        ? []
        : [metadata.objectName, metadata.containerName, ...metadata.scopeNames],
    );
    return (field: EntityField): string | null => {
      if (!NAMING_KINDS.has(field.kind)) {
        return null;
      }
      const name = field.columns[field.columns.length - 1];
      if (known.has(name)) {
        return name;
      }
      const dot = name.indexOf('.');
      const suffix = dot >= 0 ? name.substring(dot + 1) : null;
      return suffix != null && known.has(suffix) ? suffix : null;
    };
  }, [metadata]);

  const sections = useMemo(() => {
    if (result.rows.length === 0) {
      return null;
    }
    const row = result.rows[0];
    const needle = query.trim().toLowerCase();
    const fields = buildFields(result.columns).filter(f => !f.primaryKey);
    const items = fields.map(f => {
      const {a, b} = fieldValues(f, row);
      return {f, a, b, blank: isBlank(a) && isBlank(b), changed: a !== b};
    });
    const matching = items.filter(
      ({f, a, b}) =>
        needle === '' ||
        f.label.toLowerCase().includes(needle) ||
        String(a ?? '')
          .toLowerCase()
          .includes(needle) ||
        String(b ?? '')
          .toLowerCase()
          .includes(needle),
    );
    return {
      total: items.length,
      blank: matching.filter(x => x.blank).length,
      groups: DETAIL_SECTIONS.map(section => ({
        ...section,
        items: matching
          .filter(x => x.f.kind === section.kind && (showBlank || !x.blank))
          // Changed values first, then in column order.
          .sort((x, y) => Number(y.changed) - Number(x.changed)),
      })).filter(g => g.items.length > 0),
    };
  }, [result, query, showBlank]);

  if (sections == null) {
    return null;
  }

  // Dynamic dimensions have their own page, like in the sidebar.
  const section = metadata?.dynamicDimensionNames.includes(entity)
    ? 'dynamic-dimensions'
    : 'entity';
  const tableHref = `/run/${encodeRunId(runId)}/${section}/${encodeURIComponent(
    entity,
  )}?${stringifyQs({
    filters: [{rules: [{stringAny: {column: primaryKey, values: [id]}}]}],
    show: 'all',
  })}`;

  return (
    <Box>
      <Box sx={{display: 'flex', alignItems: 'center', gap: 1, mb: 1.5}}>
        <Typography variant="subtitle2" sx={{flex: 1}}>
          {`${sections.total} values`}
        </Typography>
        {/* A full navigation: the entity view reads its filters from the URL
            on mount, including when this is the page already open. */}
        <Link
          href={tableHref}
          sx={{display: 'inline-flex', alignItems: 'center', gap: 0.5}}>
          Open in table
          <ArrowUpRight size={14} aria-hidden />
        </Link>
      </Box>
      {sections.total > 12 && (
        <TextField
          size="small"
          fullWidth
          placeholder="Find a value"
          value={query}
          onChange={e => setQuery(e.target.value)}
          slotProps={{htmlInput: {'aria-label': 'Find a value'}}}
          sx={{mb: 1.5}}
        />
      )}
      <Box sx={{display: 'flex', flexDirection: 'column', gap: 2}}>
        {sections.groups.map(group => (
          <Box component="section" key={group.kind}>
            <Box
              component="h3"
              sx={{
                ...EYEBROW_TEXT,
                fontFamily: 'inherit',
                m: 0,
                mb: 0.5,
                display: 'flex',
                gap: 0.75,
              }}>
              {group.title}
              {group.sub != null && (
                <Box
                  component="span"
                  sx={{textTransform: 'none', letterSpacing: 0}}>
                  {group.sub}
                </Box>
              )}
            </Box>
            <Box
              component="dl"
              sx={{
                m: 0,
                display: 'grid',
                gridTemplateColumns: 'minmax(120px, 38%) minmax(0, 1fr) 24px',
                alignItems: 'start',
                columnGap: 2,
              }}>
              {group.items.map(({f, a, b, changed}) => {
                const linkEntity = linkEntityFor(f);
                const text = (v: CellValue) =>
                  v == null ? '—' : typeof v === 'number' ? String(v) : v;
                const shown = (v: CellValue) =>
                  typeof v === 'number' ? formatDetail(v) : text(v);
                const value = (v: CellValue, strong: boolean) =>
                  linkEntity != null &&
                  typeof v === 'string' &&
                  !(linkEntity === entity && v === id) ? (
                    <Box component="span" sx={{minWidth: 0}}>
                      <EntityLink entity={linkEntity} id={v} />
                    </Box>
                  ) : (
                    <Box
                      component="span"
                      sx={{
                        overflowWrap: 'anywhere',
                        fontVariantNumeric: 'tabular-nums',
                        fontWeight: strong ? 600 : undefined,
                        color:
                          v == null || v === 0 ? COLOR.textTertiary : undefined,
                      }}
                      title={typeof v === 'number' ? String(v) : undefined}>
                      {shown(v)}
                    </Box>
                  );
                return (
                  <Box key={f.key} sx={{display: 'contents'}}>
                    <Box
                      component="dt"
                      title={f.columns.join(' · ')}
                      sx={{
                        py: 0.75,
                        minWidth: 0,
                        color: COLOR.textTertiary,
                        fontSize: TEXT.sm,
                        overflowWrap: 'anywhere',
                      }}>
                      {f.label}
                    </Box>
                    <Box
                      component="dd"
                      sx={{
                        m: 0,
                        display: 'flex',
                        alignItems: 'center',
                        flexWrap: 'wrap',
                        columnGap: 0.5,
                        py: 0.75,
                        minWidth: 0,
                        fontSize: TEXT.sm,
                      }}>
                      {changed ? (
                        <>
                          <Box
                            component="span"
                            sx={{color: COLOR.textTertiary, minWidth: 0}}>
                            {value(a, false)}
                          </Box>
                          <Box
                            component="span"
                            aria-label="to"
                            sx={{color: COLOR.textDecorative}}>
                            →
                          </Box>
                          {value(b, true)}
                          {typeof a === 'number' && typeof b === 'number' && (
                            <Box
                              component="span"
                              sx={{
                                color: COLOR.textTertiary,
                                fontSize: TEXT.xs,
                                fontVariantNumeric: 'tabular-nums',
                              }}>
                              ({formatSignedCompact(b - a)})
                            </Box>
                          )}
                        </>
                      ) : (
                        value(b, false)
                      )}
                    </Box>
                    <Box component="dd" sx={{m: 0, pt: 0.5}}>
                      <CopyButton
                        value={text(b)}
                        label={`Copy ${f.label}`}
                        size={12}
                      />
                    </Box>
                  </Box>
                );
              })}
            </Box>
          </Box>
        ))}
      </Box>
      {sections.blank > 0 && (
        <Button
          size="small"
          onClick={() => setShowBlank(v => !v)}
          sx={{mt: 1.5, ml: -0.5}}>
          {showBlank
            ? 'Hide the values that are 0 or empty'
            : `Show ${sections.blank} more ${sections.blank === 1 ? 'value' : 'values'} that are 0 or empty`}
        </Button>
      )}
    </Box>
  );
}
