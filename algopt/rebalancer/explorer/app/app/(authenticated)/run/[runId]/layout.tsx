'use client';

import {Box, Drawer, IconButton, Tooltip, useMediaQuery} from '@mui/material';
import {ExternalLink, History, X} from 'lucide-react';
import {use} from 'react';

import {
  AssistantProvider,
  useAssistantContext,
} from '@/app/components/AssistantContext';
import {
  ASSISTANT_ENABLED,
  RebalancerExplorerAssistant,
} from '@platform/assistant-ui';
import {Sidebar, SidebarInset, SidebarProvider} from '@/app/components/Sidebar';
import {
  internalExplorerUrl,
  metamateConversationUrl,
} from '@platform/internal-links';
import {DetailPaneProvider} from '@/app/components/detail-pane/DetailPaneContext';
import {CompareProvider} from '@/app/components/shell/CompareContext';
import ContextBar from '@/app/components/shell/ContextBar';
import TopBar from '@/app/components/shell/TopBar';
import {
  ProblemMetadataProvider,
  useProblemMetadata,
} from '@/lib/contexts/ProblemMetadataContext';
import {RebalancerHandleProvider} from '@/lib/contexts/RebalancerHandleContext';
import {WaitForSandbox} from '@/lib/contexts/WaitForSandbox';
import {decodeRunId} from '@/lib/run-id';
import {COLOR, SHELL, TEXT, WEIGHT} from '@/lib/ui-tokens';

const ASSISTANT_PANEL_WIDTH = 480;

function AssistantPanel() {
  const {isOpen, close, view, toggleHistory, conversationId} =
    useAssistantContext();
  const isMobile = useMediaQuery('(max-width: 760px)');

  if (!ASSISTANT_ENABLED || !isOpen) {
    return null;
  }

  const isHistoryActive = view === 'history';
  const historyLabel = isHistoryActive ? 'Back to chat' : 'Chat history';
  const metamateUrl =
    conversationId != null ? metamateConversationUrl(conversationId) : null;

  const body = (
    <>
      <Box
        sx={{
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          height: SHELL.topBar,
          flexShrink: 0,
          px: 2,
          borderBottom: `1px solid ${COLOR.borderDefault}`,
        }}>
        <Box
          component="span"
          sx={{fontSize: TEXT.md, fontWeight: WEIGHT.semibold}}>
          {isHistoryActive ? 'Chat history' : 'Rebalancer Assistant'}
        </Box>
        <Box sx={{display: 'flex', alignItems: 'center', gap: 0.5}}>
          {!isHistoryActive && metamateUrl != null && (
            <Tooltip title="Open in Metamate">
              <IconButton
                size="small"
                component="a"
                href={metamateUrl}
                target="_blank"
                rel="noopener noreferrer"
                aria-label="Open in Metamate">
                <ExternalLink className="size-4" />
              </IconButton>
            </Tooltip>
          )}
          <Tooltip title={historyLabel}>
            <IconButton
              size="small"
              onClick={toggleHistory}
              aria-label={historyLabel}
              aria-pressed={isHistoryActive}
              sx={
                isHistoryActive
                  ? {bgcolor: COLOR.actionSubtle, color: COLOR.actionHover}
                  : undefined
              }>
              <History className="size-4" />
            </IconButton>
          </Tooltip>
          <IconButton
            size="small"
            onClick={close}
            aria-label="Close Rebalancer Assistant">
            <X className="size-4" />
          </IconButton>
        </Box>
      </Box>
      <div className="flex min-h-0 flex-1 flex-col">
        <RebalancerExplorerAssistant />
      </div>
    </>
  );

  // Phones and tablets: a full-screen drawer instead of a 480px column that
  // would squeeze the page.
  if (isMobile) {
    return (
      <Drawer
        anchor="right"
        open
        onClose={close}
        PaperProps={{sx: {width: '100vw', display: 'flex'}}}>
        {body}
      </Drawer>
    );
  }

  return (
    <Box
      component="aside"
      sx={{
        position: 'sticky',
        top: SHELL.topBar,
        height: `calc(100vh - ${SHELL.topBar}px)`,
        width: ASSISTANT_PANEL_WIDTH,
        flexShrink: 0,
        alignSelf: 'flex-start',
        display: 'flex',
        flexDirection: 'column',
        borderLeft: `1px solid ${COLOR.borderDefault}`,
        bgcolor: COLOR.bgSurface,
      }}>
      {body}
    </Box>
  );
}

/** The shell: top bar over the side navigation, the run bar and the page. */
function RunShell({
  runId,
  internalUrl,
  children,
}: {
  runId: string;
  internalUrl: string | null;
  children: React.ReactNode;
}) {
  const {metadata} = useProblemMetadata();
  return (
    <Box
      sx={{
        display: 'flex',
        flexDirection: 'column',
        minHeight: '100vh',
        width: '100%',
      }}>
      <TopBar
        runId={runId}
        serviceName={metadata?.serviceName ?? ''}
        internalUrl={internalUrl}
      />
      <Box sx={{display: 'flex', flex: 1, minWidth: 0}}>
        <Sidebar />
        <SidebarInset>
          <ContextBar />
          {children}
        </SidebarInset>
        <AssistantPanel />
      </Box>
    </Box>
  );
}

export default function RunLayout({
  children,
  params,
}: {
  children: React.ReactNode;
  params: Promise<{runId: string}>;
}) {
  const {runId} = use(params);
  const decodedRunId = decodeRunId(runId);
  const internalUrl = internalExplorerUrl(decodedRunId);

  return (
    <RebalancerHandleProvider runId={decodedRunId}>
      <WaitForSandbox>
        <ProblemMetadataProvider>
          <AssistantProvider>
            <SidebarProvider>
              <DetailPaneProvider>
                <CompareProvider>
                  <RunShell runId={decodedRunId} internalUrl={internalUrl}>
                    {children}
                  </RunShell>
                </CompareProvider>
              </DetailPaneProvider>
            </SidebarProvider>
          </AssistantProvider>
        </ProblemMetadataProvider>
      </WaitForSandbox>
    </RebalancerHandleProvider>
  );
}
