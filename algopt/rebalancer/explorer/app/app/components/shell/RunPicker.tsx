'use client';

import {useState} from 'react';

import {
  Box,
  Button,
  ButtonBase,
  Popover,
  TextField,
  Tooltip,
  Typography,
} from '@mui/material';
import {ChevronDown} from 'lucide-react';
import {useRouter} from 'next/navigation';

import CopyButton from '@/app/components/ui/CopyButton';
import {encodeRunId} from '@/lib/run-id';
import {middleEllipsis} from '@/lib/run-page';
import {
  COLOR,
  CONTROL,
  DURATION,
  EYEBROW_TEXT,
  MONO_TEXT,
  radius,
  TEXT,
  WEIGHT,
} from '@/lib/ui-tokens';

/**
 * The object picker of the top bar: the run's service and id. It opens the
 * run's full id and a field to open another run.
 */
export default function RunPicker({
  runId,
  serviceName,
}: {
  runId: string;
  serviceName: string;
}) {
  const router = useRouter();
  const [anchor, setAnchor] = useState<HTMLElement | null>(null);
  const [next, setNext] = useState('');
  const close = () => {
    setAnchor(null);
    setNext('');
  };
  const open = (e: React.FormEvent) => {
    e.preventDefault();
    const id = next.trim();
    if (id.length > 0) {
      close();
      router.push(`/run/${encodeRunId(id)}`);
    }
  };
  return (
    <>
      <Tooltip title="This run: open another">
        <ButtonBase
          aria-haspopup="dialog"
          aria-expanded={anchor != null}
          aria-label={`Run ${runId} of ${serviceName}. Open another run`}
          onClick={e => setAnchor(e.currentTarget)}
          sx={{
            display: 'flex',
            alignItems: 'center',
            gap: 1,
            height: CONTROL.md,
            minWidth: 0,
            maxWidth: 460,
            pl: 1.5,
            pr: 1.25,
            border: `1px solid ${COLOR.borderDefault}`,
            borderRadius: radius('md'),
            bgcolor: COLOR.bgSurface2,
            font: 'inherit',
            color: COLOR.textPrimary,
            transition: `border-color ${DURATION.fast}ms`,
            '&:hover': {borderColor: COLOR.borderStrong},
            '@media (max-width: 760px)': {display: 'none'},
          }}>
          {serviceName.length > 0 && (
            <Box
              component="span"
              sx={{
                fontWeight: WEIGHT.semibold,
                whiteSpace: 'nowrap',
                overflow: 'hidden',
                textOverflow: 'ellipsis',
                minWidth: 0,
              }}>
              {serviceName}
            </Box>
          )}
          <Box
            component="span"
            sx={{
              ...MONO_TEXT,
              color: COLOR.textTertiary,
              whiteSpace: 'nowrap',
              '@media (max-width: 1180px)': {display: 'none'},
            }}>
            {middleEllipsis(runId)}
          </Box>
          <Box
            component="span"
            aria-hidden
            sx={{display: 'flex', color: COLOR.textDecorative}}>
            <ChevronDown size={16} />
          </Box>
        </ButtonBase>
      </Tooltip>
      <Popover
        open={anchor != null}
        anchorEl={anchor}
        onClose={close}
        anchorOrigin={{vertical: 'bottom', horizontal: 'left'}}
        transformOrigin={{vertical: -6, horizontal: 'left'}}
        slotProps={{paper: {sx: {width: 420, maxWidth: '94vw', p: 2}}}}>
        <Typography component="div" sx={EYEBROW_TEXT}>
          This run
        </Typography>
        <Box sx={{display: 'flex', alignItems: 'center', gap: 0.5, mt: 0.5}}>
          <Box
            component="span"
            sx={{...MONO_TEXT, minWidth: 0, overflowWrap: 'anywhere'}}>
            {runId}
          </Box>
          <CopyButton value={runId} label="Copy run ID" toast="Run ID copied" />
        </Box>
        {serviceName.length > 0 && (
          <Typography
            variant="body2"
            sx={{color: COLOR.textTertiary, fontSize: TEXT.xs}}>
            {serviceName}
          </Typography>
        )}
        <Box
          component="form"
          onSubmit={open}
          sx={{
            display: 'flex',
            alignItems: 'center',
            gap: 1,
            mt: 2,
            pt: 2,
            borderTop: `1px solid ${COLOR.borderDefault}`,
          }}>
          <TextField
            size="small"
            fullWidth
            autoFocus
            placeholder="Run ID of another run"
            value={next}
            onChange={e => setNext(e.target.value)}
            slotProps={{htmlInput: {'aria-label': 'Run ID of another run'}}}
          />
          <Button
            type="submit"
            variant="contained"
            disabled={next.trim().length === 0}>
            Open
          </Button>
        </Box>
      </Popover>
    </>
  );
}
