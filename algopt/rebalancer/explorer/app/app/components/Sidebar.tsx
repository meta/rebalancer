'use client';

import React from 'react';
import {Box} from '@mui/material';
import {useParams, usePathname, useSearchParams} from 'next/navigation';
import {
  FileText,
  TrendingDown,
  Gauge,
  Activity,
  ExternalLink,
  BookOpen,
  Users,
  Sparkles,
} from 'lucide-react';
import {
  SidebarProvider,
  SidebarTrigger,
  useSidebarContext,
} from './sidebar/SidebarContext';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {slugifyMetricName} from '@/lib/metric-slug';
import {decodeRunId, encodeRunId} from '@/lib/run-id';
import {pickQueryKeys} from '@/lib/url-state';
import {NavSectionLabel, SidebarMenuItem} from './sidebar/SidebarMenuItem';
import {
  CollapsibleNavSection,
  type NavLink,
} from './sidebar/CollapsibleNavSection';
import {EntitiesNavSection} from './sidebar/EntitiesNavSection';
import {useAssistantContext} from './AssistantContext';
import {ASSISTANT_ENABLED} from '@platform/assistant-ui';
import {
  rebalancerDocsUrl,
  rebalancerWorkplaceUrl,
  thriftExplorerUrl,
} from '@platform/internal-links';
import {COLOR, OVERLAY, SHELL, Z} from '@/lib/ui-tokens';

export {SidebarProvider, SidebarTrigger};

interface NavItem {
  title: string;
  segment: string;
  icon: React.ComponentType<{className?: string}>;
}

const METRICS_TOOLTIP =
  'Evaluate metrics (e.g., container utilizations) w.r.t. different assignments';

const COMPARE_KEYS = [
  'srcBase',
  'srcStep',
  'srcOverrides',
  'dstBase',
  'dstStep',
  'dstOverrides',
] as const;

const navigationItems: NavItem[] = [
  {title: 'Summary', segment: '', icon: FileText},
];

export function Sidebar() {
  const pathname = usePathname();
  const params = useParams<{runId: string}>();
  // params.runId is a percent-encoded route segment; recover the real id.
  const runId = decodeRunId(params.runId);
  const {isOpen, isCollapsed, isMobileOpen, closeMobile} = useSidebarContext();
  const {metadata} = useProblemMetadata();
  const {isOpen: isAssistantOpen, toggle: toggleAssistant} =
    useAssistantContext();

  const basePath = `/run/${encodeRunId(runId)}`;
  const thriftUrl = thriftExplorerUrl(encodeRunId(runId));
  const docsUrl = rebalancerDocsUrl();
  const workplaceUrl = rebalancerWorkplaceUrl();

  const isItemActive = (segment: string) => {
    const fullPath = basePath + segment;
    if (segment === '') {
      return pathname === basePath || pathname === basePath + '/';
    }
    return pathname.startsWith(fullPath);
  };

  // C&O and Metrics read the same A/B keys; moving between them keeps the
  // comparison. Other keys (tables, filters) stay with their page.
  const searchParams = useSearchParams();
  const onComparePage =
    isItemActive('/constraints-objectives') || isItemActive('/metrics/');
  const compareQuery = onComparePage
    ? pickQueryKeys(searchParams, COMPARE_KEYS)
    : '';
  const withCompare = (href: string) =>
    compareQuery === '' ? href : `${href}?${compareQuery}`;

  const metricNames = metadata?.metricCollectionNames ?? [];
  const metricLinks: NavLink[] = metricNames.map(name => {
    const segment = `/metrics/${slugifyMetricName(name)}`;
    return {
      key: name,
      title: name,
      href: withCompare(basePath + segment),
      isActive: isItemActive(segment),
    };
  });

  const sidebarContent = (
    <>
      {/* Navigation */}
      <nav
        aria-label="Run pages"
        className="flex flex-1 flex-col gap-0.5 overflow-y-auto px-2.5 py-1.5">
        <div className="space-y-0.5">
          {(!isCollapsed || isMobileOpen) && (
            <NavSectionLabel>This run</NavSectionLabel>
          )}
          {navigationItems.map(item => (
            <SidebarMenuItem
              key={item.segment}
              href={basePath + item.segment}
              icon={item.icon}
              title={item.title}
              isActive={isItemActive(item.segment)}
              isCollapsed={isCollapsed && !isMobileOpen}
              onClick={closeMobile}
            />
          ))}
          {thriftUrl != null && (
            <SidebarMenuItem
              href={thriftUrl}
              icon={ExternalLink}
              title="Thrift Explorer"
              isActive={false}
              isCollapsed={isCollapsed && !isMobileOpen}
              onClick={closeMobile}
              tooltip="Thrift Explorer is a UI tool designed to help you debug the input and output thrift structures of your rebalancer instance"
              target="_blank"
            />
          )}
        </div>

        <EntitiesNavSection
          basePath={basePath}
          isCollapsed={isCollapsed}
          isMobileOpen={isMobileOpen}
          isItemActive={isItemActive}
          closeMobile={closeMobile}
        />

        {/* Evaluation */}
        <div className="space-y-0.5">
          {(!isCollapsed || isMobileOpen) && (
            <NavSectionLabel>Evaluation</NavSectionLabel>
          )}
          <SidebarMenuItem
            href={withCompare(`${basePath}/constraints-objectives`)}
            icon={TrendingDown}
            title="Constraints & Objectives"
            isActive={isItemActive('/constraints-objectives')}
            isCollapsed={isCollapsed && !isMobileOpen}
            onClick={closeMobile}
            tooltip={
              compareQuery === ''
                ? 'Evaluate constraints and objectives w.r.t. different assignments'
                : 'Evaluate constraints and objectives (keeps the A/B selection)'
            }
          />
          {metricNames.length === 1 && (
            <SidebarMenuItem
              href={metricLinks[0].href}
              icon={Gauge}
              title="Metrics"
              isActive={isItemActive('/metrics/')}
              isCollapsed={isCollapsed && !isMobileOpen}
              onClick={closeMobile}
              tooltip={METRICS_TOOLTIP}
            />
          )}
          {metricNames.length > 1 && (
            <CollapsibleNavSection
              title="Metrics"
              icon={Gauge}
              items={metricLinks}
              href={metricLinks[0].href}
              isHrefActive={isItemActive('/metrics/')}
              isCollapsed={isCollapsed && !isMobileOpen}
              onNavigate={closeMobile}
              tooltip={METRICS_TOOLTIP}
            />
          )}
        </div>

        {/* Profiling */}
        <div className="space-y-0.5">
          {(!isCollapsed || isMobileOpen) && (
            <NavSectionLabel>Profiling</NavSectionLabel>
          )}
          <SidebarMenuItem
            href={`${basePath}/local-search-profiling`}
            icon={Activity}
            title="Local Search"
            isActive={isItemActive('/local-search-profiling')}
            isCollapsed={isCollapsed && !isMobileOpen}
            onClick={closeMobile}
            tooltip="Per-objective local search solver telemetry"
          />
        </div>

        {/* Help */}
        <div className="space-y-0.5">
          {(!isCollapsed || isMobileOpen) && (
            <NavSectionLabel>Help</NavSectionLabel>
          )}
          {ASSISTANT_ENABLED && (
            <SidebarMenuItem
              icon={Sparkles}
              title="Assistant"
              isActive={isAssistantOpen}
              isCollapsed={isCollapsed && !isMobileOpen}
              onActivate={() => {
                toggleAssistant();
                closeMobile();
              }}
              tooltip="AI assistant to help you understand this rebalancer run"
            />
          )}
          {docsUrl != null && (
            <SidebarMenuItem
              href={docsUrl}
              icon={BookOpen}
              title="Documentation"
              isActive={false}
              isCollapsed={isCollapsed && !isMobileOpen}
              onClick={closeMobile}
              target="_blank"
            />
          )}
          {workplaceUrl != null && (
            <SidebarMenuItem
              href={workplaceUrl}
              icon={Users}
              title="Workplace Group"
              isActive={false}
              isCollapsed={isCollapsed && !isMobileOpen}
              onClick={closeMobile}
              target="_blank"
            />
          )}
        </div>
      </nav>
    </>
  );

  return (
    <>
      {/* Side nav: sticky under the top bar; hidden at 760px and below. */}
      <Box
        component="aside"
        data-shell="side-nav"
        data-state={isOpen ? 'expanded' : 'collapsed'}
        sx={{
          position: 'sticky',
          top: SHELL.topBar,
          alignSelf: 'flex-start',
          height: `calc(100vh - ${SHELL.topBar}px)`,
          width: 'var(--sidebar-width)',
          flexShrink: 0,
          display: 'flex',
          flexDirection: 'column',
          borderRight: `1px solid ${COLOR.borderDefault}`,
          bgcolor: COLOR.bgSurface,
          transition: 'width 200ms ease',
          '@media (max-width: 760px)': {display: 'none'},
        }}>
        {sidebarContent}
      </Box>

      {/* At 760px and below the nav opens over the page. */}
      {isMobileOpen && (
        <Box
          onClick={closeMobile}
          aria-hidden="true"
          sx={{
            position: 'fixed',
            inset: 0,
            zIndex: Z.scrim,
            bgcolor: OVERLAY.scrim,
            '@media (min-width: 761px)': {display: 'none'},
          }}
        />
      )}
      <Box
        component="aside"
        sx={{
          position: 'fixed',
          top: 0,
          bottom: 0,
          left: 0,
          zIndex: Z.drawer,
          width: SHELL.nav,
          display: 'flex',
          flexDirection: 'column',
          pt: 1.5,
          borderRight: `1px solid ${COLOR.borderDefault}`,
          bgcolor: COLOR.bgSurface,
          transform: isMobileOpen ? 'none' : 'translateX(-100%)',
          transition: 'transform 200ms ease',
          '@media (min-width: 761px)': {display: 'none'},
        }}>
        {sidebarContent}
      </Box>
    </>
  );
}

export function SidebarInset({children}: {children: React.ReactNode}) {
  return (
    <Box
      component="main"
      sx={{display: 'flex', flexDirection: 'column', flex: 1, minWidth: 0}}>
      {children}
    </Box>
  );
}
