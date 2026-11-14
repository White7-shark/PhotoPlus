/**
 * The photoshoot studio's bridge to the device (StudioModule): sessions, the
 * cut-out with corrections, scenes rendered as previews or full-quality
 * photos, and saving to the gallery. Results are checked before use.
 */
import { errorCode, errorMessage, nativeModules, requireNative, type StudioSpec } from '../../native';
import { createId } from '../database/ids';
import type { Background, Crop, Placement, SceneLook } from './scene';

export type Brush = 'keep' | 'remove' | 'hair';
export const BRUSH_ID: Record<Brush, number> = { keep: 0, remove: 1, hair: 2 };

export interface Stroke {
  brush: Brush;
  /** Share of the photo's short side. */
  radius: number;
  /** Photo fractions: x0, y0, x1, y1, … */
  points: number[];
}

export interface EdgeSettings {
  softness: number;
  shift: number;
  hairDetail: number;
}

export const DEFAULT_EDGE: EdgeSettings = { softness: 0, shift: 0, hairDetail: 0.5 };

export interface CutoutReport {
  coverage: number;
  edge: number;
  hairEdge: number;
  box: [number, number, number, number];
  cutLeft: boolean;
  cutTop: boolean;
  cutRight: boolean;
  cutBottom: boolean;
  found: boolean;
}

export interface CutoutResult {
  cutoutPath: string;
  width: number;
  height: number;
  report: CutoutReport;
}

export interface Session extends CutoutResult {
  sessionId: string;
  photoPath: string;
  /** The person-parts map was found (hair detail works best with it). */
  parts: boolean;
}

export interface SceneResult {
  outputPath: string;
  width: number;
  height: number;
  sizeBytes: number;
  format: 'jpeg' | 'png';
  box?: [number, number, number, number];
  exposure?: number;
  grounded?: boolean;
}

export class StudioError extends Error {
  constructor(
    message: string,
    readonly code: string,
  ) {
    super(message);
    this.name = 'StudioError';
  }
}

const num = (v: unknown) => typeof v === 'number' && Number.isFinite(v);

function isReport(v: unknown): v is CutoutReport {
  if (!v || typeof v !== 'object') return false;
  const r = v as Record<string, unknown>;
  return num(r.coverage) && Array.isArray(r.box) && r.box.length === 4 && r.box.every(num) && typeof r.cutBottom === 'boolean';
}

function isCutout(v: unknown): v is CutoutResult {
  if (!v || typeof v !== 'object') return false;
  const r = v as Record<string, unknown>;
  return typeof r.cutoutPath === 'string' && num(r.width) && num(r.height) && isReport(r.report);
}

function isScene(v: unknown): v is SceneResult {
  if (!v || typeof v !== 'object') return false;
  const r = v as Record<string, unknown>;
  return typeof r.outputPath === 'string' && num(r.width) && num(r.height) && (r.format === 'jpeg' || r.format === 'png');
}

export function backgroundRequest(bg: Background): Record<string, unknown> {
  switch (bg.kind) {
    case 'plate':
      return bg.plate.source === 'builtin' ? { kind: 'plate', id: bg.plate.id } : { kind: 'photo', path: bg.plate.path };
    case 'plain':
      return { kind: 'plain', color: bg.plain.color, vignette: bg.plain.vignette };
    default:
      return { kind: 'transparent' };
  }
}

export function strokeRequest(strokes: readonly Stroke[]) {
  return strokes.map(s => ({ brush: BRUSH_ID[s.brush], radius: s.radius, points: s.points }));
}

export class StudioService {
  constructor(private readonly native: StudioSpec) {}

  private async call<T>(run: () => Promise<unknown>, check: (v: unknown) => v is T): Promise<T> {
    let out: unknown;
    try {
      out = await run();
    } catch (error) {
      throw new StudioError(errorMessage(error), errorCode(error) ?? 'INTERNAL');
    }
    if (!check(out)) throw new StudioError('The studio returned something unexpected', 'INTERNAL');
    return out;
  }

  /** Finds the person (taps choose who in a group photo) and makes the first cut-out. */
  start(inputPath: string, taps: readonly { x: number; y: number }[] = []): Promise<Session> {
    return this.call(
      () => this.native.startSession(createId('studio'), { inputPath, points: taps.flatMap(p => [p.x, p.y]) }),
      (v): v is Session => isCutout(v) && typeof (v as Session).sessionId === 'string' && typeof (v as Session).photoPath === 'string',
    );
  }

  refine(sessionId: string, strokes: readonly Stroke[], edge: EdgeSettings): Promise<CutoutResult> {
    return this.call(
      () => this.native.refineCutout(createId('cut'), { sessionId, strokes: strokeRequest(strokes), ...edge }),
      isCutout,
    );
  }

  render(
    sessionId: string,
    scene: { background: Background; crop: Crop; placement: Placement; look: SceneLook; aspect?: number },
    quality: 'preview' | 'full',
    hidePerson = false,
  ): Promise<SceneResult> {
    return this.call(
      () =>
        this.native.renderScene(createId('scene'), {
          sessionId,
          background: backgroundRequest(scene.background),
          crop: scene.crop,
          placement: scene.placement,
          look: scene.look,
          aspect: scene.aspect,
          quality,
          hidePerson,
        }),
      isScene,
    );
  }

  async saveToGallery(path: string): Promise<string> {
    try {
      return await this.native.saveToGallery(path);
    } catch (error) {
      throw new StudioError(errorMessage(error), errorCode(error) ?? 'INTERNAL');
    }
  }

  end(sessionId: string) {
    try {
      this.native.endSession(sessionId);
    } catch {
      // Ending a session is best effort.
    }
  }
}

let service: StudioService | null = null;
export function getStudioService(): StudioService {
  if (!service) service = new StudioService(requireNative(nativeModules.studio, 'PhotoPlusStudio'));
  return service;
}
