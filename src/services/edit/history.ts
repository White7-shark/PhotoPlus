import type { EditDocument } from './document';

/**
 * Undo / redo over whole documents (edits are non-destructive, so a step is
 * just the previous document). Continuous changes (dragging a slider or a
 * prop) share a `group` and become a single step.
 */
export interface History {
  past: EditDocument[];
  present: EditDocument;
  future: EditDocument[];
  /** The group of the last change, while it can still absorb more. */
  group: string | null;
}

export const MAX_STEPS = 60;

export type HistoryAction =
  | { type: 'set'; doc: EditDocument; group?: string }
  | { type: 'commit' }
  | { type: 'undo' }
  | { type: 'redo' }
  | { type: 'reset'; doc: EditDocument };

export function startHistory(doc: EditDocument): History {
  return { past: [], present: doc, future: [], group: null };
}

export function historyReducer(h: History, a: HistoryAction): History {
  switch (a.type) {
    case 'set': {
      if (a.doc === h.present) return h;
      // Same continuous gesture: replace the step instead of adding one.
      if (a.group && a.group === h.group) return { ...h, present: a.doc, future: [] };
      return {
        past: [...h.past, h.present].slice(-MAX_STEPS),
        present: a.doc,
        future: [],
        group: a.group ?? null,
      };
    }
    case 'commit':
      return h.group === null ? h : { ...h, group: null };
    case 'undo': {
      const prev = h.past[h.past.length - 1];
      if (!prev) return h;
      return { past: h.past.slice(0, -1), present: prev, future: [h.present, ...h.future], group: null };
    }
    case 'redo': {
      const next = h.future[0];
      if (!next) return h;
      return { past: [...h.past, h.present], present: next, future: h.future.slice(1), group: null };
    }
    case 'reset':
      return startHistory(a.doc);
  }
}

export const canUndo = (h: History) => h.past.length > 0;
export const canRedo = (h: History) => h.future.length > 0;
