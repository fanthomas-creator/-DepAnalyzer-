#ifndef PARSER_SWIFT_H
#define PARSER_SWIFT_H

#include "types.h"

/* Parse Swift source files.
 * Detects:
 *   import Foundation                → external framework
 *   import UIKit                     → external framework
 *   import MyModule                  → internal/external module
 *   @import ModuleName               → ObjC bridging
 *   class / struct / enum / protocol / actor / extension → FunctionIndex
 *   func myFunc(...) / static func / class func / override func → FunctionIndex
 *   @Observable / @State / @Binding → SwiftUI property wrappers → deps
 *   obj.method() / Type.method()    → calls
 */
void parser_swift_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_SWIFT_H */
