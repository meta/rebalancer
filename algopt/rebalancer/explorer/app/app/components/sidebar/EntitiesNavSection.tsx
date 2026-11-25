'use client';

import React from 'react';
import {Circle, Archive, Boxes, Ruler, Shapes} from 'lucide-react';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';
import {NavSectionLabel, SidebarMenuItem} from './SidebarMenuItem';
import {CollapsibleNavSection, type NavLink} from './CollapsibleNavSection';

interface EntitiesNavSectionProps {
  basePath: string;
  isCollapsed: boolean;
  isMobileOpen: boolean;
  isItemActive: (segment: string) => boolean;
  closeMobile: () => void;
}

export function EntitiesNavSection({
  basePath,
  isCollapsed,
  isMobileOpen,
  isItemActive,
  closeMobile,
}: EntitiesNavSectionProps) {
  const {metadata} = useProblemMetadata();
  const collapsed = isCollapsed && !isMobileOpen;

  if (!metadata) {
    return null;
  }

  const scopeNames = metadata.scopeNames ?? [];
  const partitionNames = metadata.partitionNames ?? [];
  const dynamicDimensionNames = metadata.dynamicDimensionNames ?? [];

  const toLinks = (section: string, names: string[]): NavLink[] =>
    names.map(name => {
      const segment = `/${section}/${encodeURIComponent(name)}`;
      return {
        key: name,
        title: name,
        href: basePath + segment,
        isActive: isItemActive(segment),
      };
    });

  return (
    <div className="space-y-0.5">
      {!collapsed && <NavSectionLabel>Entities</NavSectionLabel>}

      <SidebarMenuItem
        href={`${basePath}/entity/${encodeURIComponent(metadata.objectName)}`}
        icon={Circle}
        title={metadata.objectName}
        isActive={isItemActive(
          `/entity/${encodeURIComponent(metadata.objectName)}`,
        )}
        isCollapsed={collapsed}
        onClick={closeMobile}
        tooltip="Object info"
      />
      <SidebarMenuItem
        href={`${basePath}/entity/${encodeURIComponent(metadata.containerName)}`}
        icon={Archive}
        title={metadata.containerName}
        isActive={isItemActive(
          `/entity/${encodeURIComponent(metadata.containerName)}`,
        )}
        isCollapsed={collapsed}
        onClick={closeMobile}
        tooltip="Container info"
      />

      {scopeNames.length > 0 && (
        <CollapsibleNavSection
          title="Scopes"
          icon={Boxes}
          items={toLinks('entity', scopeNames)}
          isCollapsed={collapsed}
          onNavigate={closeMobile}
        />
      )}

      {partitionNames.length > 0 && (
        <CollapsibleNavSection
          title="Partitions"
          icon={Shapes}
          items={toLinks('partition', partitionNames)}
          isCollapsed={collapsed}
          onNavigate={closeMobile}
        />
      )}

      {dynamicDimensionNames.length > 0 && (
        <CollapsibleNavSection
          title="Dynamic Dimensions"
          icon={Ruler}
          items={toLinks('dynamic-dimensions', dynamicDimensionNames)}
          isCollapsed={collapsed}
          onNavigate={closeMobile}
        />
      )}
    </div>
  );
}
