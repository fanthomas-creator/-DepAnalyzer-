#ifndef PARSER_SQL_H
#define PARSER_SQL_H

#include "types.h"

/* Parse SQL files.
 * Detects:
 *   \i file.sql / \ir file.sql      → internal file include (psql)
 *   SOURCE file.sql                 → internal (MySQL)
 *   .read file.sql                  → internal (SQLite)
 *   CREATE TABLE tablename          → FunctionIndex (table as "function")
 *   CREATE VIEW viewname            → FunctionIndex
 *   CREATE FUNCTION / PROCEDURE     → FunctionIndex
 *   CREATE INDEX ON tablename       → call (reference to table)
 *   INSERT INTO tablename           → call
 *   UPDATE tablename                → call
 *   FROM tablename / JOIN tablename → call
 *   REFERENCES tablename            → dependency (FK)
 */
void parser_sql_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_SQL_H */
