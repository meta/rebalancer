'use client';

import {useEffect, useState} from 'react';

import {
  Alert,
  Box,
  Button,
  CircularProgress,
  Dialog,
  DialogActions,
  DialogContent,
  DialogContentText,
  DialogTitle,
  FormControlLabel,
  Radio,
  RadioGroup,
  Skeleton,
  Snackbar,
  TextField,
  Tooltip,
  useMediaQuery,
  type Theme,
} from '@mui/material';

import Pill from '@/app/components/ui/Pill';

import type {Handle} from '@/lib/rebalancer-explorer-types';
import {
  DEFAULT_EXPIRATION_DAYS,
  extendBundleExpiration,
  fetchBundleExpiration,
  isValidExpirationDays,
} from '@/lib/rebalancer-explorer-api';

type ExpirationOption = 'days' | 'never';

export default function BundleExpirationButton({handle}: {handle: Handle}) {
  const [expiresAt, setExpiresAt] = useState<number | null>(null);
  const [loadingExpiration, setLoadingExpiration] = useState(true);
  const [loadError, setLoadError] = useState(false);
  const [open, setOpen] = useState(false);
  const [selected, setSelected] = useState<ExpirationOption>('days');
  const [days, setDays] = useState(DEFAULT_EXPIRATION_DAYS.toString());
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [confirmation, setConfirmation] = useState<string | null>(null);
  const fullScreen = useMediaQuery((theme: Theme) =>
    theme.breakpoints.down('sm'),
  );

  useEffect(() => {
    let cancelled = false;
    setLoadingExpiration(true);
    setLoadError(false);

    fetchBundleExpiration(handle)
      .then(response => {
        if (!cancelled) {
          setExpiresAt(response.expiresAt);
          setLoadingExpiration(false);
        }
      })
      .catch(() => {
        if (!cancelled) {
          setLoadError(true);
          setLoadingExpiration(false);
        }
      });

    return () => {
      cancelled = true;
    };
  }, [handle]);

  const closeDialog = () => {
    if (submitting) {
      return;
    }
    setOpen(false);
    setError(null);
  };

  const openDialog = () => {
    setSelected(expiresAt === 0 ? 'never' : 'days');
    setDays(DEFAULT_EXPIRATION_DAYS.toString());
    setError(null);
    setOpen(true);
  };

  const parsedDays = Number(days);
  const isPositiveWholeNumber =
    Number.isSafeInteger(parsedDays) && parsedDays > 0;
  const validDays = isPositiveWholeNumber && isValidExpirationDays(parsedDays);
  const daysError = !isPositiveWholeNumber
    ? 'Enter a positive whole number'
    : !validDays
      ? 'That extension is too large'
      : undefined;

  const handleExtend = async () => {
    if (selected === 'days' && !validDays) {
      return;
    }

    setSubmitting(true);
    setError(null);
    try {
      const extensionDays = selected === 'never' ? 0 : parsedDays;
      const {expiresAt: nextExpiresAt} = await extendBundleExpiration(
        handle,
        extensionDays,
      );
      setExpiresAt(nextExpiresAt);
      setLoadError(false);
      setOpen(false);
      setConfirmation(
        nextExpiresAt === 0
          ? 'This run will never expire.'
          : `Expiration extended to ${new Date(nextExpiresAt * 1000).toLocaleString()}.`,
      );
    } catch (extendError) {
      setError(
        extendError instanceof Error
          ? extendError.message
          : 'Failed to extend expiration',
      );
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <>
      <Box
        component="span"
        sx={{
          display: 'inline-flex',
          alignItems: 'center',
          flexWrap: 'wrap',
          gap: 1,
        }}>
        {loadingExpiration ? (
          <Skeleton width={160} />
        ) : loadError ? (
          <Tooltip title="Could not load the expiration time">
            <span>
              <Pill tone="bad">Unavailable</Pill>
            </span>
          </Tooltip>
        ) : (
          <span>
            {expiresAt === 0
              ? 'Never expires'
              : new Date((expiresAt ?? 0) * 1000).toLocaleString()}
          </span>
        )}
        <Button
          size="small"
          variant="outlined"
          onClick={openDialog}
          sx={{whiteSpace: 'nowrap'}}>
          Extend expiration
        </Button>
      </Box>
      <Dialog
        open={open}
        onClose={closeDialog}
        maxWidth="xs"
        fullWidth
        fullScreen={fullScreen}>
        <DialogTitle>Extend Expiration</DialogTitle>
        <DialogContent>
          <DialogContentText sx={{mb: 2}}>
            Choose how many days to add to the current expiration, or keep this
            run permanently.
          </DialogContentText>
          {error != null && (
            <Alert severity="error" sx={{mb: 2}}>
              {error}
            </Alert>
          )}
          <RadioGroup
            value={selected}
            onChange={event =>
              setSelected(event.target.value as ExpirationOption)
            }>
            <FormControlLabel
              value="days"
              control={<Radio />}
              disabled={expiresAt === 0}
              label="Extend by number of days"
            />
            {selected === 'days' && (
              <TextField
                sx={{mb: 1, ml: 4}}
                label="Days to add"
                type="number"
                size="small"
                value={days}
                onChange={event => setDays(event.target.value)}
                error={!validDays}
                helperText={daysError}
                slotProps={{
                  htmlInput: {min: 1, step: 1},
                }}
              />
            )}
            <FormControlLabel
              value="never"
              control={<Radio />}
              label="Never expire"
            />
          </RadioGroup>
        </DialogContent>
        <DialogActions>
          <Button onClick={closeDialog} disabled={submitting}>
            Cancel
          </Button>
          <Button
            variant="contained"
            onClick={handleExtend}
            disabled={submitting || (selected === 'days' && !validDays)}
            startIcon={submitting ? <CircularProgress size={16} /> : undefined}>
            Extend
          </Button>
        </DialogActions>
      </Dialog>
      <Snackbar
        open={confirmation != null}
        autoHideDuration={6000}
        onClose={() => setConfirmation(null)}
        message={confirmation}
      />
    </>
  );
}
