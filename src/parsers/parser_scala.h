#ifndef PARSER_SCALA_H
#define PARSER_SCALA_H

#include "types.h"

/* Parse Scala source files.
 * Detects:
 *   import com.example.MyClass        → import
 *   import scala.collection.mutable._ → external (scala.*)
 *   import java.util.{List, Map}       → external (java.*)
 *   import akka.actor.ActorSystem      → external
 *   class Foo / case class / object    → FunctionIndex
 *   trait Foo / abstract class Foo     → FunctionIndex
 *   def myMethod(...) / val fn = (x)   → FunctionIndex
 *   @annotation                        → dependency hint
 *   extends / with TraitName           → dependency
 *   obj.method() / Class.static()      → calls
 */
void parser_scala_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_SCALA_H */
