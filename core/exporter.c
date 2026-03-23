

#include "exporter.h"
#include <string.h>


static void json_str(FILE *out, const char *s) {
    fputc('"', out);
    while (*s) {
        if (*s == '"')       fputs("\\\"", out);
        else if (*s == '\\') fputs("\\\\", out);
        else if (*s == '\n') fputs("\\n",  out);
        else if (*s == '\r') fputs("\\r",  out);
        else if (*s == '\t') fputs("\\t",  out);
        else                 fputc(*s, out);
        s++;
    }
    fputc('"', out);
}


static void json_str_array(FILE *out, const char *indent,
                            const char *base, int elem_size, int count) {
    if (count == 0) { fputs("[]", out); return; }
    fputs("[\n", out);
    for (int i = 0; i < count; i++) {
        fprintf(out, "%s      ", indent);
        json_str(out, base + (size_t)i * (size_t)elem_size);
        if (i < count - 1) fputc(',', out);
        fputc('\n', out);
    }
    fprintf(out, "%s    ]", indent);
}

void exporter_write_json(const FileIndex *index, const char *root_dir, FILE *out) {
    fputs("{\n", out);
    fprintf(out, "  \"project\": ");
    json_str(out, root_dir);
    fputs(",\n", out);

    fprintf(out, "  \"file_count\": %d,\n", index->count);
    fputs("  \"files\": [\n", out);

    for (int i = 0; i < index->count; i++) {
        const FileEntry *fe = &index->files[i];
        fputs("    {\n", out);

        fprintf(out, "      \"name\": ");
        json_str(out, fe->name);
        fputs(",\n", out);

        fprintf(out, "      \"path\": ");
        json_str(out, fe->path);
        fputs(",\n", out);

        fprintf(out, "      \"language\": ");
        json_str(out, lang_name(fe->lang));
        fputs(",\n", out);

        fprintf(out, "      \"internal_dependencies\": ");
        json_str_array(out, "", fe->internal_deps[0], MAX_NAME, fe->internal_count);
        fputs(",\n", out);

        fprintf(out, "      \"external_dependencies\": ");
        json_str_array(out, "", fe->external_deps[0], MAX_SYMBOL, fe->external_count);
        fputs(",\n", out);

        fprintf(out, "      \"internal_calls\": ");
        json_str_array(out, "", fe->call_files[0], MAX_NAME, fe->call_file_count);
        fputc('\n', out);

        fputs("    }", out);
        if (i < index->count - 1) fputc(',', out);
        fputc('\n', out);
    }

    fputs("  ]\n}\n", out);
}
