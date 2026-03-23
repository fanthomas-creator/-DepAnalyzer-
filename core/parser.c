

#include "parser.h"
#include "parser_py.h"
#include "parser_js.h"
#include "parser_ts.h"
#include "parser_c.h"
#include "parser_html.h"
#include "parser_css.h"
#include "parser_json.h"
#include "parser_php.h"
#include "parser_rb.h"
#include "parser_go.h"
#include "parser_rs.h"
#include "parser_java.h"
#include "parser_kt.h"
#include "parser_swift.h"
#include "parser_md.h"
#include "parser_yaml.h"
#include "parser_sh.h"
#include "parser_sql.h"
#include "parser_dart.h"
#include "parser_lua.h"
#include "parser_r.h"
#include "parser_scala.h"
#include "parser_cs.h"
#include "parser_vue.h"
#include "parser_svelte.h"
#include "parser_hs.h"
#include "parser_ex.h"
#include "parser_toml.h"
#include "parser_make.h"
#include "parser_docker.h"
#include "parser_gql.h"
#include "parser_proto.h"
#include "parser_tf.h"
#include "parser_nginx.h"
#include "parser_perl.h"
#include "parser_asm.h"
#include "parser_ocaml.h"
#include "parser_fsharp.h"
#include "parser_julia.h"
#include "parser_zig.h"
#include "parser_cr.h"
#include "parser_nim.h"
#include "parser_v.h"
#include "parser_groovy.h"
#include "parser_ps.h"
#include "parser_cmake.h"
#include "parser_bazel.h"
#include "parser_nix.h"
#include "parser_sol.h"
#include "parser_glsl.h"
#include <string.h>


static FunctionDef g_defs[MAX_FUNCTIONS];
static int         g_def_count = 0;

void parser_parse_file(FileEntry *fe) {
    switch (fe->lang) {
        case LANG_PYTHON:
            parser_py_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_JAVASCRIPT:
            parser_js_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_TYPESCRIPT:
            parser_ts_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_C:
        case LANG_CPP:
            parser_c_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_HTML:
            parser_html_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_CSS:
            parser_css_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_JSON:
            parser_json_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_PHP:
            parser_php_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_RUBY:
            parser_rb_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_GO:
            parser_go_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_RUST:
            parser_rs_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_JAVA:
            parser_java_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_KOTLIN:
            parser_kt_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_SWIFT:
            parser_swift_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_MARKDOWN:
            parser_md_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_YAML:
            parser_yaml_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_SHELL:
            parser_sh_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_SQL:
            parser_sql_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_DART:
            parser_dart_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_LUA:
            parser_lua_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_R:
            parser_r_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_SCALA:
            parser_scala_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_CSHARP:
            parser_cs_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_VUE:
            parser_vue_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_SVELTE:
            parser_svelte_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_HASKELL:
            parser_hs_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_ELIXIR:
            parser_ex_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_TOML:
            parser_toml_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_MAKE:
            parser_make_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_DOCKER:
            parser_docker_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_GRAPHQL:
            parser_gql_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_PROTO:
            parser_proto_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_TERRAFORM:
            parser_tf_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_NGINX:
            parser_nginx_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_PERL:
            parser_perl_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_ASM:
            parser_asm_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_OCAML:
            parser_ocaml_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_FSHARP:
            parser_fsharp_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_JULIA:
            parser_julia_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_ZIG:
            parser_zig_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_CRYSTAL:
            parser_cr_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_NIM:
            parser_nim_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_V:
            parser_v_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_GROOVY:
            parser_groovy_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_POWERSHELL:
            parser_ps_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_CMAKE:
            parser_cmake_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_BAZEL:
            parser_bazel_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_NIX:
            parser_nix_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_SOLIDITY:
            parser_sol_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        case LANG_GLSL:
            parser_glsl_parse(fe, g_defs, &g_def_count, MAX_FUNCTIONS);
            break;
        default:
            break;
    }
}

void parser_parse_all(FileIndex *index) {
    g_def_count = 0;
    for (int i = 0; i < index->count; i++)
        parser_parse_file(&index->files[i]);
}

void parser_build_function_index(const FileIndex *index, FunctionIndex *fi) {
    (void)index;   
    fi->count = g_def_count;
    for (int i = 0; i < g_def_count && i < MAX_FUNCTIONS; i++)
        fi->defs[i] = g_defs[i];
}
