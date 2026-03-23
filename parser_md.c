/* parser_md.c  -  Markdown dependency parser
 *
 * Handles:
 *   [text](./relative/file.md)    → internal link  (local:file)
 *   [text](../up/file.md)         → internal link
 *   [text](https://example.com)   → external dep   (domain)
 *   ![alt](./img.png)             → ignored (asset, not code dep)
 *   ```python                     → code fence language note (stored as call)
 *   # Heading                     → structural call (section title)
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_md.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================ Helpers */

static int starts_with(const char *s, const char *pre) {
    return strncmp(s, pre, strlen(pre)) == 0;
}
static const char *skip_ws(const char *s) {
    while (*s==' '||*s=='\t') s++; return s;
}
static void add_import(FileEntry *fe, const char *val) {
    if (!val||!val[0]) return;
    for (int i=0;i<fe->import_count;i++) if(strcmp(fe->imports[i],val)==0) return;
    if (fe->import_count < MAX_IMPORTS)
        strncpy(fe->imports[fe->import_count++], val, MAX_SYMBOL-1);
}
static void add_call(FileEntry *fe, const char *val) {
    if (!val||!val[0]) return;
    for (int i=0;i<fe->call_count;i++) if(strcmp(fe->calls[i],val)==0) return;
    if (fe->call_count < MAX_CALLS)
        strncpy(fe->calls[fe->call_count++], val, MAX_SYMBOL-1);
}

/* Extract domain from URL: "https://github.com/foo/bar" → "github.com" */
static void extract_domain(const char *url, char *out, int out_size) {
    const char *p = url;
    /* skip scheme: https:// or http:// */
    if (starts_with(p,"https://")) p+=8;
    else if (starts_with(p,"http://"))  p+=7;
    else { out[0]='\0'; return; }
    int i=0;
    while (*p && *p!='/' && *p!='?' && *p!='#' && i<out_size-1)
        out[i++]=*p++;
    out[i]='\0';
}

/* Extract target from markdown link: [text](TARGET) */
static void parse_link(const char *line, FileEntry *fe, int is_image) {
    const char *p = line;
    while (*p) {
        /* find ![ or [ */
        if (*p=='!' && *(p+1)=='[') {
            if (!is_image) { p++; continue; } /* skip images if not processing them */
            p+=2;
        } else if (*p=='[') {
            p++;
        } else { p++; continue; }

        /* skip label text */
        int depth=1;
        while (*p && depth>0) {
            if (*p=='[') depth++;
            else if (*p==']') depth--;
            p++;
        }
        /* expect ( */
        if (*p!='(') continue;
        p++;

        /* read URL until ) or space */
        char url[MAX_PATH]={0}; int i=0;
        while (*p && *p!=')' && *p!=' ' && *p!='\t' && i<MAX_PATH-1)
            url[i++]=*p++;
        url[i]='\0';
        if (!url[0]) continue;

        /* classify */
        if (starts_with(url,"http://") || starts_with(url,"https://")) {
            /* external */
            char domain[MAX_SYMBOL]={0};
            extract_domain(url, domain, sizeof(domain));
            if (domain[0]) add_import(fe, domain);
        } else if (url[0]=='.' || (!starts_with(url,"#") && strchr(url,'.'))) {
            /* relative link → internal */
            /* get basename without extension */
            const char *base = url;
            for (const char *c=url;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
            char no_ext[MAX_SYMBOL]={0};
            strncpy(no_ext, base, MAX_SYMBOL-1);
            char *dot=strrchr(no_ext,'.');
            /* keep .md/.html etc — strip them */
            if (dot) *dot='\0';
            if (no_ext[0] && no_ext[0]!='#') {
                char tagged[MAX_SYMBOL]={0};
                snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
                add_import(fe, tagged);
            }
        }
    }
}

/* Extract heading text: "## My Section" → "My Section" */
static void parse_heading(const char *line, FileEntry *fe) {
    const char *p=line;
    while (*p=='#') p++;
    p=skip_ws(p);
    if (!*p) return;
    /* store first 4 words as heading identifier */
    char heading[MAX_SYMBOL]={0}; int i=0;
    while (*p && i<MAX_SYMBOL-1) heading[i++]=*p++;
    heading[i]='\0';
    /* trim trailing spaces */
    while (i>0 && (heading[i-1]==' '||heading[i-1]=='\t')) heading[--i]='\0';
    if (heading[0]) add_call(fe, heading);
}

/* ================================================================ Public API */

void parser_md_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    (void)defs; (void)def_count; (void)max_defs;

    FILE *f = fopen(fe->path,"r");
    if (!f) { fprintf(stderr,"[parser_md] Cannot open: %s\n",fe->path); return; }

    char line[MAX_LINE];
    int in_code_fence=0;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);
        const char *t=line;

        /* code fence toggle ```lang */
        if (starts_with(t,"```")) {
            if (!in_code_fence) {
                in_code_fence=1;
                /* record language tag */
                const char *lang=t+3;
                while (*lang==' '||*lang=='\t') lang++;
                char lname[MAX_SYMBOL]={0}; int i=0;
                while (*lang&&!isspace((unsigned char)*lang)&&i<MAX_SYMBOL-1)
                    lname[i++]=*lang++;
                lname[i]='\0';
                if (lname[0]) add_call(fe, lname);
            } else {
                in_code_fence=0;
            }
            continue;
        }
        if (in_code_fence) continue;

        /* heading */
        if (*t=='#') { parse_heading(t,fe); continue; }

        /* links */
        parse_link(t, fe, 0);
    }
    fclose(f);
}
