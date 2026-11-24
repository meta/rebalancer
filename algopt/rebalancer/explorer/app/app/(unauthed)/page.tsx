'use client';

import {useState} from 'react';
import {useRouter} from 'next/navigation';
import {Box, Button, Link, Paper, TextField, Typography} from '@mui/material';
import {ArrowRight, ExternalLink} from 'lucide-react';

import BrandMark from '@/app/components/shell/BrandMark';
import {encodeRunId} from '@/lib/run-id';
import {COLOR, radius, SHADOW_MD, TEXT, WEIGHT} from '@/lib/ui-tokens';
import {rebalancerWikiUrl} from '@platform/internal-links';

export default function Home() {
  const [runId, setRunId] = useState('');
  const router = useRouter();
  const wikiUrl = rebalancerWikiUrl();

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (runId.trim()) {
      router.push(`/run/${encodeRunId(runId.trim())}/constraints-objectives`);
    }
  };

  return (
    <Box
      sx={{
        minHeight: '100vh',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        p: 2,
      }}>
      <Paper
        variant="outlined"
        sx={{
          width: '100%',
          maxWidth: 480,
          p: 3,
          borderRadius: radius('xl'),
          boxShadow: SHADOW_MD,
        }}>
        <Box sx={{display: 'flex', alignItems: 'center', gap: 1.25, mb: 1.5}}>
          <BrandMark size={32} />
          <Typography variant="h1">Rebalancer Explorer</Typography>
        </Box>
        <Typography variant="body2" sx={{color: COLOR.textSecondary, mb: 3}}>
          {wikiUrl != null ? (
            <Link
              href={wikiUrl}
              target="_blank"
              rel="noopener noreferrer"
              sx={{display: 'inline-flex', alignItems: 'center', gap: 0.5}}>
              Rebalancer
              <ExternalLink size={12} aria-hidden />
            </Link>
          ) : (
            'Rebalancer'
          )}{' '}
          is a generic assignment solver that optimizes how objects are assigned
          to containers, subject to constraints and objectives. It powers
          service placement and other optimization use cases across Meta.
          Rebalancer Explorer lets you inspect and analyze the results of
          individual runs.
        </Typography>
        <Box
          component="form"
          onSubmit={handleSubmit}
          sx={{display: 'flex', flexDirection: 'column', gap: 2}}>
          {/* The label sits above the field, help text under it. */}
          <Box sx={{display: 'flex', flexDirection: 'column', gap: 0.75}}>
            <Box
              component="label"
              htmlFor="run-id"
              sx={{
                fontSize: TEXT.xs,
                fontWeight: WEIGHT.semibold,
                color: COLOR.textSecondary,
              }}>
              Run ID
            </Box>
            <TextField
              id="run-id"
              placeholder="e.g. expensive_dryrun_1772634401_..."
              helperText="Paste a Manifold run id"
              value={runId}
              onChange={e => setRunId(e.target.value)}
              fullWidth
              size="small"
              autoFocus
              sx={{'& .MuiFormHelperText-root': {mx: 0}}}
            />
          </Box>
          <Button
            type="submit"
            disabled={!runId.trim()}
            variant="contained"
            size="large"
            endIcon={<ArrowRight size={16} />}>
            Explore run
          </Button>
        </Box>
      </Paper>
    </Box>
  );
}
