import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/**
 * The photoshoot studio (Kotlin: com.photoplus.studio.StudioModule): a person
 * cut out of their photo and placed into another scene, fully on the device.
 * A session keeps the photo and its person masks while the person corrects
 * the cut-out and tries backgrounds.
 */
export interface Spec extends TurboModule {
  /** Finds the person in a photo: a session and a first cut-out (previews). */
  startSession(jobId: string, request: Object): Promise<Object>;
  /** The cut-out again with corrections (strokes and edge settings). */
  refineCutout(jobId: string, request: Object): Promise<Object>;
  /** The person in a scene: a preview, or the full-quality photo (Premium). */
  renderScene(jobId: string, request: Object): Promise<Object>;
  /** Relight: the person, relit, back in their own photo (a preview, or the saved photo). */
  renderRelight(jobId: string, request: Object): Promise<Object>;
  /**
   * Photo Arts: { inputPath, artist 0..19, level 0..2, quality 'thumb' | 'preview' | 'full', seed, mode 'paint' | 'pencil',
   * controls, pencil { medium, paper, intensity, lines, shading }, frame, signature (Premium), live } →
   * { outputPath, cleanPath, width, height, remaining, inner [x0, y0, x1, y1], blankPath (live) }.
   */
  renderMasters(jobId: string, request: Object): Promise<Object>;
  /** { remaining (-1: unlimited), limit }: finished Masters paintings left today on the free plan. */
  mastersAllowance(): Promise<Object>;
  /** { remaining (-1: unlimited), limit, credits, needsAd }: Pencil Art drawings today on the free plan (each after an ad). */
  pencilAllowance(): Promise<Object>;
  /** Photo Motion: reads a photo once ({ inputPath }) → { sessionId, width, height, sky 0..1, depth, faces }. */
  startMotion(jobId: string, request: Object): Promise<Object>;
  /**
   * Photo Motion: the loop as an MP4, a quick preview or the finished video
   * ({ sessionId, quality, duration, camera, cameraAmount, flowSpeed, sky,
   * skySpeed, effect, effectAmount, loop, keepFaces, arrows, anchors }) →
   * { outputPath, posterPath, width, height, durationMs, remaining }.
   */
  renderMotion(jobId: string, request: Object): Promise<Object>;
  endMotion(sessionId: string): void;
  /** { remaining (-1: unlimited), limit, credits, needsAd }: the free plan's videos today. */
  motionAllowance(): Promise<Object>;
  /** Film & Collage: photos as frames on a film strip (a preview, or the saved photo). */
  renderFilm(jobId: string, request: Object): Promise<Object>;
  /** Film & Collage: photos in a collage (a preview, or the saved photo). */
  renderCollage(jobId: string, request: Object): Promise<Object>;
  /** Adds a saved studio photo to the device gallery (Pictures/Photo Plus). */
  saveToGallery(path: string): Promise<string>;
  endSession(sessionId: string): void;
  cancel(jobId: string): void;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusStudio');
