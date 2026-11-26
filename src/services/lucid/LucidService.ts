/**
 * Where the Lucid theme's background comes from — the phone's wallpaper, a
 * photo or a short clip — and the look worked out from it (theme/lucid.ts).
 * A photo or clip is copied into Photo Plus's own storage, so it stays even
 * if the original is moved or deleted. (From Dr. File's Lucid theme.)
 *
 * The wallpaper: when Android lets the app read it, it is kept as a picture;
 * otherwise (newer Android keeps it private, and live wallpapers are moving)
 * the app's window turns see-through and shows the wallpaper itself, the
 * look worked out from the colours Android shares.
 */
import { create } from 'zustand';

import { nativeModules } from '../../native';
import { lucidLook, statsFromColors, type LookStats, type LucidLook } from '../../theme/lucid';
import type { SettingsRepository } from '../database/repositories/SettingsRepository';

export type LucidSource = 'wallpaper' | 'photo' | 'video';

export interface LucidState {
  source: LucidSource;
  /** The picture or clip shown (an app file path); null while the window shows the wallpaper itself. */
  media: string | null;
  /** The phone shows its wallpaper behind Photo Plus's see-through window. */
  seeThrough: boolean;
  look: LucidLook | null;
  busy: boolean;
}

/** Saved under settings 'internal.lucid'. */
export const LUCID_KEY = 'lucid';
export const MAX_VIDEO_MB = 30;

export const useLucid = create<LucidState>(() => ({
  source: 'wallpaper',
  media: null,
  seeThrough: false,
  look: null,
  busy: false,
}));

type Repo = Pick<SettingsRepository, 'getInternal' | 'setInternal'>;
let repo: Repo | null = null;

const native = () => nativeModules.lucid ?? null;

const save = () => {
  const { source, media, seeThrough, look } = useLucid.getState();
  void repo?.setInternal(LUCID_KEY, { source, media, seeThrough, look }).catch(() => undefined);
};

const asStats = (s: unknown): LookStats | null => {
  const o = s as Partial<LookStats> | null;
  if (!o || typeof o.p50 !== 'number' || typeof o.p10 !== 'number' || typeof o.p90 !== 'number') return null;
  return {
    mean: Number(o.mean ?? o.p50),
    p10: o.p10,
    p50: o.p50,
    p90: o.p90,
    colors: Array.isArray(o.colors) ? o.colors.filter((c): c is string => typeof c === 'string') : [],
    saturation: Number(o.saturation ?? 0.3),
  };
};

/** Loads the saved background and look (start-up). */
export async function hydrateLucid(settings: Repo): Promise<void> {
  repo = settings;
  const saved = await settings.getInternal<Partial<LucidState>>(LUCID_KEY).catch(() => null);
  if (saved?.look && typeof saved.look === 'object') {
    useLucid.setState({
      source: saved.source === 'photo' || saved.source === 'video' ? saved.source : 'wallpaper',
      media: typeof saved.media === 'string' ? saved.media : null,
      seeThrough: saved.seeThrough === true,
      look: saved.look,
    });
  }
}

/** Uses the phone's wallpaper (reading it again picks up a new one). */
export async function chooseWallpaper(): Promise<void> {
  const n = native();
  if (!n) return;
  useLucid.setState({ busy: true });
  try {
    const w = (await n.wallpaper()) as {
      path: string | null;
      stats: unknown;
      colors: string[];
      darkText: boolean;
    };
    const stats = asStats(w.stats);
    if (w.path && stats) {
      await n.showWallpaper(false);
      useLucid.setState({ source: 'wallpaper', media: w.path, seeThrough: false, look: lucidLook(stats, w.darkText) });
    } else {
      // A live wallpaper, or one Android keeps private: the window turns see-through.
      await n.showWallpaper(true);
      useLucid.setState({
        source: 'wallpaper',
        media: null,
        seeThrough: true,
        look: lucidLook(statsFromColors(w.colors ?? [], w.darkText === true), w.darkText === true),
      });
    }
    save();
  } finally {
    useLucid.setState({ busy: false });
  }
}

/**
 * Picks a photo or a short clip as the background (Premium). Resolves to
 * true when one was set, false when nothing was chosen; rejects with a
 * message the user can read (too big, unreadable, no space).
 */
export async function pickLucidBackground(kind: 'photo' | 'video'): Promise<boolean> {
  const n = native();
  if (!n) return false;
  useLucid.setState({ busy: true });
  try {
    const r = (await n.pickBackground(kind)) as { path: string; stats: unknown } | null;
    const stats = r ? asStats(r.stats) : null;
    if (!r || !stats) return false;
    await n.showWallpaper(false);
    useLucid.setState({ source: kind, media: r.path, seeThrough: false, look: lucidLook(stats) });
    save();
    return true;
  } finally {
    useLucid.setState({ busy: false });
  }
}

/** The see-through window on only while Lucid shows the wallpaper itself (set again on each return). */
export async function applyLucidWindow(active: boolean): Promise<void> {
  await native()
    ?.showWallpaper(active && useLucid.getState().seeThrough)
    .catch(() => false);
}

/**
 * Lucid is chosen: make sure it can show (start-up, each return to the
 * app), so it never falls back to plain light or dark. With nothing saved,
 * or a background file that has gone, the wallpaper is read again.
 */
export async function ensureLucid(fileExists: (path: string) => Promise<boolean>): Promise<void> {
  const s = useLucid.getState();
  if (s.busy) return;
  if (s.look && (s.media === null || (await fileExists(s.media).catch(() => false)))) return;
  await chooseWallpaper();
}
