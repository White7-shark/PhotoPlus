import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/**
 * Lucid, the see-through theme (Kotlin: com.photoplus.lucid.LucidModule):
 * the phone's wallpaper (as a picture, or through a see-through window), a
 * photo or clip picked as the background, and what a picture is like.
 * Stats: { mean, p10, p50, p90, saturation, colors: ["#RRGGBB", …] }.
 */
export interface Spec extends TurboModule {
  /** { path: string | null, stats: Stats | null, live, colors: string[], darkText }. */
  wallpaper(): Promise<Object>;
  /** Stats of a picture file, or null when it can't be read. */
  imageStats(path: string): Promise<Object | null>;
  /** Shows the phone's wallpaper behind the app's see-through window (or stops). */
  showWallpaper(on: boolean): Promise<boolean>;
  /** The photo picker for a 'photo' or a 'video'; the choice is copied into the app: { path, stats } or null. */
  pickBackground(kind: string): Promise<Object | null>;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusLucid');
