'use client';

import {Box, Button, IconButton, Tooltip} from '@mui/material';
import {ExternalLink, Sparkles} from 'lucide-react';
import Link from 'next/link';

import {useAssistantContext} from '@/app/components/AssistantContext';
import BrandMark from '@/app/components/shell/BrandMark';
import CommandPalette from '@/app/components/shell/CommandPalette';
import RunPicker from '@/app/components/shell/RunPicker';
import ShareButton from '@/app/components/shell/ShareButton';
import ShortcutsDialog from '@/app/components/shell/ShortcutsDialog';
import ThemeToggle from '@/app/components/shell/ThemeToggle';
import {SidebarTrigger} from '@/app/components/sidebar/SidebarContext';
import UserInfo from '@/app/components/UserInfo';
import {ASSISTANT_ENABLED} from '@platform/assistant-ui';
import {COLOR, SHELL, TEXT, TRACKING, WEIGHT, Z} from '@/lib/ui-tokens';

function AssistantToggle() {
  const {isOpen, toggle} = useAssistantContext();
  if (!ASSISTANT_ENABLED) {
    return null;
  }
  return (
    <Tooltip
      title={isOpen ? 'Close Rebalancer Assistant' : 'Rebalancer Assistant'}>
      <IconButton
        aria-label="Rebalancer Assistant"
        aria-pressed={isOpen}
        onClick={toggle}
        sx={
          isOpen
            ? {bgcolor: COLOR.actionSubtle, color: COLOR.actionHover}
            : undefined
        }>
        <Sparkles size={18} />
      </IconButton>
    </Tooltip>
  );
}

/**
 * The top bar (52px): mark and product, the run picker, search (⌘K), then
 * share, the assistant, theme, help and the account.
 */
export default function TopBar({
  runId,
  serviceName,
  internalUrl,
}: {
  runId: string;
  serviceName: string;
  internalUrl: string | null;
}) {
  return (
    <Box
      component="header"
      data-shell="top-bar"
      sx={{
        position: 'sticky',
        top: 0,
        zIndex: Z.topbar,
        height: SHELL.topBar,
        flexShrink: 0,
        display: 'flex',
        alignItems: 'center',
        gap: 1.5,
        px: 2,
        bgcolor: COLOR.bgSurface,
        borderBottom: `1px solid ${COLOR.borderDefault}`,
      }}>
      <SidebarTrigger />
      <Box
        component={Link}
        href="/"
        aria-label="Rebalancer Explorer home"
        sx={{
          display: 'flex',
          alignItems: 'center',
          gap: 1.25,
          flexShrink: 0,
          color: COLOR.textPrimary,
          textDecoration: 'none',
          fontSize: TEXT.md,
          fontWeight: WEIGHT.heading,
          letterSpacing: TRACKING.xl,
          whiteSpace: 'nowrap',
          borderRadius: '6px',
        }}>
        <BrandMark size={26} />
        <Box
          component="span"
          sx={{'@media (max-width: 1180px)': {display: 'none'}}}>
          Rebalancer Explorer
        </Box>
      </Box>
      <RunPicker runId={runId} serviceName={serviceName} />
      <Box sx={{flex: 1, minWidth: 0}} />
      <CommandPalette />
      {internalUrl != null && (
        <Tooltip title="Switch to internal version (opens in a new tab)">
          <Button
            variant="outlined"
            component="a"
            href={internalUrl}
            target="_blank"
            rel="noopener noreferrer"
            aria-label="Switch to internal version"
            startIcon={<ExternalLink size={14} />}
            sx={{
              flexShrink: 0,
              '@media (max-width: 1180px)': {
                minWidth: 0,
                '& .label': {display: 'none'},
              },
            }}>
            <span className="label">Switch to internal version</span>
          </Button>
        </Tooltip>
      )}
      <Box sx={{display: 'flex', alignItems: 'center', gap: 0.5}}>
        <ShareButton />
        <AssistantToggle />
        <ThemeToggle />
        <ShortcutsDialog />
      </Box>
      <UserInfo />
    </Box>
  );
}
