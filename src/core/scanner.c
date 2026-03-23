

#include "scanner.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#ifdef _WIN32
#  include <windows.h>
#  define PATH_SEP '\\'
#else
#  include <dirent.h>
#  include <sys/stat.h>
#  define PATH_SEP '/'
#endif


static void join_path(const char *dir, const char *name, char *out, int out_size) {
    int dir_len = (int)strlen(dir);

    while (dir_len > 1 && (dir[dir_len-1] == '/' || dir[dir_len-1] == '\\'))
        dir_len--;
    snprintf(out, out_size, "%.*s%c%s", dir_len, dir, PATH_SEP, name);
}

static int is_supported(const char *name) {
    return detect_lang(name) != LANG_UNKNOWN;
}

static int should_skip_dir(const char *name) {

    if (name[0] == '.') return 1;
    if (strcmp(name, "node_modules") == 0) return 1;
    if (strcmp(name, "__pycache__")  == 0) return 1;
    if (strcmp(name, "venv")         == 0) return 1;
    if (strcmp(name, ".venv")        == 0) return 1;
    if (strcmp(name, "build")        == 0) return 1;
    if (strcmp(name, "dist")         == 0) return 1;
    if (strcmp(name, "target")       == 0) return 1;
    if (strcmp(name, ".git")         == 0) return 1;
    return 0;
}


#ifndef _WIN32

static int scan_dir(const char *dir, FileIndex *index, int depth) {
    if (depth > 16) return 0;          

    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "[scanner] Cannot open directory: %s\n", dir);
        return 0;
    }

    struct dirent *ent;
    int added = 0;

    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char full[MAX_PATH];
        join_path(dir, ent->d_name, full, sizeof(full));

        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            if (!should_skip_dir(ent->d_name))
                added += scan_dir(full, index, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            if (is_supported(ent->d_name) && index->count < MAX_FILES) {
                FileEntry *fe = &index->files[index->count++];
                memset(fe, 0, sizeof(*fe));
                strncpy(fe->name, ent->d_name, MAX_NAME - 1);
                strncpy(fe->path, full,        MAX_PATH - 1);
                fe->lang = detect_lang(ent->d_name);
                added++;
            }
        }
    }
    closedir(d);
    return added;
}


#else

static int scan_dir(const char *dir, FileIndex *index, int depth) {
    if (depth > 16) return 0;

    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[scanner] Cannot open directory: %s\n", dir);
        return 0;
    }

    int added = 0;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;

        char full[MAX_PATH];
        join_path(dir, fd.cFileName, full, sizeof(full));

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!should_skip_dir(fd.cFileName))
                added += scan_dir(full, index, depth + 1);
        } else {
            if (is_supported(fd.cFileName) && index->count < MAX_FILES) {
                FileEntry *fe = &index->files[index->count++];
                memset(fe, 0, sizeof(*fe));
                strncpy(fe->name, fd.cFileName, MAX_NAME - 1);
                strncpy(fe->path, full,          MAX_PATH - 1);
                fe->lang = detect_lang(fd.cFileName);
                added++;
            }
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    return added;
}

#endif  


static void normalize_path(const char *path, char *out, int out_size) {

    int i = 0;
    const char *p = path;
    while (*p && i < out_size - 1) {
#ifdef _WIN32
        out[i++] = (*p == '/') ? '\\' : *p;
#else
        out[i++] = (*p == '\\') ? '/' : *p;
#endif
        p++;
    }
    out[i] = '\0';


    for (;;) {
        int len = (int)strlen(out);
        if (out[0] != '.') break;

        int seps = 0;
        while (out[1 + seps] == '\\' || out[1 + seps] == '/') seps++;
        if (seps == 0) break;               
        int skip = 1 + seps;
        if (out[skip] == '\0') break;       
        if (out[skip + 1] == ':') break;    
        memmove(out, out + skip, (size_t)(len - skip + 1));
    }
}



int scanner_run(const char *root_dir, FileIndex *index) {
    if (!root_dir || !index) return -1;
    memset(index, 0, sizeof(*index));


    char norm[MAX_PATH];
    normalize_path(root_dir, norm, sizeof(norm));

    return scan_dir(norm, index, 0);
}
