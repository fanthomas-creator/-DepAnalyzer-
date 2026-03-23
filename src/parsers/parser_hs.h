#ifndef PARSER_HS_H
#define PARSER_HS_H

#include "types.h"

/* Parse Haskell source files (.hs, .lhs).
 * Detects:
 *   import Data.Map                   → external (Data.*)
 *   import qualified Data.Map as Map  → external
 *   import Control.Monad (forM_)      → external
 *   import MyApp.Utils                → internal (project module)
 *   module MyApp.Models where         → module declaration
 *   data MyType = ...                 → FunctionIndex
 *   newtype Wrapper = ...             → FunctionIndex
 *   type Alias = ...                  → FunctionIndex
 *   myFunc :: Type -> Type            → FunctionIndex (type sig)
 *   myFunc x y = ...                  → FunctionIndex (def)
 *   class MyClass a where             → FunctionIndex
 *   instance MyClass Int where        → dependency
 */
void parser_hs_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_HS_H */
