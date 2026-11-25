'use client';

import type {ReactNode} from 'react';

import {Box, Link, Skeleton, Typography} from '@mui/material';
import {ArrowRight} from 'lucide-react';

import {formatCompact} from '@/lib/format';
import {COLOR, TABULAR, TEXT, WEIGHT} from '@/lib/ui-tokens';

/** A section of the panel: a 14px title, a 12px sub-line, an action. */
export function PanelSection({
  title,
  sub,
  action,
  children,
}: {
  title: string;
  sub?: ReactNode;
  action?: ReactNode;
  children: ReactNode;
}) {
  return (
    <Box component="section" sx={{minWidth: 0}}>
      <Box
        sx={{
          display: 'flex',
          alignItems: 'baseline',
          flexWrap: 'wrap',
          columnGap: 1,
          rowGap: 0.25,
          mb: 1,
        }}>
        <Typography
          component="h3"
          sx={{fontSize: TEXT.md, fontWeight: WEIGHT.semibold, m: 0}}>
          {title}
        </Typography>
        {sub != null && (
          <Typography
            component="span"
            sx={{fontSize: TEXT.xs, color: COLOR.textTertiary, flex: 1}}>
            {sub}
          </Typography>
        )}
        {action}
      </Box>
      {children}
    </Box>
  );
}

/** "All 118 in the table →": a link that leaves the panel for a page. */
export function TableLink({
  href,
  children,
}: {
  href: string;
  children: ReactNode;
}) {
  return (
    <Link
      href={href}
      sx={{
        display: 'inline-flex',
        alignItems: 'center',
        gap: 0.5,
        fontSize: TEXT.xs,
        whiteSpace: 'nowrap',
      }}>
      {children}
      <ArrowRight size={12} aria-hidden />
    </Link>
  );
}

/** A quiet line of text for a section with nothing to list. */
export function PanelNote({children}: {children: ReactNode}) {
  return (
    <Typography sx={{fontSize: TEXT.sm, color: COLOR.textTertiary}}>
      {children}
    </Typography>
  );
}

/** Rows of placeholder text while a section loads. */
export function PanelSkeleton({rows = 3}: {rows?: number}) {
  return (
    <Box>
      {Array.from({length: rows}, (_, i) => (
        <Skeleton key={i} variant="text" height={26} />
      ))}
    </Box>
  );
}

/** "A → B" with the final value emphasized; just B when they are equal. */
export function InitialFinal({
  a,
  b,
  format = formatCompact,
}: {
  a: number;
  b: number;
  format?: (v: number) => string;
}) {
  return (
    <Box
      component="span"
      sx={{whiteSpace: 'nowrap', ...TABULAR}}
      title={a === b ? String(b) : `${a} → ${b}`}>
      {a !== b && (
        <>
          <Box component="span" sx={{color: COLOR.textTertiary}}>
            {format(a)}
          </Box>
          <Box
            component="span"
            aria-label="to"
            sx={{color: COLOR.textDecorative, mx: 0.5}}>
            →
          </Box>
        </>
      )}
      <Box
        component="span"
        sx={{fontWeight: a !== b ? WEIGHT.semibold : undefined}}>
        {format(b)}
      </Box>
    </Box>
  );
}
