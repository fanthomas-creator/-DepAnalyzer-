#ifndef PARSER_PERL_H
#define PARSER_PERL_H

#include "types.h"

/* Parse Perl source files (.pl, .pm).
 * Detects:
 *   use strict;                        → pragma (ignored)
 *   use Moose;                         → external dep
 *   use DBI;                           → external dep
 *   use MyApp::Models::User;           → internal or external
 *   use parent 'MyApp::Base';          → dependency
 *   use base 'MyApp::Controller';      → dependency
 *   require './lib/utils.pl';          → internal
 *   require MyApp::Config;             → internal/external
 *   our $VERSION = '1.0';             → metadata
 *   sub my_func { }                    → FunctionIndex
 *   package MyApp::Controller;         → namespace
 *   $obj->method() / Class->static()  → calls
 */
void parser_perl_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_PERL_H */
