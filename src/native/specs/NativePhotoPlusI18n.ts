import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/**
 * The app's language (Kotlin: com.photoplus.i18n.I18nModule). The words
 * are Android string resources, so Google Play's app-string translation
 * reaches them; the language is Android's per-app language (also in the
 * phone's Settings > Apps > Photo Plus > Language).
 */
export interface Spec extends TurboModule {
  /** Every string of the app in a language (a BCP 47 tag; "" for the app's current language). */
  getStrings(tag: string): Object;
  /** The language chosen for the app ("" when it follows the phone). */
  getLanguage(): string;
  /** The phone's own language (a BCP 47 tag). */
  getSystemLanguage(): string;
  /** Sets the app's language ("" to follow the phone); the app starts again in it. */
  setLanguage(tag: string): void;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusI18n');
