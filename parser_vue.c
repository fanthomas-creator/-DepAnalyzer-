/* parser_vue.c  -  Vue Single File Component parser
 *
 * Parses .vue files in 3 sections:
 *
 * <script> / <script setup>:
 *   import Foo from './Foo.vue'       → internal
 *   import { ref, computed } from 'vue' → external "vue"
 *   import axios from 'axios'         → external
 *   const emit = defineEmits(...)     → Vue macro (call)
 *   defineProps({...})                → Vue macro (call)
 *
 * <template>:
 *   <MyComponent />                   → component call
 *   <router-view> / <router-link>     → vue-router dep
 *   <el-button> (Element Plus prefix) → external dep hint
 *
 * <style>:
 *   @import './base.css'              → internal CSS dep
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_vue.h"
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
static int read_ident(const char *s, char *out, int out_size) {
    int i=0;
    while(*s&&(isalnum((unsigned char)*s)||*s=='_'||*s=='$')&&i<out_size-1)
        out[i++]=*s++;
    out[i]='\0'; return i;
}
static void add_sym(char arr[][MAX_SYMBOL], int *count, int max, const char *val) {
    if (!val||!val[0]) return;
    for (int i=0;i<*count;i++) if(strcmp(arr[i],val)==0) return;
    if (*count<max) strncpy(arr[(*count)++],val,MAX_SYMBOL-1);
}

/* ================================================================ Section tags */

typedef enum { SEC_NONE, SEC_TEMPLATE, SEC_SCRIPT, SEC_STYLE } Section;

static Section detect_section(const char *line) {
    if (strstr(line,"<template")) return SEC_TEMPLATE;
    if (strstr(line,"<script"))   return SEC_SCRIPT;
    if (strstr(line,"<style"))    return SEC_STYLE;
    return SEC_NONE;
}
static int is_end_tag(const char *line, Section sec) {
    if (sec==SEC_TEMPLATE&&strstr(line,"</template>")) return 1;
    if (sec==SEC_SCRIPT  &&strstr(line,"</script>"))   return 1;
    if (sec==SEC_STYLE   &&strstr(line,"</style>"))    return 1;
    return 0;
}

/* ================================================================ Script section */

static void extract_module(const char *s, char *out, int out_size) {
    while (*s&&*s!='"'&&*s!='\'') s++;
    if (!*s){out[0]='\0';return;}
    char q=*s++; int i=0;
    int is_rel=0;
    if (*s=='.'){is_rel=1;while(*s=='.'||*s=='/'||*s=='\\') s++;}
    while(*s&&*s!=q&&*s!='/'&&*s!='\\'&&i<out_size-1) out[i++]=*s++;
    out[i]='\0';
    char *dot=strrchr(out,'.');
    if (dot&&(strcmp(dot,".vue")==0||strcmp(dot,".js")==0||
              strcmp(dot,".ts")==0||strcmp(dot,".css")==0)) *dot='\0';
    (void)is_rel;
}

static void parse_script_line(const char *line, FileEntry *fe) {
    const char *t=skip_ws(line);

    /* import */
    if (starts_with(t,"import ")) {
        char mod[MAX_SYMBOL]={0};
        extract_module(t,mod,sizeof(mod));
        if (mod[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,mod);
    }

    /* require */
    const char *rp=strstr(t,"require(");
    if (rp) {
        char mod[MAX_SYMBOL]={0};
        extract_module(rp+7,mod,sizeof(mod));
        if (mod[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,mod);
    }

    /* Vue 3 define* macros */
    const char *macros[]={"defineProps","defineEmits","defineExpose",
                           "defineComponent","defineAsyncComponent",
                           "withDefaults","useSlots","useAttrs","useCssVars",NULL};
    for (int i=0;macros[i];i++) {
        if (strstr(t,macros[i])) {
            add_sym(fe->calls,&fe->call_count,MAX_CALLS,macros[i]);
        }
    }

    /* component registration in options API: components: { Foo, Bar } */
    if (strstr(t,"components:")) {
        const char *p=strstr(t,"components:")+11;
        p=skip_ws(p);
        if (*p=='{') {
            p++;
            while (*p&&*p!='}') {
                p=skip_ws(p);
                char nm[MAX_SYMBOL]={0}; int l=read_ident(p,nm,sizeof(nm));
                if (l&&nm[0]) add_sym(fe->calls,&fe->call_count,MAX_CALLS,nm);
                p+=l;
                while (*p&&*p!=','&&*p!='}') p++;
                if (*p==',') p++;
            }
        }
    }
}

/* ================================================================ Template section */

/* Well-known UI library prefixes that indicate an external dep */
static const char *UI_PREFIXES[]={
    "el-",    /* Element Plus */
    "v-",     /* Vuetify */
    "n-",     /* Naive UI */
    "a-",     /* Ant Design Vue */
    "q-",     /* Quasar */
    "b-",     /* Bootstrap Vue */
    NULL
};

static void parse_template_line(const char *line, FileEntry *fe) {
    const char *p=line;
    while (*p) {
        if (*p!='<') { p++; continue; }
        p++;
        if (*p=='/'||*p=='!'||*p=='?') { p++; continue; }

        char tag[MAX_SYMBOL]={0}; int i=0;
        /* read tag name including hyphens */
        while (*p&&(isalnum((unsigned char)*p)||*p=='-'||*p=='_')&&i<MAX_SYMBOL-1)
            tag[i++]=*p++;
        tag[i]='\0';
        if (!tag[0]) continue;

        /* router-view / router-link → vue-router */
        if (strcmp(tag,"router-view")==0||strcmp(tag,"router-link")==0) {
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,"vue-router");
            continue;
        }

        /* UI library prefixes */
        for (int j=0;UI_PREFIXES[j];j++) {
            int plen=(int)strlen(UI_PREFIXES[j]);
            if (starts_with(tag,UI_PREFIXES[j])&&(int)strlen(tag)>plen) {
                /* Store prefix as dep e.g. "el-button" → "element-plus" hint */
                add_sym(fe->calls,&fe->call_count,MAX_CALLS,tag);
                break;
            }
        }

        /* PascalCase component → call (it's registered somewhere) */
        if (isupper((unsigned char)tag[0])&&strchr(tag,'-')==NULL)
            add_sym(fe->calls,&fe->call_count,MAX_CALLS,tag);
    }
}

/* ================================================================ Style section */

static void parse_style_line(const char *line, FileEntry *fe) {
    const char *t=skip_ws(line);
    if (!starts_with(t,"@import")) return;
    t=skip_ws(t+7);
    /* strip url( */
    if (starts_with(t,"url(")) t=skip_ws(t+4);
    char mod[MAX_SYMBOL]={0};
    extract_module(t,mod,sizeof(mod));
    if (mod[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,mod);
}

/* ================================================================ Public API */

void parser_vue_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    (void)defs; (void)def_count; (void)max_defs;

    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_vue] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    Section sec=SEC_NONE;
    int in_bc=0;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);

        /* section detection */
        Section ns=detect_section(line);
        if (ns!=SEC_NONE) { sec=ns; continue; }
        if (sec!=SEC_NONE&&is_end_tag(line,sec)) { sec=SEC_NONE; continue; }
        if (sec==SEC_NONE) continue;

        /* block comment (inside script/style) */
        if (sec==SEC_SCRIPT||sec==SEC_STYLE) {
            if (!in_bc&&strstr(line,"/*")) in_bc=1;
            if (in_bc){if(strstr(line,"*/"))in_bc=0;continue;}
            char tmp[MAX_LINE]; strncpy(tmp,line,MAX_LINE-1);
            char *cmt=strstr(tmp,"//"); if(cmt)*cmt='\0';
            if (sec==SEC_SCRIPT)      parse_script_line(tmp,fe);
            else if (sec==SEC_STYLE)  parse_style_line(tmp,fe);
        } else if (sec==SEC_TEMPLATE) {
            parse_template_line(line,fe);
        }
    }
    fclose(f);
}
