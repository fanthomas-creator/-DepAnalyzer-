#ifndef PARSER_YAML_H
#define PARSER_YAML_H

#include "types.h"

/* Parse YAML files relevant to dependency analysis:
 *   docker-compose.yml  → image:, build:, depends_on:, volumes:
 *   .github/workflows/  → uses: actions/checkout@v3
 *   k8s manifests       → image:, configMapRef:, secretRef:
 *   ansible playbooks   → hosts:, roles:, include_tasks:
 *   any YAML            → generic key: value extraction for known dep keys
 */
void parser_yaml_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif /* PARSER_YAML_H */
