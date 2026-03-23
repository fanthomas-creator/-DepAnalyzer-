#ifndef PARSER_GROOVY_H
#define PARSER_GROOVY_H
#include "types.h"
/* Parse Groovy (.groovy, .gradle, Jenkinsfile).
 * import org.springframework.web.bind.annotation.*  → external
 * import com.example.MyClass                        → external/internal
 * @Grab('group:artifact:version')                   → external dep
 * implementation 'com.example:lib:1.0'              → Gradle dep
 * classpath 'org.springframework.boot:...'          → Gradle dep
 * compile group: 'log4j', name: 'log4j'             → Gradle dep
 * def myMethod(args) / class Foo / def Foo          → FunctionIndex
 */
void parser_groovy_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);
#endif
