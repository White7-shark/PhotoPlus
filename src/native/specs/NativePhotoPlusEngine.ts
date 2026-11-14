import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/**
 * Bridge to the C/C++ processing engine (Kotlin: com.photoplus.engine.EngineModule).
 * Jobs run on a bounded native worker pool and can be cancelled by id.
 */
export interface Spec extends TurboModule {
  getEngineInfo(): Promise<Object>;
  process(jobId: string, request: Object): Promise<Object>;
  /** Restores a saved photo (denoise, deblur, tone; upscale 2 = super-resolution). */
  enhance(jobId: string, request: Object): Promise<Object>;
  analyzeQuality(path: string): Promise<Object>;
  /** Spotlight on a saved photo (subject stands out, natural skin retouching). */
  portrait(jobId: string, request: Object): Promise<Object>;
  /** Applies a look ("Vibe Match") to a saved photo. */
  applyLook(jobId: string, request: Object): Promise<Object>;
  /** Measures a reference photo's look. */
  analyzeLook(path: string): Promise<Object>;
  /** Measures what the camera's lens left in a photo (full resolution). */
  analyzeLens(path: string): Promise<Object>;
  /** Transform: high-end lens look, then the photo's effects re-applied. */
  transform(jobId: string, request: Object): Promise<Object>;
  /** Glide: makes a recorded clip steady, clean and sharp (+ Spotlight / Glow); progress as job events. */
  glide(jobId: string, request: Object): Promise<Object>;
  glideSubject(jobId: string, request: Object): Promise<Object>;
  /** Editor: renders a photo with its edit document (preview or saved copy). */
  renderEdit(jobId: string, request: Object): Promise<Object>;
  /** Export for sharing or the gallery: the watermark follows the native plan. */
  exportImage(jobId: string, request: Object): Promise<Object>;
  /** Faces (for fitting props) in the photo after its turn/straighten/crop. */
  detectFaces(request: Object): Promise<Object>;
  /** Suggested perspective fix for the photo after its turn/mirror ({ vertical }). */
  detectPerspective(request: Object): Promise<Object>;
  /** Every frame design around a photo, small (paths by frame id). */
  frameThumbnails(request: Object): Promise<Object>;
  /** Premium: cuts the tapped object out of a photo and saves it as a prop. */
  createCustomProp(jobId: string, request: Object): Promise<Object>;
  deleteCustomProp(id: string): Promise<boolean>;
  /** The plan held natively ("free" | "premium"). */
  getPlan(): Promise<string>;
  /** Debug builds only: switch plan to test Premium. */
  setDebugPlan(plan: string): Promise<boolean>;
  cancel(jobId: string): void;
  cancelAll(): void;
  getMemoryStats(): Promise<Object>;
  getProfileReport(): Promise<string>;
  resetProfiler(): void;
  resetMemoryPeak(): void;
  generateTestImage(width: number, height: number): Promise<string>;
  readImageMetadata(path: string): Promise<Object>;
  addListener(eventName: string): void;
  removeListeners(count: number): void;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusEngine');
