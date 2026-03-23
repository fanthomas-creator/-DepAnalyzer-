#ifndef PARSER_DOCKER_H
#define PARSER_DOCKER_H

#include "types.h"

/* Parse Dockerfile / Containerfile.
 * Detects:
 *   FROM nginx:1.21              → external image dep
 *   FROM ubuntu:22.04 AS builder → external + alias
 *   FROM builder AS final        → internal (multi-stage ref)
 *   COPY --from=builder /app .   → internal (multi-stage)
 *   RUN apt-get install pkg      → external package
 *   RUN pip install pkg          → external package
 *   RUN npm install pkg          → external package
 *   ADD ./local-file /dest       → internal file ref
 *   COPY ./src /app              → internal dir ref
 */
void parser_docker_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif
