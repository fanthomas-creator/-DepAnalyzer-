
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "scanner.h"
#include "parser.h"
#include "resolver.h"
#include "analyzer.h"
#include "exporter.h"


static FileIndex     g_files;
static FunctionIndex g_functions;



static void print_usage(const char *prog) {
    fprintf(stderr,
        "depanalyzer v1.0 - Source code dependency analyzer\n"
        "Usage:\n"
        "  %s <directory> [options]\n"
        "\n"
        "Options:\n"
        "  --output <file>   Write JSON to file (default: stdout)\n"
        "  --stats           Print summary statistics to stderr\n"
        "  --help            Show this help\n"
        "\n"
        "Supported languages: Python (.py), JavaScript/TypeScript (.js .ts), C/C++ (.c .h .cpp .cc)\n"
        "\n"
        "Example:\n"
        "  %s ./my_project --output deps.json\n",
        prog, prog);
}

static void print_stats(const FileIndex *fi, const FunctionIndex *fun) {
    int total_internal = 0, total_external = 0, total_calls = 0;
    int by_lang[5] = {0};

    for (int i = 0; i < fi->count; i++) {
        total_internal += fi->files[i].internal_count;
        total_external += fi->files[i].external_count;
        total_calls    += fi->files[i].call_file_count;
        if (fi->files[i].lang < 5)
            by_lang[fi->files[i].lang]++;
    }

    fprintf(stderr, "\n=== depanalyzer stats ===\n");
    fprintf(stderr, "Files scanned       : %d\n", fi->count);
    fprintf(stderr, "  Python            : %d\n", by_lang[LANG_PYTHON]);
    fprintf(stderr, "  JavaScript/TS     : %d\n", by_lang[LANG_JAVASCRIPT]);
    fprintf(stderr, "  C/C++             : %d\n", by_lang[LANG_C] + by_lang[LANG_CPP]);
    fprintf(stderr, "Functions indexed   : %d\n", fun->count);
    fprintf(stderr, "Internal deps found : %d\n", total_internal);
    fprintf(stderr, "External deps found : %d\n", total_external);
    fprintf(stderr, "Cross-file calls    : %d\n", total_calls);
    fprintf(stderr, "=========================\n\n");
}



int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    
    const char *root_dir   = NULL;
    const char *output     = NULL;
    int         show_stats = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--output") == 0 || strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --output requires a filename\n");
                return 1;
            }
            output = argv[++i];
        } else if (strcmp(argv[i], "--stats") == 0) {
            show_stats = 1;
        } else if (argv[i][0] != '-') {
            root_dir = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!root_dir) {
        fprintf(stderr, "Error: no directory specified\n");
        print_usage(argv[0]);
        return 1;
    }

    

    fprintf(stderr, "[1/5] Scanning files in: %s\n", root_dir);
    int found = scanner_run(root_dir, &g_files);
    if (found < 0) {
        fprintf(stderr, "Error: cannot scan directory '%s'\n", root_dir);
        return 1;
    }
    fprintf(stderr, "      Found %d source file(s)\n", found);

    if (found == 0) {
        fprintf(stderr, "Warning: no supported source files found.\n");
    }

    fprintf(stderr, "[2/5] Parsing imports and function definitions...\n");
    parser_parse_all(&g_files);
    parser_build_function_index(&g_files, &g_functions);
    fprintf(stderr, "      Indexed %d function(s)\n", g_functions.count);

    fprintf(stderr, "[3/5] Resolving internal vs external dependencies...\n");
    resolver_resolve(&g_files);

    fprintf(stderr, "[4/5] Resolving cross-file function calls...\n");
    analyzer_resolve_calls(&g_files, &g_functions);

    fprintf(stderr, "[5/5] Exporting JSON...\n");
    FILE *out = stdout;
    if (output) {
        out = fopen(output, "w");
        if (!out) {
            fprintf(stderr, "Error: cannot open output file '%s'\n", output);
            return 1;
        }
    }

    exporter_write_json(&g_files, root_dir, out);

    if (output) {
        fclose(out);
        fprintf(stderr, "      Output written to: %s\n", output);
    }

    if (show_stats)
        print_stats(&g_files, &g_functions);

    fprintf(stderr, "Done.\n");
    return 0;
}
