/**
 * Product photography's bridge to the device (ProductModule): a session per
 * photo, the cut-out with corrections, and the catalogue shot as a preview
 * or a saved photo. Results are checked before use.
 */
import { errorCode, errorMessage, nativeModules, requireNative, type ProductSpec } from '../../native';
import { createId } from '../database/ids';
import { strokeRequest, type Stroke } from '../studio/StudioService';
import type { WatermarkSpec } from '../edit/watermark';
import { profileRequest, setupRequest, type ExportProfile, type ProductSetup } from './setup';

export interface ProductReport {
  found: boolean;
  coverage: number;
  /** The product's box in the photo (left, top, right, bottom; 0..1). */
  box: [number, number, number, number];
  /** The photographed backdrop's colour (#RRGGBB). */
  backdrop: string;
  /** A plain backdrop (paper, a table, a wall): its colours shaped the edge and correct the colour. */
  plain: boolean;
  /** Separate products kept (a set). */
  objects: number;
}

export interface ProductCutout {
  cutoutPath: string;
  width: number;
  height: number;
  report: ProductReport;
}

export interface ProductSession extends ProductCutout {
  sessionId: string;
  photoPath: string;
  /** The object model found it (otherwise the backdrop's colours alone). */
  model: boolean;
}

export interface MatteSettings {
  /** 0..1: higher keeps colours closer to the backdrop's (white on white). */
  tolerance: number;
  softness: number;
  shift: number;
}
export const DEFAULT_MATTE: MatteSettings = { tolerance: 0.5, softness: 0.2, shift: 0 };

export interface ProductShot {
  outputPath: string;
  cleanPath: string | null;
  width: number;
  height: number;
  sizeBytes: number;
  format: 'jpeg' | 'png';
  box?: [number, number, number, number];
}

export class ProductError extends Error {
  constructor(
    message: string,
    readonly code: string,
  ) {
    super(message);
    this.name = 'ProductError';
  }
}

const num = (v: unknown) => typeof v === 'number' && Number.isFinite(v);

function isReport(v: unknown): v is ProductReport {
  if (!v || typeof v !== 'object') return false;
  const r = v as Record<string, unknown>;
  return (
    typeof r.found === 'boolean' &&
    num(r.coverage) &&
    Array.isArray(r.box) &&
    r.box.length === 4 &&
    r.box.every(num) &&
    typeof r.backdrop === 'string' &&
    typeof r.plain === 'boolean' &&
    num(r.objects)
  );
}

function isCutout(v: unknown): v is ProductCutout {
  if (!v || typeof v !== 'object') return false;
  const r = v as Record<string, unknown>;
  return typeof r.cutoutPath === 'string' && num(r.width) && num(r.height) && isReport(r.report);
}

function isShot(v: unknown): v is ProductShot {
  if (!v || typeof v !== 'object') return false;
  const r = v as Record<string, unknown>;
  return (
    typeof r.outputPath === 'string' && num(r.width) && num(r.height) && (r.format === 'jpeg' || r.format === 'png')
  );
}

export class ProductService {
  constructor(private readonly native: ProductSpec) {}

  private async call<T>(run: () => Promise<unknown>, check: (v: unknown) => v is T): Promise<T> {
    let out: unknown;
    try {
      out = await run();
    } catch (error) {
      throw new ProductError(errorMessage(error), errorCode(error) ?? 'INTERNAL');
    }
    if (!check(out)) throw new ProductError('Product photography returned something unexpected', 'INTERNAL');
    return out;
  }

  /** Finds the product (taps choose which objects) and makes the first cut-out. */
  open(inputPath: string, taps: readonly { x: number; y: number }[] = []): Promise<ProductSession> {
    return this.call(
      () => this.native.openProduct(createId('product'), { inputPath, points: taps.flatMap(p => [p.x, p.y]) }),
      (v): v is ProductSession =>
        isCutout(v) &&
        typeof (v as ProductSession).sessionId === 'string' &&
        typeof (v as ProductSession).photoPath === 'string',
    );
  }

  refine(sessionId: string, strokes: readonly Stroke[], matte: MatteSettings): Promise<ProductCutout> {
    return this.call(
      () => this.native.refineProduct(createId('pcut'), { sessionId, strokes: strokeRequest(strokes), ...matte }),
      isCutout,
    );
  }

  /** The catalogue shot: a quick preview, or the saved photo at the profile's shape and `resolution` (short edge, px). */
  render(
    sessionId: string,
    setup: ProductSetup,
    profile: ExportProfile,
    resolution: number,
    quality: 'preview' | 'full',
    watermark?: WatermarkSpec | null,
  ): Promise<ProductShot> {
    return this.call(
      () =>
        this.native.renderProduct(createId('pshot'), {
          sessionId,
          setup: setupRequest(setup),
          profile: profileRequest(profile, resolution),
          quality,
          ...(watermark ? { watermark } : {}),
        }),
      isShot,
    );
  }

  close(sessionId: string) {
    try {
      this.native.closeProduct(sessionId);
    } catch {
      // Already closed.
    }
  }
}

let service: ProductService | null = null;
export function getProductService(): ProductService {
  if (!service) service = new ProductService(requireNative(nativeModules.product, 'PhotoPlusProduct'));
  return service;
}
