'use client';

import {
  createContext,
  useCallback,
  useContext,
  useMemo,
  useState,
  type ReactNode,
} from 'react';

import {usePathname} from 'next/navigation';

import type {Assignment, Handle} from '@/lib/rebalancer-explorer-types';

import DetailPane from './DetailPane';

export type ExpressionPaneTab = 'spec' | 'tree';

/** A C&O row as shown in the pane, with what the tree needs to load. */
export interface ExpressionPanePayload {
  expressionId: number;
  name: string;
  description: string;
  expressionType: 'CONSTRAINT' | 'OBJECTIVE';
  tupleIndex: number | null;
  srcValue: number;
  dstValue: number;
  delta: number;
  percentage: number;
  srcPenalty: number | null;
  dstPenalty: number | null;
  handle: Handle;
  sourceAssignment: Assignment;
  destinationAssignment: Assignment;
  /** Short names of A and B, e.g. "Initial" and "Step 12 +2 moves". */
  srcLabel?: string;
  dstLabel?: string;
  /** The run's largest tuple index, for the tuple index color. */
  maxTupleIndex?: number;
}

/** One entity row (object, container or scope item) by its name. */
export interface EntityPanePayload {
  entity: string;
  id: string;
  handle: Handle;
  runId: string;
}

export type DetailPaneContent =
  | {kind: 'expression'; payload: ExpressionPanePayload; tab: ExpressionPaneTab}
  | {kind: 'entity'; payload: EntityPanePayload};

interface DetailPaneContextValue {
  content: DetailPaneContent | null;
  /** The panels under this one, most recent last ("Back to …"). */
  stack: readonly DetailPaneContent[];
  /**
   * Shows `content`. With `push`, the panel shown now stays one step back,
   * for links inside a panel; otherwise the panel replaces the whole stack.
   */
  open: (content: DetailPaneContent, options?: {push?: boolean}) => void;
  back: () => void;
  setTab: (tab: ExpressionPaneTab) => void;
  close: () => void;
}

type DetailPaneActions = Omit<DetailPaneContextValue, 'content' | 'stack'>;

/** The title a panel shows, for "Back to …". */
export function paneTitle(content: DetailPaneContent): string {
  return content.kind === 'expression'
    ? content.payload.name
    : content.payload.id;
}

// Content and actions are separate contexts, so rows that only open the pane
// do not re-render when it opens, switches or closes.
const DetailPaneActionsContext = createContext<DetailPaneActions | null>(null);
const DetailPaneContentContext = createContext<{
  content: DetailPaneContent | null;
  stack: readonly DetailPaneContent[];
}>({content: null, stack: []});

/** True inside the pane, so links there open the next panel on top. */
export const InPaneContext = createContext(false);

/** One right-side detail pane per run; pages open it with `useDetailPane`. */
export function DetailPaneProvider({children}: {children: ReactNode}) {
  const [state, setState] = useState<{
    content: DetailPaneContent | null;
    stack: readonly DetailPaneContent[];
  }>({content: null, stack: []});

  // A pane opened on one page does not follow the user to another.
  const pathname = usePathname();
  const [prevPathname, setPrevPathname] = useState(pathname);
  if (pathname !== prevPathname) {
    setPrevPathname(pathname);
    setState({content: null, stack: []});
  }

  const open = useCallback(
    (next: DetailPaneContent, options?: {push?: boolean}) =>
      setState(prev => ({
        content: next,
        stack:
          options?.push === true && prev.content != null
            ? [...prev.stack, prev.content]
            : [],
      })),
    [],
  );
  const back = useCallback(
    () =>
      setState(prev =>
        prev.stack.length === 0
          ? prev
          : {
              content: prev.stack[prev.stack.length - 1],
              stack: prev.stack.slice(0, -1),
            },
      ),
    [],
  );
  const close = useCallback(() => setState({content: null, stack: []}), []);
  const setTab = useCallback(
    (tab: ExpressionPaneTab) =>
      setState(prev =>
        prev.content?.kind === 'expression'
          ? {...prev, content: {...prev.content, tab}}
          : prev,
      ),
    [],
  );

  const actions = useMemo(
    () => ({open, back, setTab, close}),
    [open, back, setTab, close],
  );

  return (
    <DetailPaneActionsContext.Provider value={actions}>
      <DetailPaneContentContext.Provider value={state}>
        {children}
        <DetailPane />
      </DetailPaneContentContext.Provider>
    </DetailPaneActionsContext.Provider>
  );
}

/** Opens, switches and closes the pane; does not re-render with it. */
export function useDetailPaneActions(): DetailPaneActions {
  const actions = useContext(DetailPaneActionsContext);
  if (actions == null) {
    throw new Error(
      'useDetailPaneActions must be used within a DetailPaneProvider',
    );
  }
  return actions;
}

/** The pane's content and actions; re-renders whenever the pane changes. */
export function useDetailPane(): DetailPaneContextValue {
  const actions = useDetailPaneActions();
  const {content, stack} = useContext(DetailPaneContentContext);
  return useMemo(
    () => ({...actions, content, stack}),
    [actions, content, stack],
  );
}
