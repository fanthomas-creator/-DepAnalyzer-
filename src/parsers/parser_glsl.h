#ifndef PARSER_GLSL_H
#define PARSER_GLSL_H
#include "types.h"
/* Parse GLSL/WGSL shader files (.glsl, .vert, .frag, .comp, .wgsl, .hlsl).
 * #include "common.glsl"                → internal (GLSL extension)
 * #pragma include("utils.glsl")         → internal
 * void main() / vec4 myFunc()           → FunctionIndex
 * uniform sampler2D myTexture           → uniform dep
 * layout(binding=0) uniform MyBlock     → uniform block dep
 * // WGSL: @group(0) @binding(0) var   → binding dep
 */
void parser_glsl_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);
#endif
