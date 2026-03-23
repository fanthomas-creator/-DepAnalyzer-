#ifndef PARSER_BAZEL_H
#define PARSER_BAZEL_H
#include "types.h"
/* Parse Bazel BUILD/WORKSPACE files.
 * load("@rules_go//go:def.bzl", "go_binary")  → external ruleset
 * load("//lib:defs.bzl", "my_rule")            → internal
 * http_archive(name="...", url="...")           → external dep
 * git_repository(name="...", remote="...")      → external dep
 * deps = ["//src/lib:mylib", "@maven//:guava"]  → internal/external
 * go_binary(name="app", deps=[...])             → def
 * cc_library / py_library / java_library        → def
 */
void parser_bazel_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);
#endif
