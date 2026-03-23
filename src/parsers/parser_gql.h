#ifndef PARSER_GQL_H
#define PARSER_GQL_H

#include "types.h"

/* Parse GraphQL schema and operation files (.graphql, .gql).
 * Detects:
 *   #import "./fragments/UserFields.graphql"  → internal (Apollo)
 *   type Query { }                            → FunctionIndex
 *   type Mutation { }                         → FunctionIndex
 *   type MyType { }                           → FunctionIndex
 *   interface MyInterface { }                 → FunctionIndex
 *   enum MyEnum { }                           → FunctionIndex
 *   input MyInput { }                         → FunctionIndex
 *   union MyUnion = TypeA | TypeB             → FunctionIndex + calls
 *   fragment MyFragment on TypeName           → FunctionIndex + call
 *   extend type Query { }                     → call
 *   scalar DateTime                           → FunctionIndex
 *   implements InterfaceName                  → call (dep)
 *   directive @myDir on FIELD_DEFINITION      → FunctionIndex
 */
void parser_gql_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif
