package com.photoplus.storage

import org.json.JSONArray
import org.json.JSONObject

/** Bound SQL parameter values, parsed from the JSON sent by JavaScript. */
sealed class SqlValue {
  data object Null : SqlValue()
  data class Integer(val value: Long) : SqlValue()
  data class Real(val value: Double) : SqlValue()
  data class Text(val value: String) : SqlValue()
}

data class SqlStatement(val sql: String, val params: List<SqlValue>)

class SqlBridgeException(message: String) : IllegalArgumentException(message)

object SqlValues {
  private val NAME_PATTERN = Regex("^[A-Za-z0-9_-]{1,64}$")

  fun requireValidName(name: String): String {
    if (!NAME_PATTERN.matches(name)) throw SqlBridgeException("Invalid database name '$name'")
    return name
  }

  fun parseParams(json: String?): List<SqlValue> {
    if (json.isNullOrBlank()) return emptyList()
    val array = try { JSONArray(json) } catch (e: Exception) { throw SqlBridgeException("params must be a JSON array") }
    return (0 until array.length()).map { i -> toValue(array.opt(i), i) }
  }

  fun parseStatements(json: String): List<SqlStatement> {
    val array = try { JSONArray(json) } catch (e: Exception) { throw SqlBridgeException("statements must be a JSON array") }
    return (0 until array.length()).map { i ->
      val obj = array.optJSONObject(i) ?: throw SqlBridgeException("Statement #$i must be an object")
      val sql = obj.optString("sql").takeIf { it.isNotBlank() } ?: throw SqlBridgeException("Statement #$i is missing sql")
      val params = obj.optJSONArray("params")
      SqlStatement(sql, if (params == null) emptyList() else parseParams(params.toString()))
    }
  }

  private fun toValue(raw: Any?, index: Int): SqlValue = when (raw) {
    null, JSONObject.NULL -> SqlValue.Null
    is Boolean -> SqlValue.Integer(if (raw) 1 else 0)
    is Int -> SqlValue.Integer(raw.toLong())
    is Long -> SqlValue.Integer(raw)
    is Number -> {
      val d = raw.toDouble()
      if (d.isNaN() || d.isInfinite()) throw SqlBridgeException("Param #$index must be finite")
      if (d == Math.floor(d) && Math.abs(d) < 9.007199254740992E15) SqlValue.Integer(d.toLong()) else SqlValue.Real(d)
    }
    is String -> SqlValue.Text(raw)
    else -> throw SqlBridgeException("Param #$index has unsupported type ${raw.javaClass.simpleName}")
  }

  /** Classifies a statement so the right SQLiteStatement executor is used. */
  enum class Kind { INSERT, UPDATE_DELETE, OTHER }

  fun kindOf(sql: String): Kind {
    val keyword = sql.trimStart().takeWhile { it.isLetter() }.uppercase()
    return when (keyword) {
      "INSERT", "REPLACE" -> Kind.INSERT
      "UPDATE", "DELETE" -> Kind.UPDATE_DELETE
      else -> Kind.OTHER
    }
  }
}
