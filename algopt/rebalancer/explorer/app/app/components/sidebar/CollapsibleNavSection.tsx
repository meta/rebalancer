'use client';

import React, {useState} from 'react';
import Link from 'next/link';
import {ListSubheader, Menu, MenuItem, Tooltip} from '@mui/material';
import {ChevronDown, ChevronRight} from 'lucide-react';

import {NavCount, navItemClassName, SidebarMenuItem} from './SidebarMenuItem';

export interface NavLink {
  key: string;
  title: string;
  href: string;
  isActive: boolean;
}

interface CollapsibleNavSectionProps {
  title: string;
  icon: React.ComponentType<{className?: string}>;
  /** Icon of each child row; defaults to the group icon. */
  itemIcon?: React.ComponentType<{className?: string}>;
  items: NavLink[];
  isCollapsed: boolean;
  onNavigate?: () => void;
  /** When set, the group title is itself a link (the chevron toggles). */
  href?: string;
  /** Active state of `href` when no child matches. */
  isHrefActive?: boolean;
  tooltip?: string;
}

/**
 * A nav group: expandable list in the full sidebar, one icon with a flyout
 * menu in the collapsed rail. Opens on its own when it holds the active page.
 */
export function CollapsibleNavSection({
  title,
  icon: Icon,
  itemIcon,
  items,
  isCollapsed,
  onNavigate,
  href,
  isHrefActive = false,
  tooltip,
}: CollapsibleNavSectionProps) {
  const isActive = isHrefActive || items.some(item => item.isActive);
  const [isExpanded, setIsExpanded] = useState(isActive);
  const [prevActive, setPrevActive] = useState(isActive);
  if (isActive !== prevActive) {
    setPrevActive(isActive);
    if (isActive) {
      setIsExpanded(true);
    }
  }
  const [anchorEl, setAnchorEl] = useState<HTMLElement | null>(null);

  if (isCollapsed) {
    return (
      <>
        <Tooltip title={tooltip ?? title} placement="right">
          <button
            type="button"
            aria-haspopup="menu"
            aria-expanded={anchorEl != null}
            aria-label={title}
            onClick={e => setAnchorEl(e.currentTarget)}
            className={`${navItemClassName(isActive, true)} w-full`}>
            <Icon className="size-4 shrink-0" />
          </button>
        </Tooltip>
        <Menu
          anchorEl={anchorEl}
          open={anchorEl != null}
          onClose={() => setAnchorEl(null)}
          anchorOrigin={{vertical: 'top', horizontal: 'right'}}
          transformOrigin={{vertical: 'top', horizontal: 'left'}}
          slotProps={{paper: {sx: {minWidth: 220, maxHeight: 480}}}}>
          <ListSubheader>{title}</ListSubheader>
          {items.map(item => (
            <MenuItem
              key={item.key}
              component={Link}
              href={item.href}
              selected={item.isActive}
              onClick={() => {
                setAnchorEl(null);
                onNavigate?.();
              }}>
              {item.title}
            </MenuItem>
          ))}
        </Menu>
      </>
    );
  }

  const chevron = isExpanded ? (
    <ChevronDown className="size-3.5 shrink-0" />
  ) : (
    <ChevronRight className="size-3.5 shrink-0" />
  );
  const toggleLabel = `${isExpanded ? 'Collapse' : 'Expand'} ${title}`;

  const header =
    href != null ? (
      <div className="flex items-center gap-1">
        <div className="min-w-0 flex-1">
          <SidebarMenuItem
            href={href}
            icon={Icon}
            title={title}
            isActive={isActive && !isExpanded}
            isCollapsed={false}
            onClick={onNavigate}
            tooltip={tooltip}
            count={items.length}
          />
        </div>
        <button
          type="button"
          aria-expanded={isExpanded}
          aria-label={toggleLabel}
          onClick={() => setIsExpanded(prev => !prev)}
          className="flex size-8 shrink-0 items-center justify-center rounded-md text-(--color-text-decorative) hover:bg-(--color-bg-subtle) hover:text-(--color-text-secondary)">
          {chevron}
        </button>
      </div>
    ) : (
      <button
        type="button"
        aria-expanded={isExpanded}
        onClick={() => setIsExpanded(prev => !prev)}
        className={`${navItemClassName(isActive && !isExpanded, false)} w-full`}>
        <Icon className="size-4 shrink-0" />
        <span className="flex-1 truncate text-left">{title}</span>
        <NavCount count={items.length} isActive={isActive && !isExpanded} />
        <span className="text-(--color-text-decorative)">{chevron}</span>
      </button>
    );

  return (
    <div>
      {header}
      {isExpanded && (
        <div className="ml-4 mt-0.5 space-y-0.5 border-l border-border pl-2">
          {items.map(item => (
            <SidebarMenuItem
              key={item.key}
              href={item.href}
              icon={itemIcon ?? Icon}
              title={item.title}
              isActive={item.isActive}
              isCollapsed={false}
              onClick={onNavigate}
            />
          ))}
        </div>
      )}
    </div>
  );
}
