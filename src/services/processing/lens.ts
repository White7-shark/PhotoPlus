/**
 * Lens awareness: tells people, in plain words, when their camera's lens
 * (not their photography) is what holds a photo back, and points them to
 * Transform.
 *
 * One photo can be soft for reasons that are nobody's lens (a missed focus,
 * a moving subject), so a single low reading only speaks about "this photo".
 * Readings are remembered per camera lens; once most recent photos agree,
 * the notice speaks about the lens itself.
 */

export interface LensReport {
  detailBlur: number;
  cornerBlur: number;
  fringePx: number;
  noise: number;
  /** 0 basic … 1 flagship-like. */
  grade: number;
  reliable: boolean;
  width: number;
  height: number;
}

/** Recent readings for one camera lens (newest last), one per photo. */
export interface LensProfile {
  readings: { id: string; grade: number }[];
}
export type LensProfiles = Record<string, LensProfile>;

export const LENS_PROFILES_KEY = 'lens.profiles';
/** Below this grade a photo shows what a basic lens leaves behind. */
export const BASIC_GRADE = 0.55;
const KEEP = 12;
const MIN_FOR_LENS = 3;

const isNum = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v);

export function isLensReport(v: unknown): v is LensReport {
  if (!v || typeof v !== 'object') return false;
  const r = v as Record<string, unknown>;
  return (
    [r.detailBlur, r.cornerBlur, r.fringePx, r.noise, r.grade, r.width, r.height].every(isNum) &&
    typeof r.reliable === 'boolean'
  );
}

export function isLensProfiles(v: unknown): v is LensProfiles {
  if (!v || typeof v !== 'object') return false;
  return Object.values(v as Record<string, unknown>).every(
    p =>
      !!p &&
      typeof p === 'object' &&
      Array.isArray((p as LensProfile).readings) &&
      (p as LensProfile).readings.length <= KEEP &&
      (p as LensProfile).readings.every(
        r => !!r && typeof r.id === 'string' && isNum(r.grade) && r.grade >= 0 && r.grade <= 1,
      ),
  );
}

/**
 * Profiles after a reading of photo `id` from `lens`. A photo counts once
 * (re-opening it replaces its reading); unreliable readings are ignored.
 */
export function rememberReading(profiles: LensProfiles, lens: string, id: string, report: LensReport): LensProfiles {
  if (!report.reliable) return profiles;
  const others = (profiles[lens]?.readings ?? []).filter(r => r.id !== id);
  const readings = [...others, { id, grade: Math.min(1, Math.max(0, report.grade)) }].slice(-KEEP);
  return { ...profiles, [lens]: { readings } };
}

/** True when most recent photos from this lens show a basic lens. */
export function lensIsBasic(profile: LensProfile | undefined): boolean {
  if (!profile || profile.readings.length < MIN_FOR_LENS) return false;
  const low = profile.readings.filter(r => r.grade < BASIC_GRADE).length;
  return low * 3 >= profile.readings.length * 2;
}

export interface LensNotice {
  title: string;
  body: string;
  /** What the photo shows, in plain words (for the details line). */
  signs: string[];
  scope: 'lens' | 'photo';
}

function signsOf(r: LensReport): string[] {
  const out: string[] = [];
  if (r.detailBlur >= 1.1) out.push('soft fine detail');
  if (Math.sqrt(Math.max(r.cornerBlur * r.cornerBlur - r.detailBlur * r.detailBlur, 0)) >= 0.8) {
    out.push('blurry edges');
  }
  if (r.fringePx >= 1) out.push('colour fringes on edges');
  if (r.noise >= 0.014) out.push('grain');
  if (r.width * r.height < 4e6) out.push('low resolution');
  return out;
}

/**
 * The notice for a photo, or null when the photo looks like it came from a
 * good camera (or there was too little detail to judge).
 */
export function lensNotice(report: LensReport | null, profile?: LensProfile): LensNotice | null {
  if (!report || !report.reliable || report.grade >= BASIC_GRADE) return null;
  const signs = signsOf(report);
  if (lensIsBasic(profile)) {
    return {
      scope: 'lens',
      title: 'Your camera’s lens is holding your photos back',
      body: 'Its lens captures less detail than high-end cameras, so even with Clarity your photos may not look their best. Transform gives them the look of a high-end camera, and keeps your effects.',
      signs,
    };
  }
  return {
    scope: 'photo',
    title: 'This photo could look sharper',
    body: 'It shows signs of a basic camera lens, so it may not reach top quality as it is. Transform gives it the look of a high-end camera, and keeps your effects.',
    signs,
  };
}
