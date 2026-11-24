import {useSyncExternalStore} from 'react';

// Small per-browser preferences. Storage can be unavailable (private modes,
// sandboxed frames), so every read and write falls back to memory.

const memory = new Map<string, string>();

export function readPref(key: string): string | null {
  try {
    return window.localStorage.getItem(key);
  } catch {
    return memory.get(key) ?? null;
  }
}

export function writePref(key: string, value: string): void {
  memory.set(key, value);
  try {
    window.localStorage.setItem(key, value);
  } catch {
    // Memory only.
  }
}

// Subscribers of `usePref`, told when a preference changes in this tab.
const listeners = new Set<() => void>();

function subscribe(listener: () => void): () => void {
  listeners.add(listener);
  window.addEventListener('storage', listener);
  return () => {
    listeners.delete(listener);
    window.removeEventListener('storage', listener);
  };
}

/** Writes a preference and re-renders every `usePref` reader. */
export function setPref(key: string, value: string): void {
  writePref(key, value);
  for (const l of [...listeners]) {
    l();
  }
}

/**
 * A preference as React state. The server render and hydration see
 * `serverValue`; the stored value applies right after.
 */
export function usePref(
  key: string,
  serverValue: string | null,
): string | null {
  return useSyncExternalStore(
    subscribe,
    () => readPref(key),
    () => serverValue,
  );
}
