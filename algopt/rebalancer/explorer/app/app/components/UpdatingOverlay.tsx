'use client';

import {Box} from '@mui/material';
import {keyframes} from '@mui/material/styles';

import {LoadBar} from '@/app/components/ui/StateViews';
import {COLOR, DURATION, radius} from '@/lib/ui-tokens';

const dim = keyframes`from { opacity: 0; } to { opacity: 1; }`;

// While a table or section reloads after a columns, filter, grouping or page
// change: after 120 ms the old content dims to 60% and the 2px progress bar
// runs along the top of the window. Clicks pass through, so the controls stay
// usable during a refetch.
export default function UpdatingOverlay({
  label = 'Updating data…',
}: {
  label?: string;
  /** Kept for callers; the progress bar is always at the top of the window. */
  floating?: boolean;
}) {
  return (
    <Box
      role="status"
      aria-label={label}
      sx={{
        position: 'absolute',
        inset: 0,
        borderRadius: radius('lg'),
        bgcolor: `color-mix(in srgb, ${COLOR.bgCanvas} 40%, transparent)`,
        zIndex: 10,
        pointerEvents: 'none',
        animation: `${dim} ${DURATION.base}ms ${DURATION.fast}ms both`,
      }}>
      <LoadBar />
    </Box>
  );
}
