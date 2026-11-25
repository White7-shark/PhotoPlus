/**
 * Batch editing (Phase 12): one setup reapplied to many photos, one after
 * another on the device. A product setup makes each photo a catalogue shot;
 * a look edits each photo's colour (and can crop it to a shape). Every
 * result is a new photo beside its original: originals are never changed,
 * and each result remembers where it came from.
 */
import type { Repositories } from '../database/Database';
import type { ImageRecord } from '../database/repositories/ImagesRepository';
import { EMPTY_DOCUMENT, type EditDocument } from '../edit/document';
import type { WatermarkSpec } from '../edit/watermark';
import type { RenderEditRequest, RenderEditResult } from '../processing/ImageProcessingService';
import { cleanOf, editedPathFor } from '../processing/quality';
import type { ProductService } from './ProductService';
import { ProductError } from './ProductService';
import type { ExportProfile, LookPreset, ProductSetup } from './setup';

/** The free plan's batch; Premium batches up to MAX_BATCH. */
export const FREE_BATCH = 3;
export const MAX_BATCH = 50;

export type BatchJob =
  | {
      kind: 'product';
      /** What it is called (kept with each result). */
      name: string;
      setup: ProductSetup;
      profile: ExportProfile;
      /** Short edge, px. */
      resolution: number;
      watermark?: WatermarkSpec | null;
    }
  | {
      kind: 'look';
      name: string;
      look: LookPreset['look'];
      /** Crop each photo to this shape (centred), or keep it as shot. */
      shape: { aspectW: number; aspectH: number } | null;
      watermark: WatermarkSpec;
    };

export type BatchStatus = 'waiting' | 'working' | 'done' | 'failed';

export interface BatchItem {
  image: ImageRecord;
  status: BatchStatus;
  result?: ImageRecord;
  error?: string;
}

export interface BatchDeps {
  repos: Pick<Repositories, 'images'>;
  product: Pick<ProductService, 'open' | 'render' | 'close'>;
  renderEdit: (r: RenderEditRequest) => Promise<RenderEditResult>;
  exists: (path: string) => Promise<boolean>;
}

export interface BatchOptions {
  /** Where results go (null: beside each original). */
  projectId: string | null;
  /** Set `cancelled` to stop after the photo being worked on. */
  signal?: { cancelled: boolean };
  onItem?: (index: number, item: BatchItem) => void;
}

/** A centred crop of a w x h photo to aspectW:aspectH (fractions), or null when it already is that shape. */
export function shapeCrop(w: number, h: number, aspectW: number, aspectH: number): EditDocument['crop'] {
  if (!(w > 0 && h > 0 && aspectW > 0 && aspectH > 0)) return null;
  const a = w / h;
  const t = aspectW / aspectH;
  if (Math.abs(a - t) < 0.005) return null;
  if (a > t) {
    const cw = t / a;
    return { x: (1 - cw) / 2, y: 0, w: cw, h: 1 };
  }
  const ch = a / t;
  return { x: 0, y: (1 - ch) / 2, w: 1, h: ch };
}

/** The look as an edit of one photo. */
export function lookDocument(job: Extract<BatchJob, { kind: 'look' }>, image: ImageRecord): EditDocument {
  return {
    ...EMPTY_DOCUMENT,
    ...job.look,
    crop: job.shape ? shapeCrop(image.width, image.height, job.shape.aspectW, job.shape.aspectH) : null,
  };
}

async function freePath(base: string, exists: BatchDeps['exists']) {
  let out = editedPathFor(base, 'look');
  for (let n = 1; (await exists(out)) && n < 200; n++) out = editedPathFor(base, 'look', n);
  return out;
}

const message = (e: unknown) => (e instanceof Error ? e.message : 'Something went wrong');

/**
 * Runs the job over the photos in order. A photo that fails is marked and
 * the batch goes on; a Premium refusal stops it (every photo would fail).
 */
export async function runBatch(
  images: readonly ImageRecord[],
  job: BatchJob,
  deps: BatchDeps,
  opts: BatchOptions,
): Promise<BatchItem[]> {
  const items: BatchItem[] = images.slice(0, MAX_BATCH).map(image => ({ image, status: 'waiting' }));
  const report = (i: number) => opts.onItem?.(i, { ...items[i] });
  for (let i = 0; i < items.length; i++) {
    if (opts.signal?.cancelled) break;
    const item = items[i];
    item.status = 'working';
    report(i);
    try {
      item.result =
        job.kind === 'product'
          ? await productOne(item.image, job, deps, opts)
          : await lookOne(item.image, job, deps, opts);
      item.status = 'done';
    } catch (e) {
      item.status = 'failed';
      item.error = message(e);
      report(i);
      if (e instanceof ProductError && e.code === 'PREMIUM_REQUIRED') throw e;
      continue;
    }
    report(i);
  }
  return items;
}

async function productOne(
  image: ImageRecord,
  job: Extract<BatchJob, { kind: 'product' }>,
  deps: BatchDeps,
  opts: BatchOptions,
) {
  const original = cleanOf(image);
  const session = await deps.product.open(original);
  try {
    if (!session.report.found) throw new Error('No product was found in this photo');
    const shot = await deps.product.render(
      session.sessionId,
      job.setup,
      job.profile,
      job.resolution,
      'full',
      job.watermark,
    );
    return await deps.repos.images.add({
      projectId: opts.projectId ?? image.projectId,
      path: shot.outputPath,
      source: 'export',
      width: shot.width,
      height: shot.height,
      sizeBytes: shot.sizeBytes,
      mimeType: shot.format === 'png' ? 'image/png' : 'image/jpeg',
      capturedAt: image.capturedAt,
      metadata: {
        captureMode: 'product',
        sourceImageId: image.id,
        originalPath: original,
        originalOwned: false,
        cleanPath: shot.cleanPath,
        product: { setup: job.setup, profile: job.profile.id, resolution: job.resolution },
        batch: job.name,
      },
    });
  } finally {
    deps.product.close(session.sessionId);
  }
}

async function lookOne(
  image: ImageRecord,
  job: Extract<BatchJob, { kind: 'look' }>,
  deps: BatchDeps,
  opts: BatchOptions,
) {
  // An edited photo is re-edited from its untouched photo.
  const saved = typeof image.metadata.editBase === 'string' ? image.metadata.editBase : null;
  const base = saved ?? cleanOf(image);
  const doc = lookDocument(job, image);
  const outputPath = await freePath(base, deps.exists);
  const r = await deps.renderEdit({
    inputPath: base,
    outputPath,
    doc,
    watermark: job.watermark,
    preview: false,
    cleanOutputPath: outputPath.replace(/\.jpg$/, '_clean.jpg'),
  });
  return deps.repos.images.add({
    projectId: opts.projectId ?? image.projectId,
    path: r.outputPath,
    source: 'export',
    width: r.width,
    height: r.height,
    sizeBytes: r.sizeBytes,
    mimeType: 'image/jpeg',
    capturedAt: image.capturedAt,
    metadata: {
      captureMode: 'edit',
      sourceImageId: image.id,
      originalPath: base,
      originalOwned: false,
      cleanPath: r.cleanPath,
      editBase: base,
      edit: doc,
      recipe: { basePath: r.cleanPath ?? r.outputPath, baseStyled: true, steps: [] },
      batch: job.name,
    },
  });
}
