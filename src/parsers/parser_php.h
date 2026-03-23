#ifndef PARSER_PHP_H
#define PARSER_PHP_H

#include "types.h"

/* Parse PHP files.
 * Detects:
 *   require 'file.php'          → internal
 *   require_once './lib.php'    → internal
 *   include 'header.php'        → internal
 *   use Vendor\Package\Class    → external (namespace)
 *   use App\Models\User         → internal (App namespace)
 *   namespace App\Controllers   → declares namespace
 *   class Foo / interface Foo / trait Foo / function foo() → FunctionIndex
 */
void parser_php_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_PHP_H */
