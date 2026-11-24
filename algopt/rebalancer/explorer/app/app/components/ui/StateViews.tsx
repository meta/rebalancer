'use client';

import {useEffect, useState, type ComponentType, type ReactNode} from 'react';

import {
  Alert,
  AlertTitle,
  Box,
  Typography,
  type SxProps,
  type Theme,
} from '@mui/material';
import {keyframes} from '@mui/material/styles';

import {COLOR, DURATION, TEXT, WEIGHT, Z} from '@/lib/ui-tokens';

const spin = keyframes`to { transform: rotate(360deg); }`;
const sweep = keyframes`from { background-position: -40% 0; } to { background-position: 140% 0; }`;

/** The 14px ring spinner of loading states. */
export function Spinner({size = 14}: {size?: number}) {
  return (
    <Box
      component="span"
      aria-hidden
      sx={{
        display: 'inline-block',
        flexShrink: 0,
        width: size,
        height: size,
        borderRadius: '50%',
        border: `2px solid ${COLOR.borderStrong}`,
        borderTopColor: COLOR.action,
        animation: `${spin} 0.8s linear infinite`,
      }}
    />
  );
}

/** True once `delayMs` has passed: feedback shows only after 120 ms. */
function useDelayed(delayMs: number): boolean {
  const [shown, setShown] = useState(delayMs <= 0);
  useEffect(() => {
    if (delayMs <= 0) {
      return;
    }
    const t = window.setTimeout(() => setShown(true), delayMs);
    return () => window.clearTimeout(t);
  }, [delayMs]);
  return shown;
}

/** The 2px progress bar along the top of the window, after 120 ms. */
export function LoadBar() {
  const shown = useDelayed(DURATION.fast);
  if (!shown) {
    return null;
  }
  return (
    <Box
      aria-hidden
      sx={{
        position: 'fixed',
        top: 0,
        left: 0,
        right: 0,
        height: 2,
        zIndex: Z.progress,
        pointerEvents: 'none',
        backgroundImage: `linear-gradient(90deg, transparent, ${COLOR.action}, transparent)`,
        backgroundSize: '35% 100%',
        backgroundRepeat: 'no-repeat',
        animation: `${sweep} 1.1s linear infinite`,
        '@media (prefers-reduced-motion: reduce)': {
          animation: 'none',
          backgroundImage: 'none',
          bgcolor: COLOR.action,
        },
      }}
    />
  );
}

/** Spinner with a label, for content that is loading. */
export function LoadingState({
  label = 'Loading…',
  compact = false,
}: {
  label?: ReactNode;
  compact?: boolean;
}) {
  const shown = useDelayed(DURATION.fast);
  return (
    <Box
      role="status"
      sx={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: compact ? 'center' : 'flex-start',
        gap: 1.25,
        py: compact ? 2 : 5,
        px: compact ? 2 : 5,
        color: COLOR.textTertiary,
        fontSize: TEXT.sm,
        visibility: shown ? 'visible' : 'hidden',
      }}>
      <LoadBar />
      <Spinner />
      {label}
    </Box>
  );
}

/**
 * A view with nothing to show: ∅, a title that names the filter or the
 * missing data, and what to do. Empty states never blame. `compact` is a
 * single tertiary line, for empty tables and chart slots.
 */
export function EmptyState({
  icon: Icon,
  title,
  children,
  compact = false,
}: {
  icon?: ComponentType<{size?: number; className?: string}>;
  title: ReactNode;
  children?: ReactNode;
  compact?: boolean;
}) {
  const glyph =
    Icon != null ? (
      <Icon size={compact ? 16 : 20} />
    ) : (
      <Box component="span" sx={{fontSize: compact ? TEXT.md : TEXT.xl}}>
        ∅
      </Box>
    );
  if (compact) {
    return (
      <Box
        sx={{
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          gap: 1,
          py: 2.5,
          px: 2,
          color: COLOR.textTertiary,
          fontSize: TEXT.sm,
        }}>
        <Box
          component="span"
          aria-hidden
          sx={{display: 'flex', color: COLOR.textDecorative}}>
          {glyph}
        </Box>
        <Typography variant="body2" component="div" color="inherit">
          {title}
        </Typography>
      </Box>
    );
  }
  return (
    <Box
      sx={{
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        justifyContent: 'center',
        py: 4,
        px: 2.5,
        textAlign: 'center',
        color: COLOR.textTertiary,
      }}>
      <Box
        component="span"
        aria-hidden
        sx={{display: 'flex', color: COLOR.textDecorative}}>
        {glyph}
      </Box>
      <Typography
        component="div"
        sx={{
          mt: 1,
          mb: 0.5,
          fontSize: TEXT.md,
          fontWeight: WEIGHT.semibold,
          color: COLOR.textSecondary,
        }}>
        {title}
      </Typography>
      {children != null && (
        <Typography
          variant="body2"
          component="div"
          color="inherit"
          sx={{maxWidth: 560}}>
          {children}
        </Typography>
      )}
    </Box>
  );
}

/**
 * Errors follow the message formula: what happened, why, what still works,
 * and a button that does something about it. Full width by default, so it
 * lines up with the cards around it; pass `sx` for a margin inside an
 * unpadded card.
 */
export function ErrorState({
  title,
  children,
  action,
  onClose,
  sx,
}: {
  title?: ReactNode;
  children?: ReactNode;
  action?: ReactNode;
  onClose?: () => void;
  sx?: SxProps<Theme>;
}) {
  return (
    <Alert severity="error" action={action} onClose={onClose} sx={sx}>
      {title != null && <AlertTitle>{title}</AlertTitle>}
      {children}
    </Alert>
  );
}
