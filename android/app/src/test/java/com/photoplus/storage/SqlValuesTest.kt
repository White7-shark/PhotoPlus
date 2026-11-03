package com.photoplus.storage

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class SqlValuesTest {
  @Test fun parsesTypedParams() {
    val values = SqlValues.parseParams("""[null, 1, 2.5, "text", true, false, 9007199254740991]""")
    assertEquals(
      listOf(
        SqlValue.Null,
        SqlValue.Integer(1),
        SqlValue.Real(2.5),
        SqlValue.Text("text"),
        SqlValue.Integer(1),
        SqlValue.Integer(0),
        SqlValue.Integer(9007199254740991),
      ),
      values,
    )
  }

  @Test fun emptyOrBlankParamsAreEmpty() {
    assertEquals(emptyList<SqlValue>(), SqlValues.parseParams(""))
    assertEquals(emptyList<SqlValue>(), SqlValues.parseParams(null))
    assertEquals(emptyList<SqlValue>(), SqlValues.parseParams("[]"))
  }

  @Test fun rejectsNonArraysAndObjects() {
    assertThrows(SqlBridgeException::class.java) { SqlValues.parseParams("{}") }
    assertThrows(SqlBridgeException::class.java) { SqlValues.parseParams("""[{"a":1}]""") }
  }

  @Test fun parsesStatements() {
    val statements = SqlValues.parseStatements("""[{"sql":"INSERT INTO t VALUES (?)","params":[1]},{"sql":"DELETE FROM t"}]""")
    assertEquals(2, statements.size)
    assertEquals(listOf(SqlValue.Integer(1)), statements[0].params)
    assertEquals(emptyList<SqlValue>(), statements[1].params)
    assertThrows(SqlBridgeException::class.java) { SqlValues.parseStatements("""[{"params":[]}]""") }
  }

  @Test fun classifiesStatements() {
    assertEquals(SqlValues.Kind.INSERT, SqlValues.kindOf("  insert into x values (1)"))
    assertEquals(SqlValues.Kind.INSERT, SqlValues.kindOf("REPLACE INTO x VALUES (1)"))
    assertEquals(SqlValues.Kind.UPDATE_DELETE, SqlValues.kindOf("\nupdate x set a = 1"))
    assertEquals(SqlValues.Kind.UPDATE_DELETE, SqlValues.kindOf("DELETE FROM x"))
    assertEquals(SqlValues.Kind.OTHER, SqlValues.kindOf("CREATE TABLE x (a)"))
    assertEquals(SqlValues.Kind.OTHER, SqlValues.kindOf("SELECT 1"))
  }

  @Test fun validatesDatabaseNames() {
    assertEquals("photoplus", SqlValues.requireValidName("photoplus"))
    assertThrows(SqlBridgeException::class.java) { SqlValues.requireValidName("../evil") }
    assertThrows(SqlBridgeException::class.java) { SqlValues.requireValidName("") }
  }
}
