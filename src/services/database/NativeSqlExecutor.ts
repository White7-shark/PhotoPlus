import { errorCode, errorMessage, nativeModules, requireNative, type DatabaseSpec } from '../../native';
import { DatabaseError, type ExecuteResult, type SqlExecutor, type SqlParam, type SqlStatement } from './SqlExecutor';

export class NativeSqlExecutor implements SqlExecutor {
  private constructor(private readonly name: string, private readonly native: DatabaseSpec) {}

  static async open(
    name: string,
    native: DatabaseSpec = requireNative(nativeModules.database, 'PhotoPlusDatabase'),
  ): Promise<NativeSqlExecutor> {
    await wrap(() => native.open(name));
    return new NativeSqlExecutor(name, native);
  }

  async execute(sql: string, params: SqlParam[] = []): Promise<ExecuteResult> {
    const result = (await wrap(() => this.native.execute(this.name, sql, JSON.stringify(params)))) as {
      rowsAffected?: number;
      insertId?: number | null;
    };
    return { rowsAffected: result.rowsAffected ?? 0, insertId: result.insertId ?? null };
  }

  async query<T = Record<string, unknown>>(sql: string, params: SqlParam[] = []): Promise<T[]> {
    const json = await wrap(() => this.native.query(this.name, sql, JSON.stringify(params)));
    return JSON.parse(json) as T[];
  }

  async transaction(statements: SqlStatement[]): Promise<void> {
    await wrap(() =>
      this.native.transaction(this.name, JSON.stringify(statements.map(s => ({ sql: s.sql, params: s.params ?? [] })))),
    );
  }

  close(): Promise<void> {
    return wrap(() => this.native.close(this.name));
  }
}

async function wrap<T>(fn: () => Promise<T>): Promise<T> {
  try {
    return await fn();
  } catch (error) {
    throw new DatabaseError(errorMessage(error), errorCode(error) ?? 'SQL_ERROR', error);
  }
}
