import initSqlJs, { type Database } from 'sql.js';

import type { ExecuteResult, SqlExecutor, SqlParam, SqlStatement } from '../../src/services/database/SqlExecutor';

/**
 * Real SQLite (compiled to WebAssembly) behind the app's SqlExecutor
 * interface. Mirrors the Kotlin bridge semantics: booleans bind as 0/1,
 * foreign keys are enforced, transactions roll back on error.
 */
export class SqlJsExecutor implements SqlExecutor {
  private constructor(readonly db: Database) {}

  static async create(): Promise<SqlJsExecutor> {
    const SQL = await initSqlJs();
    const db = new SQL.Database();
    db.run('PRAGMA foreign_keys = ON');
    return new SqlJsExecutor(db);
  }

  private bind(params: SqlParam[] = []) {
    return params.map(p => (typeof p === 'boolean' ? (p ? 1 : 0) : p));
  }

  async execute(sql: string, params: SqlParam[] = []): Promise<ExecuteResult> {
    this.db.run(sql, this.bind(params));
    const rowsAffected = this.db.getRowsModified();
    const isInsert = /^\s*(insert|replace)/i.test(sql);
    const insertId = isInsert ? (this.db.exec('SELECT last_insert_rowid()')[0]?.values[0][0] as number) : null;
    return { rowsAffected, insertId };
  }

  async query<T = Record<string, unknown>>(sql: string, params: SqlParam[] = []): Promise<T[]> {
    const stmt = this.db.prepare(sql);
    try {
      stmt.bind(this.bind(params));
      const rows: T[] = [];
      while (stmt.step()) {
        rows.push(stmt.getAsObject() as T);
      }
      return rows;
    } finally {
      stmt.free();
    }
  }

  async transaction(statements: SqlStatement[]): Promise<void> {
    this.db.run('BEGIN');
    try {
      for (const s of statements) {
        this.db.run(s.sql, this.bind(s.params));
      }
      this.db.run('COMMIT');
    } catch (error) {
      this.db.run('ROLLBACK');
      throw error;
    }
  }
}
