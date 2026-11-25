'use client';

import {use} from 'react';

import EntityView from '@/app/components/EntityView';
import {PageContainer} from '@/app/components/shell/PageHead';
import {LoadingState} from '@/app/components/ui/StateViews';
import {useProblemMetadata} from '@/lib/contexts/ProblemMetadataContext';

/** A partition's groups: the run's objects grouped by the partition. */
export default function PartitionPage({
  params,
}: {
  params: Promise<{runId: string; partitionName: string}>;
}) {
  const {partitionName} = use(params);
  const partition = decodeURIComponent(partitionName);
  const {metadata} = useProblemMetadata();
  if (metadata == null) {
    return (
      <PageContainer>
        <LoadingState label="Loading the problem…" />
      </PageContainer>
    );
  }
  return (
    <EntityView
      key={partition}
      entityName={metadata.objectName}
      partition={partition}
    />
  );
}
