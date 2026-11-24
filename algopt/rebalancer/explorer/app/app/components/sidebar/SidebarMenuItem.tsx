'use client';

import React from 'react';
import Link from 'next/link';
import {Box, Tooltip, Typography} from '@mui/material';
import {ExternalLink} from 'lucide-react';

import {COLOR, radius, TABULAR, TEXT} from '@/lib/ui-tokens';

interface BaseProps {
  icon: React.ComponentType<{className?: string}>;
  title: string;
  isActive: boolean;
  isCollapsed: boolean;
  tooltip?: string;
  count?: number;
}

interface LinkProps extends BaseProps {
  href: string;
  onClick?: () => void;
  target?: string;
  onActivate?: never;
}

interface ButtonProps extends BaseProps {
  href?: never;
  onClick?: never;
  target?: never;
  onActivate: () => void;
}

type MenuItemProps = LinkProps | ButtonProps;

/**
 * Classes shared by nav rows (links, buttons and group headers): 32px, 13px
 * medium secondary text; hover subtle; active is the action tint with
 * action-hover text.
 */
export function navItemClassName(isActive: boolean, isCollapsed: boolean) {
  return [
    'relative flex h-8 items-center gap-2.5 rounded-md px-2.5 text-sm font-medium no-underline transition-colors duration-[120ms]',
    isActive
      ? 'bg-(--color-action-subtle) text-(--color-action-hover)'
      : 'text-(--color-text-secondary) hover:bg-(--color-bg-subtle) hover:text-(--color-text-primary)',
    'focus-visible:outline-2 focus-visible:outline-offset-[-2px] focus-visible:outline-(--color-action)',
    isCollapsed ? 'justify-center px-0' : '',
  ].join(' ');
}

/** Uppercase eyebrow above a group of nav items. */
export function NavSectionLabel({children}: {children: React.ReactNode}) {
  return (
    <Typography
      variant="overline"
      component="div"
      sx={{display: 'block', px: 1.25, pt: 1.5, pb: 0.75, lineHeight: 1.2}}>
      {children}
    </Typography>
  );
}

/** A nav row's count: a number that means work, not a badge. */
export function NavCount({
  count,
  isActive,
}: {
  count: number;
  isActive: boolean;
}) {
  return (
    <Box
      component="span"
      sx={{
        ml: 'auto',
        flexShrink: 0,
        px: 0.75,
        borderRadius: radius('full'),
        fontSize: TEXT['2xs'],
        lineHeight: '18px',
        bgcolor: isActive ? COLOR.actionSubtleBorder : COLOR.bgSubtle,
        color: isActive ? COLOR.actionHover : COLOR.textTertiary,
        ...TABULAR,
      }}>
      {count}
    </Box>
  );
}

export function SidebarMenuItem(props: MenuItemProps) {
  const {icon: Icon, title, isActive, isCollapsed, tooltip, count} = props;
  const isExternal = props.target === '_blank';
  const className = navItemClassName(isActive, isCollapsed);

  const inner = (
    <>
      <Icon className="size-4 shrink-0" />
      {!isCollapsed && (
        <>
          <span className="min-w-0 flex-1 truncate">{title}</span>
          {count != null && <NavCount count={count} isActive={isActive} />}
          {isExternal && (
            <ExternalLink
              aria-hidden
              className="size-3 shrink-0 text-(--color-text-decorative)"
            />
          )}
        </>
      )}
    </>
  );

  const content =
    props.href != null ? (
      <Link
        href={props.href}
        onClick={props.onClick}
        target={props.target}
        rel={isExternal ? 'noopener noreferrer' : undefined}
        aria-current={isActive ? 'page' : undefined}
        className={className}>
        {inner}
      </Link>
    ) : (
      <button
        type="button"
        onClick={props.onActivate}
        aria-pressed={isActive}
        className={`${className} w-full text-left`}>
        {inner}
      </button>
    );

  if (isCollapsed || tooltip) {
    return (
      <Tooltip title={tooltip ?? title} placement="right">
        {content}
      </Tooltip>
    );
  }

  return content;
}
