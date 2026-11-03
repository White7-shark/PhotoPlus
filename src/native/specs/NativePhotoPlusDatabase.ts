import type { TurboModule } from 'react-native';
import { TurboModuleRegistry } from 'react-native';

/**
 * Thin SQLite bridge (Kotlin: com.photoplus.storage.DatabaseModule).
 * Parameters and rows travel as JSON strings so heterogenous values
 * (null, numbers, strings) survive the bridge without lossy coercion.
 */
export interface Spec extends TurboModule {
  open(name: string): Promise<void>;
  close(name: string): Promise<void>;
  execute(name: string, sql: string, paramsJson: string): Promise<Object>;
  query(name: string, sql: string, paramsJson: string): Promise<string>;
  transaction(name: string, statementsJson: string): Promise<void>;
}

export default TurboModuleRegistry.get<Spec>('PhotoPlusDatabase');
