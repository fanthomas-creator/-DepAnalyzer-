#ifndef PARSER_DART_H
#define PARSER_DART_H

#include "types.h"

/* Parse Dart / Flutter source files.
 * Detects:
 *   import 'dart:core'                  → external (dart: SDK)
 *   import 'dart:async'                 → external (dart: SDK)
 *   import 'package:flutter/material.dart' → external (package:)
 *   import 'package:http/http.dart'     → external
 *   import '../models/user.dart'        → internal (relative)
 *   import './utils/helpers.dart'       → internal
 *   export 'package:...'                → re-export dep
 *   part 'user.g.dart'                  → internal part file
 *   part of 'base.dart'                 → internal
 *   class Foo / abstract class          → FunctionIndex
 *   mixin Foo                           → FunctionIndex
 *   extension Foo on Type               → FunctionIndex
 *   void myFunc() / Future<T> func()    → FunctionIndex
 *   obj.method() / ClassName.static()  → calls
 */
void parser_dart_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_DART_H */
