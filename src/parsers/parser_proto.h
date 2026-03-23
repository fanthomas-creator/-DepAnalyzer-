#ifndef PARSER_PROTO_H
#define PARSER_PROTO_H

#include "types.h"

/* Parse Protocol Buffer files (.proto).
 * Detects:
 *   import "google/protobuf/timestamp.proto"  → external
 *   import "other_service.proto"              → internal
 *   import weak "optional.proto"              → internal
 *   import public "base.proto"                → internal
 *   message MyMessage { }                     → FunctionIndex
 *   service MyService { }                     → FunctionIndex
 *   enum MyEnum { }                           → FunctionIndex
 *   rpc MyMethod (Request) returns (Response) → FunctionIndex + calls
 *   option java_package = "com.example"       → metadata
 */
void parser_proto_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif
