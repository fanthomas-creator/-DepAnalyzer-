#ifndef PARSER_CMAKE_H
#define PARSER_CMAKE_H
#include "types.h"
/* Parse CMake files (CMakeLists.txt, *.cmake).
 * find_package(OpenSSL REQUIRED)    → external dep
 * find_package(Boost COMPONENTS...) → external dep
 * add_subdirectory(src/lib)         → internal
 * include(cmake/utils.cmake)        → internal
 * target_link_libraries(app OpenSSL::SSL) → dep
 * FetchContent_Declare(json ...)    → external dep
 * project(MyProject)                → def
 * add_executable / add_library      → def
 */
void parser_cmake_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);
#endif
