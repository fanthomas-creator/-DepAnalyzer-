#ifndef PARSER_NGINX_H
#define PARSER_NGINX_H

#include "types.h"

/* Parse Nginx configuration files (.conf, nginx.conf, sites-available/*).
 * Detects:
 *   include /etc/nginx/conf.d/*.conf    → internal wildcard include
 *   include /etc/nginx/mime.types       → internal include
 *   include snippets/ssl.conf           → internal
 *   proxy_pass http://backend_upstream  → upstream reference (call)
 *   upstream backend { }               → FunctionIndex (upstream block)
 *   server { }                         → FunctionIndex
 *   location / { }                     → FunctionIndex
 *   fastcgi_pass unix:/var/run/php-fpm  → external dep
 *   ssl_certificate /path/to/cert.pem   → local file dep
 *   root /var/www/html                  → local dir
 */
void parser_nginx_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs);

#endif
