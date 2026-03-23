/* parser_svelte.c  -  Svelte SFC dependency parser
 *
 * Svelte files have the same section structure as Vue:
 *   <script [context="module"]> ... </script>
 *   markup (template)
 *   <style [global]> ... </style>
 *
 * <script> parsing:
 *   import / require → same as JS parser
 *   export let / export const → prop declarations
 *   $: reactive statement → ignored (runtime)
 *
 * Template parsing:
 *   <ComponentName />      → PascalCase → component call
 *   <svelte:component>     → dynamic component
 *   <svelte:head>          → svelte built-in (ignored)
 *
 * <style> parsing:
 *   @import → CSS dep
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_svelte.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================ Helpers */

static int starts_with(const char *s, const char *pre) {
    return strncmp(s,pre,strlen(pre))==0;
}
static const char *skip_ws(const char *s) {
    while(*s==' '||*s=='\t') s++; return s;
}
static void add_sym(char arr[][MAX_SYMBOL], int *count, int max, const char *val) {
    if (!val||!val[0]) return;
    for (int i=0;i<*count;i++) if(strcmp(arr[i],val)==0) return;
    if (*count<max) strncpy(arr[(*count)++],val,MAX_SYMBOL-1);
}

typedef enum { SEC_NONE, SEC_SCRIPT, SEC_STYLE, SEC_TEMPLATE } Section;

/* ================================================================ Module extraction */

static void extract_module(const char *s, char *out, int out_size) {
    while (*s&&*s!='"'&&*s!='\'') s++;
    if (!*s){out[0]='\0';return;}
    char q=*s++; int i=0;
    /* relative: skip leading ./..  */
    if (*s=='.') { while(*s=='.'||*s=='/'||*s=='\\') s++; }
    while(*s&&*s!=q&&*s!='/'&&*s!='\\'&&i<out_size-1) out[i++]=*s++;
    out[i]='\0';
    /* strip extension */
    char *dot=strrchr(out,'.');
    if (dot&&(strcmp(dot,".svelte")==0||strcmp(dot,".js")==0||
              strcmp(dot,".ts")==0||strcmp(dot,".css")==0)) *dot='\0';
}

/* Full path extraction for relative imports (to tag as local:) */
static void extract_full_module(const char *line, char *out, int out_size, int *is_rel) {
    const char *s=line;
    while (*s&&*s!='"'&&*s!='\'') s++;
    if (!*s){out[0]='\0';return;}
    char q=*s++;
    *is_rel=(*s=='.');
    if (*is_rel) { while(*s=='.'||*s=='/'||*s=='\\') s++; }
    /* get just the base filename */
    const char *base=s;
    const char *c=s;
    while(*c&&*c!=q){if(*c=='/'||*c=='\\') base=c+1; c++;}
    int i=0;
    while(*base&&*base!=q&&*base!='.'&&i<out_size-1) out[i++]=*base++;
    out[i]='\0';
}

/* ================================================================ Script section */

static void parse_script_line(const char *line, FileEntry *fe) {
    const char *t=skip_ws(line);

    if (starts_with(t,"import ")) {
        /* check if relative */
        int is_rel=0;
        char mod[MAX_SYMBOL]={0};
        extract_full_module(t,mod,sizeof(mod),&is_rel);
        if (mod[0]) {
            if (is_rel) {
                char tagged[MAX_SYMBOL]={0};
                snprintf(tagged,sizeof(tagged),"local:%s",mod);
                add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
            } else {
                /* for package imports like 'svelte/store' → root "svelte" */
                char root[MAX_SYMBOL]={0}; int i=0;
                while(mod[i]&&mod[i]!='/'&&i<MAX_SYMBOL-1) root[i]=mod[i++];
                root[i]='\0';
                add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,root[0]?root:mod);
            }
        }
    }

    const char *rp=strstr(t,"require(");
    if (rp) {
        char mod[MAX_SYMBOL]={0};
        extract_module(rp+7,mod,sizeof(mod));
        if (mod[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,mod);
    }

    /* Svelte stores: writable, readable, derived → calls */
    const char *sv_api[]={"writable","readable","derived","get","set","update",
                           "onMount","onDestroy","beforeUpdate","afterUpdate",
                           "setContext","getContext","createEventDispatcher",
                           "tick","tweened","spring",NULL};
    for (int i=0;sv_api[i];i++) {
        if (strstr(t,sv_api[i]))
            add_sym(fe->calls,&fe->call_count,MAX_CALLS,sv_api[i]);
    }
}

/* ================================================================ Template section */

static void parse_template_line(const char *line, FileEntry *fe) {
    const char *p=line;
    while (*p) {
        if (*p!='<'){p++;continue;}
        p++;
        if (*p=='/'||*p=='!'){ p++; continue; }

        /* svelte: built-ins */
        if (starts_with(p,"svelte:")) {
            /* svelte:component, svelte:self → dynamic, skip */
            while(*p&&*p!='>'&&*p!=' ') p++;
            continue;
        }

        char tag[MAX_SYMBOL]={0}; int i=0;
        while(*p&&(isalnum((unsigned char)*p)||*p=='-'||*p=='_')&&i<MAX_SYMBOL-1)
            tag[i++]=*p++;
        tag[i]='\0';
        if (!tag[0]) continue;

        /* PascalCase → user component */
        if (isupper((unsigned char)tag[0])&&strchr(tag,'-')==NULL)
            add_sym(fe->calls,&fe->call_count,MAX_CALLS,tag);
    }
}

/* ================================================================ Style section */

static void parse_style_line(const char *line, FileEntry *fe) {
    const char *t=skip_ws(line);
    if (!starts_with(t,"@import")) return;
    t=skip_ws(t+7);
    if (starts_with(t,"url(")) t=skip_ws(t+4);
    char mod[MAX_SYMBOL]={0};
    extract_module(t,mod,sizeof(mod));
    if (mod[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,mod);
}

/* ================================================================ Public API */

void parser_svelte_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    (void)defs; (void)def_count; (void)max_defs;

    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_svelte] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    Section sec=SEC_TEMPLATE; /* Svelte: template is default (no wrapping tag needed) */
    int in_bc=0;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);

        /* section transitions */
        if (strstr(line,"<script"))   { sec=SEC_SCRIPT;   continue; }
        if (strstr(line,"</script>")) { sec=SEC_TEMPLATE;  continue; }
        if (strstr(line,"<style"))    { sec=SEC_STYLE;     continue; }
        if (strstr(line,"</style>"))  { sec=SEC_TEMPLATE;  continue; }

        if (sec==SEC_SCRIPT||sec==SEC_STYLE) {
            if (!in_bc&&strstr(line,"/*")) in_bc=1;
            if (in_bc){if(strstr(line,"*/"))in_bc=0;continue;}
            char tmp[MAX_LINE]; strncpy(tmp,line,MAX_LINE-1);
            char *cmt=strstr(tmp,"//"); if(cmt)*cmt='\0';
            if (sec==SEC_SCRIPT)     parse_script_line(tmp,fe);
            else                     parse_style_line(tmp,fe);
        } else {
            parse_template_line(line,fe);
        }
    }
    fclose(f);
}
