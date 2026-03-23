#ifndef TYPES_H
#define TYPES_H


#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define MAX_FILES         512
#define MAX_IMPORTS       128
#define MAX_CALLS         256
#define MAX_FUNCTIONS     1024
#define MAX_PATH          512
#define MAX_NAME          256
#define MAX_SYMBOL        128
#define MAX_LINE          2048


typedef enum {
    LANG_UNKNOWN = 0,
    LANG_PYTHON,
    LANG_JAVASCRIPT,
    LANG_TYPESCRIPT,
    LANG_C,
    LANG_CPP,
    LANG_HTML,
    LANG_CSS,
    LANG_JSON,
    LANG_PHP,
    LANG_RUBY,
    LANG_GO,
    LANG_RUST,
    LANG_JAVA,
    LANG_KOTLIN,
    LANG_SWIFT,
    LANG_MARKDOWN,
    LANG_YAML,
    LANG_SHELL,
    LANG_SQL,
    LANG_DART,
    LANG_LUA,
    LANG_R,
    LANG_SCALA,
    LANG_CSHARP,
    LANG_VUE,
    LANG_SVELTE,
    LANG_HASKELL,
    LANG_ELIXIR,
    LANG_TOML,
    LANG_MAKE,
    LANG_DOCKER,
    LANG_GRAPHQL,
    LANG_PROTO,
    LANG_TERRAFORM,
    LANG_NGINX,
    LANG_PERL,
    LANG_ASM,
    LANG_OCAML,
    LANG_FSHARP,
    LANG_JULIA,
    LANG_ZIG,
    LANG_CRYSTAL,
    LANG_NIM,
    LANG_V,
    LANG_GROOVY,
    LANG_POWERSHELL,
    LANG_CMAKE,
    LANG_BAZEL,
    LANG_NIX,
    LANG_SOLIDITY,
    LANG_GLSL,
    LANG_ERLANG,
    LANG_CLOJURE,
    LANG_COBOL,
    LANG_FORTRAN
} Language;


typedef struct {
    char name[MAX_NAME];          
    char path[MAX_PATH];          
    Language lang;

    char imports[MAX_IMPORTS][MAX_SYMBOL];   
    int  import_count;

    char calls[MAX_CALLS][MAX_SYMBOL];       
    int  call_count;


    char internal_deps[MAX_IMPORTS][MAX_NAME];
    int  internal_count;

    char external_deps[MAX_IMPORTS][MAX_SYMBOL];
    int  external_count;

    char call_files[MAX_CALLS][MAX_NAME];    
    int  call_file_count;
} FileEntry;


typedef struct {
    FileEntry files[MAX_FILES];
    int       count;
} FileIndex;


typedef struct {
    char function[MAX_SYMBOL];
    char file[MAX_NAME];
} FunctionDef;

typedef struct {
    FunctionDef defs[MAX_FUNCTIONS];
    int         count;
} FunctionIndex;


static inline void str_trim(char *s) {
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' ||
                        s[len-1] == ' '  || s[len-1] == '\t')) {
        s[--len] = '\0';
    }
}


static inline const char *path_basename(const char *path) {
    const char *s = path + strlen(path);
    while (s > path && *(s-1) != '/' && *(s-1) != '\\') s--;
    return s;
}


static inline void strip_ext(const char *name, char *out, int out_size) {
    strncpy(out, name, out_size - 1);
    out[out_size - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}


static inline Language detect_lang(const char *filename) {
    const char *dot = strrchr(filename, '.');

    if (strcmp(filename, "Makefile")     == 0) return LANG_MAKE;
    if (strcmp(filename, "makefile")     == 0) return LANG_MAKE;
    if (strcmp(filename, "GNUmakefile")  == 0) return LANG_MAKE;
    if (strcmp(filename, "Dockerfile")   == 0) return LANG_DOCKER;
    if (strcmp(filename, "Containerfile")   == 0) return LANG_DOCKER;
    if (strcmp(filename, "Jenkinsfile")      == 0) return LANG_GROOVY;
    if (strcmp(filename, "CMakeLists.txt")   == 0) return LANG_CMAKE;
    if (strcmp(filename, "BUILD")            == 0) return LANG_BAZEL;
    if (strcmp(filename, "WORKSPACE")        == 0) return LANG_BAZEL;
    if (strcmp(filename, "BUILD.bazel")      == 0) return LANG_BAZEL;
    if (strcmp(filename, "WORKSPACE.bazel")  == 0) return LANG_BAZEL;
    if (!dot) return LANG_UNKNOWN;
    if (strcmp(dot, ".py")    == 0) return LANG_PYTHON;
    if (strcmp(dot, ".js")    == 0) return LANG_JAVASCRIPT;
    if (strcmp(dot, ".jsx")   == 0) return LANG_JAVASCRIPT;
    if (strcmp(dot, ".ts")    == 0) return LANG_TYPESCRIPT;
    if (strcmp(dot, ".tsx")   == 0) return LANG_TYPESCRIPT;
    if (strcmp(dot, ".c")     == 0) return LANG_C;
    if (strcmp(dot, ".h")     == 0) return LANG_C;
    if (strcmp(dot, ".cpp")   == 0) return LANG_CPP;
    if (strcmp(dot, ".cc")    == 0) return LANG_CPP;
    if (strcmp(dot, ".cxx")   == 0) return LANG_CPP;
    if (strcmp(dot, ".html")  == 0) return LANG_HTML;
    if (strcmp(dot, ".htm")   == 0) return LANG_HTML;
    if (strcmp(dot, ".css")   == 0) return LANG_CSS;
    if (strcmp(dot, ".json")  == 0) return LANG_JSON;
    if (strcmp(dot, ".php")   == 0) return LANG_PHP;
    if (strcmp(dot, ".rb")    == 0) return LANG_RUBY;
    if (strcmp(dot, ".go")    == 0) return LANG_GO;
    if (strcmp(dot, ".rs")    == 0) return LANG_RUST;
    if (strcmp(dot, ".java")  == 0) return LANG_JAVA;
    if (strcmp(dot, ".kt")    == 0) return LANG_KOTLIN;
    if (strcmp(dot, ".kts")   == 0) return LANG_KOTLIN;
    if (strcmp(dot, ".swift") == 0) return LANG_SWIFT;
    if (strcmp(dot, ".md")    == 0) return LANG_MARKDOWN;
    if (strcmp(dot, ".mdx")   == 0) return LANG_MARKDOWN;
    if (strcmp(dot, ".rst")   == 0) return LANG_MARKDOWN;
    if (strcmp(dot, ".yml")   == 0) return LANG_YAML;
    if (strcmp(dot, ".yaml")  == 0) return LANG_YAML;
    if (strcmp(dot, ".sh")    == 0) return LANG_SHELL;
    if (strcmp(dot, ".bash")  == 0) return LANG_SHELL;
    if (strcmp(dot, ".zsh")   == 0) return LANG_SHELL;
    if (strcmp(dot, ".fish")  == 0) return LANG_SHELL;
    if (strcmp(dot, ".sql")   == 0) return LANG_SQL;
    if (strcmp(dot, ".psql")  == 0) return LANG_SQL;
    if (strcmp(dot, ".mysql") == 0) return LANG_SQL;
    if (strcmp(dot, ".dart")  == 0) return LANG_DART;
    if (strcmp(dot, ".lua")   == 0) return LANG_LUA;
    if (strcmp(dot, ".r")     == 0) return LANG_R;
    if (strcmp(dot, ".R")     == 0) return LANG_R;
    if (strcmp(dot, ".Rmd")   == 0) return LANG_R;
    if (strcmp(dot, ".scala") == 0) return LANG_SCALA;
    if (strcmp(dot, ".sc")     == 0) return LANG_SCALA;
    if (strcmp(dot, ".cs")     == 0) return LANG_CSHARP;
    if (strcmp(dot, ".csx")    == 0) return LANG_CSHARP;
    if (strcmp(dot, ".vue")    == 0) return LANG_VUE;
    if (strcmp(dot, ".svelte") == 0) return LANG_SVELTE;
    if (strcmp(dot, ".hs")     == 0) return LANG_HASKELL;
    if (strcmp(dot, ".lhs")    == 0) return LANG_HASKELL;
    if (strcmp(dot, ".ex")     == 0) return LANG_ELIXIR;
    if (strcmp(dot, ".exs")    == 0) return LANG_ELIXIR;
    if (strcmp(dot, ".heex")   == 0) return LANG_ELIXIR;
    if (strcmp(dot, ".toml")   == 0) return LANG_TOML;
    if (strcmp(dot, ".graphql")== 0) return LANG_GRAPHQL;
    if (strcmp(dot, ".gql")    == 0) return LANG_GRAPHQL;
    if (strcmp(dot, ".proto")  == 0) return LANG_PROTO;
    if (strcmp(dot, ".tf")     == 0) return LANG_TERRAFORM;
    if (strcmp(dot, ".hcl")    == 0) return LANG_TERRAFORM;
    if (strcmp(dot, ".conf")   == 0) return LANG_NGINX;
    if (strcmp(dot, ".mk")     == 0) return LANG_MAKE;
    if (strcmp(dot, ".dockerfile") == 0) return LANG_DOCKER;
    if (strcmp(dot, ".pl")  == 0) return LANG_PERL;
    if (strcmp(dot, ".pm")  == 0) return LANG_PERL;
    if (strcmp(dot, ".pl6") == 0) return LANG_PERL;
    if (strcmp(dot, ".asm") == 0) return LANG_ASM;
    if (strcmp(dot, ".s")   == 0) return LANG_ASM;
    if (strcmp(dot, ".S")   == 0) return LANG_ASM;
    if (strcmp(dot, ".nasm")== 0) return LANG_ASM;
    if (strcmp(dot, ".ml")  == 0) return LANG_OCAML;
    if (strcmp(dot, ".mli") == 0) return LANG_OCAML;
    if (strcmp(dot, ".fs")  == 0) return LANG_FSHARP;
    if (strcmp(dot, ".fsx") == 0) return LANG_FSHARP;
    if (strcmp(dot, ".fsi")    == 0) return LANG_FSHARP;
    if (strcmp(dot, ".jl")     == 0) return LANG_JULIA;
    if (strcmp(dot, ".zig")    == 0) return LANG_ZIG;
    if (strcmp(dot, ".cr")     == 0) return LANG_CRYSTAL;
    if (strcmp(dot, ".nim")    == 0) return LANG_NIM;
    if (strcmp(dot, ".nims")   == 0) return LANG_NIM;
    if (strcmp(dot, ".nimble") == 0) return LANG_NIM;
    if (strcmp(dot, ".v")      == 0) return LANG_V;
    if (strcmp(dot, ".vv")     == 0) return LANG_V;
    if (strcmp(dot, ".groovy") == 0) return LANG_GROOVY;
    if (strcmp(dot, ".gradle") == 0) return LANG_GROOVY;
    if (strcmp(dot, ".gvy")    == 0) return LANG_GROOVY;
    if (strcmp(dot, ".ps1")    == 0) return LANG_POWERSHELL;
    if (strcmp(dot, ".psm1")   == 0) return LANG_POWERSHELL;
    if (strcmp(dot, ".psd1")   == 0) return LANG_POWERSHELL;
    if (strcmp(dot, ".cmake")  == 0) return LANG_CMAKE;
    if (strcmp(dot, ".nix")    == 0) return LANG_NIX;
    if (strcmp(dot, ".sol")    == 0) return LANG_SOLIDITY;
    if (strcmp(dot, ".glsl")   == 0) return LANG_GLSL;
    if (strcmp(dot, ".vert")   == 0) return LANG_GLSL;
    if (strcmp(dot, ".frag")   == 0) return LANG_GLSL;
    if (strcmp(dot, ".comp")   == 0) return LANG_GLSL;
    if (strcmp(dot, ".geom")   == 0) return LANG_GLSL;
    if (strcmp(dot, ".tesc")   == 0) return LANG_GLSL;
    if (strcmp(dot, ".tese")   == 0) return LANG_GLSL;
    if (strcmp(dot, ".wgsl")   == 0) return LANG_GLSL;
    if (strcmp(dot, ".hlsl")   == 0) return LANG_GLSL;
    if (strcmp(dot, ".metal")  == 0) return LANG_GLSL;
    if (strcmp(dot, ".erl")    == 0) return LANG_ERLANG;
    if (strcmp(dot, ".hrl")    == 0) return LANG_ERLANG;
    if (strcmp(dot, ".clj")    == 0) return LANG_CLOJURE;
    if (strcmp(dot, ".cljs")   == 0) return LANG_CLOJURE;
    if (strcmp(dot, ".cljc")   == 0) return LANG_CLOJURE;
    if (strcmp(dot, ".edn")    == 0) return LANG_CLOJURE;
    if (strcmp(dot, ".cob")    == 0) return LANG_COBOL;
    if (strcmp(dot, ".cbl")    == 0) return LANG_COBOL;
    if (strcmp(dot, ".cpy")    == 0) return LANG_COBOL;
    if (strcmp(dot, ".cobol")  == 0) return LANG_COBOL;
    if (strcmp(dot, ".pco")    == 0) return LANG_COBOL;
    if (strcmp(dot, ".f90")    == 0) return LANG_FORTRAN;
    if (strcmp(dot, ".f95")    == 0) return LANG_FORTRAN;
    if (strcmp(dot, ".f03")    == 0) return LANG_FORTRAN;
    if (strcmp(dot, ".f08")    == 0) return LANG_FORTRAN;
    if (strcmp(dot, ".f18")    == 0) return LANG_FORTRAN;
    if (strcmp(dot, ".f")      == 0) return LANG_FORTRAN;
    if (strcmp(dot, ".for")    == 0) return LANG_FORTRAN;
    if (strcmp(dot, ".f77")    == 0) return LANG_FORTRAN;
    if (strcmp(dot, ".fpp")    == 0) return LANG_FORTRAN;
    return LANG_UNKNOWN;
}

static inline const char *lang_name(Language l) {
    switch (l) {
        case LANG_PYTHON:     return "python";
        case LANG_JAVASCRIPT: return "javascript";
        case LANG_TYPESCRIPT: return "typescript";
        case LANG_C:          return "c";
        case LANG_CPP:        return "cpp";
        case LANG_HTML:       return "html";
        case LANG_CSS:        return "css";
        case LANG_JSON:       return "json";
        case LANG_PHP:        return "php";
        case LANG_RUBY:       return "ruby";
        case LANG_GO:         return "go";
        case LANG_RUST:       return "rust";
        case LANG_JAVA:       return "java";
        case LANG_KOTLIN:     return "kotlin";
        case LANG_SWIFT:      return "swift";
        case LANG_MARKDOWN:   return "markdown";
        case LANG_YAML:       return "yaml";
        case LANG_SHELL:      return "shell";
        case LANG_SQL:        return "sql";
        case LANG_DART:       return "dart";
        case LANG_LUA:        return "lua";
        case LANG_R:          return "r";
        case LANG_SCALA:      return "scala";
        case LANG_CSHARP:     return "csharp";
        case LANG_VUE:        return "vue";
        case LANG_SVELTE:     return "svelte";
        case LANG_HASKELL:    return "haskell";
        case LANG_ELIXIR:     return "elixir";
        case LANG_TOML:       return "toml";
        case LANG_MAKE:       return "makefile";
        case LANG_DOCKER:     return "dockerfile";
        case LANG_GRAPHQL:    return "graphql";
        case LANG_PROTO:      return "proto";
        case LANG_TERRAFORM:  return "terraform";
        case LANG_NGINX:      return "nginx";
        case LANG_PERL:       return "perl";
        case LANG_ASM:        return "assembly";
        case LANG_OCAML:      return "ocaml";
        case LANG_FSHARP:     return "fsharp";
        case LANG_JULIA:      return "julia";
        case LANG_ZIG:        return "zig";
        case LANG_CRYSTAL:    return "crystal";
        case LANG_NIM:        return "nim";
        case LANG_V:          return "v";
        case LANG_GROOVY:     return "groovy";
        case LANG_POWERSHELL: return "powershell";
        case LANG_CMAKE:      return "cmake";
        case LANG_BAZEL:      return "bazel";
        case LANG_NIX:        return "nix";
        case LANG_SOLIDITY:   return "solidity";
        case LANG_GLSL:       return "glsl";
        case LANG_ERLANG:     return "erlang";
        case LANG_CLOJURE:    return "clojure";
        case LANG_COBOL:      return "cobol";
        case LANG_FORTRAN:    return "fortran";
        default:              return "unknown";
    }
}

#endif 
