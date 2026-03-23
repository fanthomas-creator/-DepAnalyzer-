#ifndef PARSER_CLJ_H
#define PARSER_CLJ_H
#include "types.h"
/* Parse Clojure source files (.clj, .cljs, .cljc).
 * (ns myapp.core                    → namespace
 *   (:require [clojure.string :as s] → external dep
 *             [myapp.utils :refer]   → internal dep
 *   (:import [java.util Date]))      → Java interop dep
 * (require '[clojure.set])           → external dep
 * (use 'clojure.string)              → external dep
 * (defn my-func [args])              → FunctionIndex
 * (defmacro my-macro [args])         → FunctionIndex
 * (defrecord MyRecord [fields])      → FunctionIndex
 * (defprotocol MyProto)              → FunctionIndex
 * (deftype MyType [fields])          → FunctionIndex
 */
void parser_clj_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);
#endif
