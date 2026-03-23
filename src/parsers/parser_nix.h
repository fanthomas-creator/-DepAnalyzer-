#ifndef PARSER_NIX_H
#define PARSER_NIX_H
#include "types.h"
/* Parse Nix expression files (.nix).
 * import ./other.nix                 → internal
 * import <nixpkgs>                   → external channel
 * pkgs.callPackage ./pkg/default.nix → internal
 * with pkgs; [ curl git nodejs ]     → external deps
 * buildInputs = [ openssl libxml2 ]  → external deps
 * let x = import ./lib.nix; in ...  → internal
 * { pkgs, lib, ... }:               → function input (deps)
 */
void parser_nix_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);
#endif
