package com.photoplus.storage

import android.database.Cursor
import android.database.sqlite.SQLiteCursor
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteException
import android.database.sqlite.SQLiteProgram
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.module.annotations.ReactModule
import com.photoplus.specs.NativePhotoPlusDatabaseSpec
import com.photoplus.util.toWritableMap
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors

/**
 * Minimal SQLite bridge. Schema, migrations and repositories live in
 * TypeScript (src/services/database) so they are unit tested against a real
 * SQLite engine on the host; this module only executes statements.
 * All work is serialised on one thread, matching SQLite's best-practice of a
 * single writer connection.
 */
@ReactModule(name = DatabaseModule.NAME)
class DatabaseModule(reactContext: ReactApplicationContext) : NativePhotoPlusDatabaseSpec(reactContext) {

  private val executor = Executors.newSingleThreadExecutor { r -> Thread(r, "pp-database") }
  private val databases = ConcurrentHashMap<String, SQLiteDatabase>()

  override fun getName() = NAME

  private fun run(promise: Promise, block: () -> Any?) {
    executor.execute {
      try {
        promise.resolve(block())
      } catch (e: SqlBridgeException) {
        promise.reject("SQL_INVALID", e.message, e)
      } catch (e: SQLiteException) {
        promise.reject("SQL_ERROR", e.message, e)
      } catch (e: Exception) {
        promise.reject("SQL_INTERNAL", e.message, e)
      }
    }
  }

  private fun db(name: String): SQLiteDatabase =
    databases[SqlValues.requireValidName(name)] ?: throw SqlBridgeException("Database '$name' is not open")

  override fun open(name: String, promise: Promise) = run(promise) {
    SqlValues.requireValidName(name)
    if (!databases.containsKey(name)) {
      val file = reactApplicationContext.getDatabasePath("$name.db")
      file.parentFile?.mkdirs()
      val database = SQLiteDatabase.openOrCreateDatabase(file, null)
      database.enableWriteAheadLogging()
      database.execSQL("PRAGMA foreign_keys = ON")
      databases[name] = database
    }
    null
  }

  override fun close(name: String, promise: Promise) = run(promise) {
    databases.remove(SqlValues.requireValidName(name))?.close()
    null
  }

  override fun execute(name: String, sql: String, paramsJson: String, promise: Promise) = run(promise) {
    val result = executeStatement(db(name), SqlStatement(sql, SqlValues.parseParams(paramsJson)))
    result.toWritableMap()
  }

  override fun query(name: String, sql: String, paramsJson: String, promise: Promise) = run(promise) {
    val params = SqlValues.parseParams(paramsJson)
    val cursor = db(name).rawQueryWithFactory(
      { _, driver, editTable, query ->
        bind(query, params)
        SQLiteCursor(driver, editTable, query)
      },
      sql, null, "",
    )
    cursor.use { toJson(it) }
  }

  override fun transaction(name: String, statementsJson: String, promise: Promise) = run(promise) {
    val database = db(name)
    val statements = SqlValues.parseStatements(statementsJson)
    database.beginTransaction()
    try {
      statements.forEach { executeStatement(database, it) }
      database.setTransactionSuccessful()
    } finally {
      database.endTransaction()
    }
    null
  }

  private fun executeStatement(database: SQLiteDatabase, statement: SqlStatement): Map<String, Any?> {
    val kind = SqlValues.kindOf(statement.sql)
    if (kind == SqlValues.Kind.OTHER && statement.params.isEmpty()) {
      database.execSQL(statement.sql)
      return mapOf("rowsAffected" to 0, "insertId" to null)
    }
    database.compileStatement(statement.sql).use { compiled ->
      bind(compiled, statement.params)
      return when (kind) {
        SqlValues.Kind.INSERT -> {
          val id = compiled.executeInsert()
          mapOf("rowsAffected" to if (id >= 0) 1 else 0, "insertId" to if (id >= 0) id.toDouble() else null)
        }
        SqlValues.Kind.UPDATE_DELETE -> mapOf("rowsAffected" to compiled.executeUpdateDelete(), "insertId" to null)
        SqlValues.Kind.OTHER -> {
          compiled.execute()
          mapOf("rowsAffected" to 0, "insertId" to null)
        }
      }
    }
  }

  private fun bind(program: SQLiteProgram, params: List<SqlValue>) {
    params.forEachIndexed { i, value ->
      val index = i + 1
      when (value) {
        SqlValue.Null -> program.bindNull(index)
        is SqlValue.Integer -> program.bindLong(index, value.value)
        is SqlValue.Real -> program.bindDouble(index, value.value)
        is SqlValue.Text -> program.bindString(index, value.value)
      }
    }
  }

  private fun toJson(cursor: Cursor): String {
    val rows = JSONArray()
    val columns = cursor.columnNames
    while (cursor.moveToNext()) {
      val row = JSONObject()
      for (i in columns.indices) {
        when (cursor.getType(i)) {
          Cursor.FIELD_TYPE_NULL -> row.put(columns[i], JSONObject.NULL)
          Cursor.FIELD_TYPE_INTEGER -> row.put(columns[i], cursor.getLong(i))
          Cursor.FIELD_TYPE_FLOAT -> row.put(columns[i], cursor.getDouble(i))
          Cursor.FIELD_TYPE_STRING -> row.put(columns[i], cursor.getString(i))
          Cursor.FIELD_TYPE_BLOB -> throw SqlBridgeException("BLOB columns are not supported over the bridge")
        }
      }
      rows.put(row)
    }
    return rows.toString()
  }

  override fun invalidate() {
    executor.execute {
      databases.values.forEach { it.close() }
      databases.clear()
    }
    executor.shutdown()
    super.invalidate()
  }

  companion object {
    const val NAME = "PhotoPlusDatabase"
  }
}
