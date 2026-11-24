'use client';

import React, {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState,
} from 'react';
import {IconButton, Tooltip, useMediaQuery} from '@mui/material';
import {PanelLeft} from 'lucide-react';

import {setPref, usePref} from '@/lib/prefs';
import {SHELL} from '@/lib/ui-tokens';

const SIDEBAR_PREF = 'rebalancer_sidebar_state';

// The side nav is 232px; a 64px icon rail when the reader collapses it or at
// 1180px and below; hidden (opened as an overlay) at 760px and below.
export const NARROW_QUERY = '(max-width: 1180px)';
export const MOBILE_QUERY = '(max-width: 760px)';

interface SidebarContextValue {
  isOpen: boolean;
  isCollapsed: boolean;
  isMobileOpen: boolean;
  toggle: () => void;
  toggleMobile: () => void;
  closeMobile: () => void;
}

const SidebarContext = createContext<SidebarContextValue | null>(null);

export function useSidebarContext() {
  const context = useContext(SidebarContext);
  if (!context) {
    throw new Error('useSidebarContext must be used within SidebarProvider');
  }
  return context;
}

interface SidebarProviderProps {
  children: React.ReactNode;
  defaultOpen?: boolean;
}

export function SidebarProvider({
  children,
  defaultOpen = true,
}: SidebarProviderProps) {
  // The reader's choice, stored per browser; the server render (and
  // hydration) use `defaultOpen`.
  const stored = usePref(SIDEBAR_PREF, null);
  const isOpen = stored == null ? defaultOpen : stored === 'true';
  const [isMobileOpen, setIsMobileOpen] = useState(false);

  const toggle = useCallback(() => {
    setPref(SIDEBAR_PREF, String(!isOpen));
  }, [isOpen]);

  const toggleMobile = useCallback(() => {
    setIsMobileOpen(prev => !prev);
  }, []);

  const closeMobile = useCallback(() => {
    setIsMobileOpen(false);
  }, []);

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'b' && (e.metaKey || e.ctrlKey)) {
        e.preventDefault();
        toggle();
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [toggle]);

  const isNarrow = useMediaQuery(NARROW_QUERY);
  const isCollapsed = !isOpen || isNarrow;
  const value = useMemo<SidebarContextValue>(
    () => ({
      isOpen,
      isCollapsed,
      isMobileOpen,
      toggle,
      toggleMobile,
      closeMobile,
    }),
    [isOpen, isCollapsed, isMobileOpen, toggle, toggleMobile, closeMobile],
  );

  return (
    <SidebarContext.Provider value={value}>
      <div
        className="flex min-h-screen w-full"
        style={
          {
            '--sidebar-width': `${isCollapsed ? SHELL.navRail : SHELL.nav}px`,
          } as React.CSSProperties
        }>
        {children}
      </div>
    </SidebarContext.Provider>
  );
}

export function SidebarTrigger({className}: {className?: string}) {
  const {toggle, toggleMobile} = useSidebarContext();
  const isMobile = useMediaQuery(MOBILE_QUERY);

  return (
    <Tooltip title={isMobile ? 'Open navigation' : 'Toggle sidebar (⌘B)'}>
      <IconButton
        onClick={isMobile ? toggleMobile : toggle}
        className={className}
        aria-label={isMobile ? 'Open navigation' : 'Toggle sidebar'}>
        <PanelLeft size={18} />
      </IconButton>
    </Tooltip>
  );
}
