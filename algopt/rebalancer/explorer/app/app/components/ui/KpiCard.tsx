'use client';

import type {ReactNode} from 'react';

import {Box, Paper} from '@mui/material';
import {Check, TriangleAlert} from 'lucide-react';
import Link from 'next/link';

import HintIcon from '@/app/components/ui/HintIcon';
import {
  COLOR,
  DURATION,
  focusRing,
  KPI_EDGE,
  radius,
  SHADOW_SM,
  TABULAR,
  TEXT,
  TONES,
  TRACKING,
  WEIGHT,
} from '@/lib/ui-tokens';

export type KpiTone = 'good' | 'warn' | 'bad';

const TONE_ICON = {good: Check, warn: TriangleAlert, bad: TriangleAlert};
const TONE_LABEL: Record<KpiTone, string> = {
  good: 'Status: fine',
  warn: 'Status: worth a look',
  bad: 'Status: problem',
};

/**
 * KPI card: one number with a 12px label, a sub-line, an optional status tone
 * (3px edge plus an icon, never color alone) and click-through.
 */
export default function KpiCard({
  label,
  hint,
  href,
  tone,
  children,
  sub,
}: {
  label: string;
  hint?: string;
  href?: string;
  tone?: KpiTone | null;
  /** The value: use <KpiValue>, or words for a word KPI. */
  children: ReactNode;
  sub?: ReactNode;
}) {
  const Icon = tone != null ? TONE_ICON[tone] : null;
  const body = (
    <>
      <Box
        sx={{
          display: 'flex',
          alignItems: 'center',
          gap: 0.75,
          color: COLOR.textTertiary,
          fontSize: TEXT.xs,
          fontWeight: WEIGHT.medium,
        }}>
        {Icon != null && tone != null && (
          <Box
            component="span"
            role="img"
            aria-label={TONE_LABEL[tone]}
            sx={{
              display: 'inline-grid',
              placeItems: 'center',
              width: 17,
              height: 17,
              borderRadius: '50%',
              flexShrink: 0,
              color: TONES[tone].text,
              bgcolor: TONES[tone].bg,
            }}>
            <Icon size={11} strokeWidth={2.5} />
          </Box>
        )}
        <span>{label}</span>
        {hint != null && <HintIcon title={hint} label={`Help: ${label}`} />}
        {href != null && (
          <Box
            className="kpi-go"
            component="span"
            aria-hidden
            sx={{
              ml: 'auto',
              color: COLOR.action,
              fontWeight: WEIGHT.semibold,
              opacity: 0,
              transition: `opacity ${DURATION.fast}ms`,
            }}>
            →
          </Box>
        )}
      </Box>
      <Box
        sx={{
          mt: 0.5,
          display: 'flex',
          alignItems: 'baseline',
          flexWrap: 'wrap',
          columnGap: 1,
          minWidth: 0,
        }}>
        {children}
      </Box>
      {sub != null && (
        <Box
          sx={{
            mt: 0.25,
            color: COLOR.textTertiary,
            fontSize: TEXT.xs,
            lineHeight: 1.45,
          }}>
          {sub}
        </Box>
      )}
    </>
  );
  return (
    <Paper
      variant="outlined"
      sx={{
        position: 'relative',
        px: 2,
        py: 1.75,
        minWidth: 0,
        ...(tone != null && {
          boxShadow: `inset 3px 0 0 ${KPI_EDGE[tone]}, ${SHADOW_SM}`,
        }),
        ...(tone === 'bad' && {
          bgcolor: `color-mix(in srgb, ${COLOR.negativeBg} 60%, ${COLOR.bgSurface})`,
        }),
        ...(href != null && {
          transition: `border-color ${DURATION.fast}ms`,
          '&:hover': {borderColor: COLOR.borderStrong},
          '&:hover .kpi-go, &:focus-within .kpi-go': {opacity: 1},
          '&:focus-within': focusRing(-2),
        }),
      }}>
      {href != null ? (
        <Box
          component={Link}
          href={href}
          sx={{
            display: 'block',
            color: 'inherit',
            textDecoration: 'none',
            '&:focus-visible': {outline: 'none'},
            // The whole card is the target.
            '&::after': {
              content: '""',
              position: 'absolute',
              inset: 0,
              borderRadius: radius('lg'),
            },
          }}>
          {body}
        </Box>
      ) : (
        body
      )}
    </Paper>
  );
}

/** A KPI number: 24px / 650, tabular, with an optional unit or word after. */
export function KpiValue({
  children,
  color,
  unit,
}: {
  children: ReactNode;
  color?: string;
  unit?: ReactNode;
}) {
  return (
    <Box
      component="span"
      sx={{
        fontSize: TEXT['3xl'],
        fontWeight: WEIGHT.heading,
        letterSpacing: TRACKING['3xl'],
        lineHeight: 1.3,
        color: color ?? COLOR.textPrimary,
        ...TABULAR,
      }}>
      {children}
      {unit != null && (
        <Box
          component="small"
          sx={{
            ml: 0.5,
            fontSize: TEXT.sm,
            fontWeight: WEIGHT.medium,
            letterSpacing: 0,
            color: COLOR.textTertiary,
          }}>
          {unit}
        </Box>
      )}
    </Box>
  );
}
