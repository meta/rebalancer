'use client';

import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useState,
  useSyncExternalStore,
} from 'react';

/**
 * Shared ResizeObserver for clipped (ellipsis or line-clamped) text in a
 * table. Size
 * changes are measured once per animation frame, and only cells whose
 * clipped state changed re-render (each cell subscribes to its own entry).
 */
export class ClipStore {
  private observer: ResizeObserver | null = null;
  private ids = new Map<Element, string>();
  private clipped = new Map<string, boolean>();
  private listeners = new Map<string, () => void>();
  private pending = new Set<Element>();
  private frame: number | null = null;

  observe(el: HTMLElement, id: string): () => void {
    this.observer ??= new ResizeObserver(entries => {
      for (const entry of entries) {
        this.schedule(entry.target);
      }
    });
    this.ids.set(el, id);
    this.observer.observe(el);
    this.schedule(el);
    return () => {
      this.observer?.unobserve(el);
      this.ids.delete(el);
      this.pending.delete(el);
    };
  }

  // The last published value is kept while an element re-registers (new
  // text in the same cell), so a clipped-to-fitting change still notifies.
  subscribe(id: string, listener: () => void): () => void {
    this.listeners.set(id, listener);
    return () => {
      this.listeners.delete(id);
      this.clipped.delete(id);
    };
  }

  isClipped(id: string): boolean {
    return this.clipped.get(id) ?? false;
  }

  dispose(): void {
    this.observer?.disconnect();
    this.observer = null;
    if (this.frame != null) {
      cancelAnimationFrame(this.frame);
      this.frame = null;
    }
  }

  private schedule(el: Element): void {
    this.pending.add(el);
    this.frame ??= requestAnimationFrame(() => this.flush());
  }

  private flush(): void {
    this.frame = null;
    const changed: string[] = [];
    for (const el of this.pending) {
      const id = this.ids.get(el);
      if (id == null || !(el instanceof HTMLElement)) {
        continue;
      }
      const isClipped =
        el.scrollWidth > el.clientWidth || el.scrollHeight > el.clientHeight;
      if (isClipped !== (this.clipped.get(id) ?? false)) {
        this.clipped.set(id, isClipped);
        changed.push(id);
      }
    }
    this.pending.clear();
    for (const id of changed) {
      this.listeners.get(id)?.();
    }
  }
}

export const ClipStoreContext = createContext<ClipStore | null>(null);

/** One store per table; disconnects its observer on unmount. */
export function useClipStore(): ClipStore {
  const [store] = useState(() => new ClipStore());
  useEffect(() => () => store.dispose(), [store]);
  return store;
}

export function useClipStoreContext(): ClipStore | null {
  return useContext(ClipStoreContext);
}

/** Whether the element registered under `id` is clipped. */
export function useIsClipped(store: ClipStore | null, id: string): boolean {
  const subscribe = useCallback(
    (listener: () => void) =>
      store == null ? () => {} : store.subscribe(id, listener),
    [store, id],
  );
  const getSnapshot = useCallback(
    () => store?.isClipped(id) ?? false,
    [store, id],
  );
  return useSyncExternalStore(subscribe, getSnapshot, () => false);
}
