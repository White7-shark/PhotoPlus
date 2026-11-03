export type SqlParam = string | number | boolean | null;

export interface ExecuteResult {
  rowsAffected: number;
  insertId: number | null;
}

export interface SqlStatement {
  sql: string;
  params?: SqlParam[];
}

/**
 * Storage-agnostic SQL surface. Production uses the native SQLite bridge;
 * tests use sql.js (real SQLite compiled to WebAssembly) so migrations and
 * repositories are verified against genuine SQL semantics.
 */
export interface SqlExecutor {
  execute(sql: string, params?: SqlParam[]): Promise<ExecuteResult>;
  query<T = Record<string, unknown>>(sql: string, params?: SqlParam[]): Promise<T[]>;
  /** Runs all statements atomically. */
  transaction(statements: SqlStatement[]): Promise<void>;
}

export class DatabaseError extends Error {
  constructor(message: string, readonly code: string = 'SQL_ERROR', readonly cause?: unknown) {
    super(message);
    this.name = 'DatabaseError';
  }
}
