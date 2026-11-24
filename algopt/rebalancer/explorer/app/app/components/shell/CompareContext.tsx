'use client';

import {
  createContext,
  useContext,
  useEffect,
  useMemo,
  useState,
  type ReactNode,
} from 'react';

/** What the context bar's Compare control shows for the current page. */
export interface PageComparison {
  baseline: string;
  current: string;
  /** Opens the page's comparison editor; without it the control is static. */
  onEdit?: () => void;
}

interface CompareContextValue {
  comparison: PageComparison | null;
  setComparison: (c: PageComparison | null) => void;
}

const CompareContext = createContext<CompareContextValue | null>(null);

export function CompareProvider({children}: {children: ReactNode}) {
  const [comparison, setComparison] = useState<PageComparison | null>(null);
  const value = useMemo(() => ({comparison, setComparison}), [comparison]);
  return (
    <CompareContext.Provider value={value}>{children}</CompareContext.Provider>
  );
}

/** The comparison the current page registered, if any. */
export function usePageComparison(): PageComparison | null {
  return useContext(CompareContext)?.comparison ?? null;
}

/**
 * A page that compares two assignments registers its sides (and its editor)
 * for the context bar while it is mounted.
 */
export function useRegisterComparison(
  baseline: string,
  current: string,
  onEdit?: () => void,
): void {
  const setComparison = useContext(CompareContext)?.setComparison;
  useEffect(() => {
    setComparison?.({baseline, current, onEdit});
  }, [setComparison, baseline, current, onEdit]);
  useEffect(() => () => setComparison?.(null), [setComparison]);
}
