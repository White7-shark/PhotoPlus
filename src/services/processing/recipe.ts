/**
 * Recipes: what was done to a photo, so Transform can restore the untouched
 * photo and then re-apply every effect the person chose, exactly.
 *
 *   capture / edit ──▶ metadata.recipe = { basePath, steps[] }
 *   Transform      ──▶ restore basePath, then replay Spotlight and Vibe Match
 *
 * Clarity steps are not replayed: Transform includes everything Clarity does.
 * Multi-shot photos (Moonlight, Ultra Detail) cannot be re-made from a single
 * frame, so they are their own base, with their effects kept as they are.
 */

export interface SpotlightStep {
  kind: 'spotlight';
  /** Taps in the upright photo (x0, y0, x1, y1, … in 0..1). */
  points: number[];
  blur: number;
  retouch: number;
  brighten: number;
  pop: number;
}

/** Camera beauty (Glow and its face touches) on a photo taken outside Spotlight. */
export interface BeautyStep {
  kind: 'beauty';
  retouch: number;
  brighten: number;
  eyes: number;
  underEye: number;
  shine: number;
  faceLight: number;
}

export interface LookStep {
  kind: 'look';
  look: number[];
  strength: number;
}

export type RecipeStep = { kind: 'clarity' } | SpotlightStep | BeautyStep | LookStep;

export interface Recipe {
  /** The photo the steps start from. */
  basePath: string;
  /** True when the base already carries effects that can't be re-made (keep its colours and vignette). */
  baseStyled: boolean;
  steps: RecipeStep[];
}

type Metadata = Record<string, unknown>;

const isUnit = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v) && v >= 0 && v <= 1;
const str = (v: unknown): string | null => (typeof v === 'string' && v.length > 0 ? v : null);

function isStep(v: unknown): v is RecipeStep {
  if (!v || typeof v !== 'object') return false;
  const s = v as Record<string, unknown>;
  switch (s.kind) {
    case 'clarity':
      return true;
    case 'spotlight':
      return (
        Array.isArray(s.points) &&
        s.points.length % 2 === 0 &&
        s.points.length <= 16 &&
        s.points.every(isUnit) &&
        [s.blur, s.retouch, s.brighten, s.pop].every(isUnit)
      );
    case 'beauty':
      return [s.retouch, s.brighten, s.eyes, s.underEye, s.shine, s.faceLight].every(isUnit);
    case 'look':
      return (
        Array.isArray(s.look) &&
        s.look.length > 0 &&
        s.look.every(n => typeof n === 'number' && Number.isFinite(n)) &&
        isUnit(s.strength)
      );
    default:
      return false;
  }
}

export function isRecipe(v: unknown): v is Recipe {
  if (!v || typeof v !== 'object') return false;
  const r = v as Record<string, unknown>;
  return (
    str(r.basePath) !== null &&
    typeof r.baseStyled === 'boolean' &&
    Array.isArray(r.steps) &&
    r.steps.length <= 12 &&
    r.steps.every(isStep)
  );
}

export function recipeOf(metadata: Metadata): Recipe | null {
  return isRecipe(metadata.recipe) ? metadata.recipe : null;
}

/** Capture modes made from several shots: they can't be re-made from the saved frame. */
const MULTI_SHOT = new Set(['night', 'superres']);

/**
 * Recipe for a fresh capture. Single-shot modes keep their untouched frame
 * (`originalPath`), so it becomes the base and the effects are the steps.
 */
export function captureRecipe(shot: {
  path: string;
  mode: string;
  originalPath?: string | null;
  /** The photo without the Photo Plus mark (free plan). */
  cleanPath?: string | null;
  spotlight?: { points: number[]; blur: number; retouch: number; brighten: number; pop: number } | null;
  beauty?: Omit<BeautyStep, 'kind'> | null;
  look?: { look: number[]; strength: number } | null;
}): Recipe | null {
  const steps: RecipeStep[] = [];
  if (shot.mode === 'enhance') steps.push({ kind: 'clarity' });
  if (shot.spotlight) steps.push({ kind: 'spotlight', ...shot.spotlight });
  if (shot.beauty) steps.push({ kind: 'beauty', ...shot.beauty });
  if (shot.look) steps.push({ kind: 'look', look: shot.look.look, strength: shot.look.strength });
  const original = str(shot.originalPath);
  const recipe: Recipe =
    MULTI_SHOT.has(shot.mode) || !original
      ? { basePath: str(shot.cleanPath) ?? shot.path, baseStyled: steps.some(s => s.kind !== 'clarity'), steps: [] }
      : { basePath: original, baseStyled: false, steps };
  return isRecipe(recipe) ? recipe : null;
}

/** Whether a photo without a recipe already carries effects (colours or blur) that must be kept. */
function looksStyled(metadata: Metadata): boolean {
  const mode = str(metadata.captureMode);
  return metadata.look === true || !!metadata.portrait || mode === 'spotlight' || mode === 'look';
  // (Camera beauty is recorded as a recipe step, so it never needs this guess.)
}

/** The recipe of a photo, or the best stand-in for photos saved before recipes existed. */
export function effectiveRecipe(image: { path: string; metadata: Metadata }): Recipe {
  const own = recipeOf(image.metadata);
  if (own) return own;
  const mode = str(image.metadata.captureMode);
  const original = str(image.metadata.originalPath);
  // A Clarity capture or edit with its untouched original: start over from that.
  if (mode === 'enhance' && original && !looksStyled(image.metadata)) {
    return { basePath: original, baseStyled: false, steps: [{ kind: 'clarity' }] };
  }
  return { basePath: str(image.metadata.cleanPath) ?? image.path, baseStyled: looksStyled(image.metadata), steps: [] };
}

/** Recipe for an edited copy: the source's recipe plus this edit. */
export function editRecipe(source: { path: string; metadata: Metadata }, step: RecipeStep): Recipe {
  // A kept Transform result carries its source's recipe, so this still
  // starts from the untouched photo and Transform can always start over.
  const base = effectiveRecipe(source);
  return { ...base, steps: [...base.steps, step].slice(-12) };
}

export interface TransformPlan {
  inputPath: string;
  keepStyle: boolean;
  spotlight: Omit<SpotlightStep, 'kind'> | null;
  beauty: Omit<BeautyStep, 'kind'> | null;
  look: { look: number[]; strength: number } | null;
  /** Number of effects re-applied (for "your effects are kept"). */
  replayed: number;
}

/**
 * What Transform does for a photo: restore its base, then the latest
 * Spotlight and the latest Vibe Match (each applies once, as the photo
 * shows them).
 */
export function transformPlan(image: { path: string; metadata: Metadata }): TransformPlan {
  const r = effectiveRecipe(image);
  let spotlight: TransformPlan['spotlight'] = null;
  let beauty: TransformPlan['beauty'] = null;
  let look: TransformPlan['look'] = null;
  for (const s of r.steps) {
    if (s.kind === 'spotlight') {
      const { kind: _kind, ...rest } = s;
      spotlight = rest;
    } else if (s.kind === 'beauty') {
      const { kind: _kind, ...rest } = s;
      beauty = rest;
    } else if (s.kind === 'look') {
      look = { look: s.look, strength: s.strength };
    }
  }
  return {
    inputPath: r.basePath,
    keepStyle: r.baseStyled,
    spotlight,
    beauty,
    look,
    replayed: (spotlight ? 1 : 0) + (beauty ? 1 : 0) + (look ? 1 : 0),
  };
}
