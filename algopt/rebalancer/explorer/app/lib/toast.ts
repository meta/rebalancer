// Toasts confirm (copy, share): one short message on the inverse surface for
// 1.6 s. Any component can raise one; <Toaster /> shows it.

export const TOAST_EVENT = 'rbx:toast';
export const TOAST_MS = 1600;

export function showToast(message: string): void {
  if (typeof window === 'undefined') {
    return;
  }
  window.dispatchEvent(new CustomEvent<string>(TOAST_EVENT, {detail: message}));
}
