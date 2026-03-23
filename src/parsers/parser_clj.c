/* parser_clj.c  -  Clojure dependency parser (.clj, .cljs, .cljc)
 *
 * Handles:
 *   (ns myapp.core
 *     (:require [clojure.string :as s]
 *               [myapp.utils :refer [helper]])
 *     (:import (java.util ArrayList))
 *     (:use clojure.test))
 *   (require '[clojure.string :as str])
 *   (import 'java.util.HashMap)
 *   (load "my/other/file")
 *   (defn my-func [x y] ...)
 *   (defn- private [x] ...)
 *   (def MY-CONST 42)
 *   (defmacro my-macro [x] ...)
 *   (defprotocol MyProto ...)
 *   (defrecord MyRec [f1 f2] ...)
 *   (deftype MyType [f] ...)
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_clj.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================ Helpers */

static int sw(const char *s,const char *p){return strncmp(s,p,strlen(p))==0;}
static const char *ws(const char *s){while(*s==' '||*s=='\t')s++;return s;}
static void as(char a[][MAX_SYMBOL],int *c,int m,const char *v){
    if(!v||!v[0])return;
    for(int i=0;i<*c;i++)if(!strcmp(a[i],v))return;
    if(*c<m)strncpy(a[(*c)++],v,MAX_SYMBOL-1);
}
static void ad(FunctionDef *d,int *c,int m,const char *f,const char *file){
    if(!f||!f[0])return;
    for(int i=0;i<*c;i++)if(!strcmp(d[i].function,f)&&!strcmp(d[i].file,file))return;
    if(*c<m){strncpy(d[*c].function,f,MAX_SYMBOL-1);strncpy(d[*c].file,file,MAX_NAME-1);(*c)++;}
}

/* Read a Clojure symbol/namespace: can contain letters, digits, -, _, ., /, !, ?, * */
static int read_sym(const char *s, char *o, int n) {
    int i=0;
    while(*s&&*s!=' '&&*s!='\t'&&*s!=')'&&*s!=']'&&*s!='\n'&&
          *s!=','&&*s!='{'&&*s!='}'&&*s!='"'&&i<n-1)
        o[i++]=*s++;
    o[i]=0; return i;
}

/* Get root namespace component: "clojure.string" -> "clojure"
   "myapp.models.user" -> "myapp" */
static void get_root(const char *ns, char *root, int n) {
    int i=0;
    while(*ns&&*ns!='.'&&*ns!='/'&&i<n-1)root[i++]=*ns++;
    root[i]=0;
}

/* Known Clojure/ClojureScript stdlib namespaces */
static int is_stdlib(const char *root) {
    static const char *std[]={
        "clojure","cljs","ring","compojure","hiccup","reagent","re-frame",
        "rum","om","fulcro","mount","integrant","component","pedestal",
        "reitit","muuntaja","metosin","buddy","slingshot","timbre",
        "clj-http","aleph","manifold","core.async","spec","test.check",
        "tools","leiningen","boot","deps","shadow","figwheel",
        "java","javax","org","com","net","io",NULL
    };
    for(int i=0;std[i];i++)if(!strcmp(std[i],root))return 1;
    return 0;
}

/* Store namespace as import (internal if matches project namespace heuristic) */
static void store_ns(const char *ns, FileEntry *fe) {
    if(!ns||!ns[0])return;
    /* strip leading quote ' or backtick ` */
    if(*ns=='\''||*ns=='`') ns++;
    char root[MAX_SYMBOL]={0}; get_root(ns,root,sizeof(root));
    if(!root[0])return;
    if(is_stdlib(root))
        as(fe->imports,&fe->import_count,MAX_IMPORTS,root);
    else {
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",root);
        as(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
    }
}

/* ================================================================ :require / :import parsing */

/* Parse content inside (:require [...] [...]) or (require '...) */
static void parse_require_block(const char *s, FileEntry *fe) {
    while(*s&&*s!=')') {
        s=ws(s);
        if(*s=='['||*s=='(') { s++; continue; }
        if(*s==']'||*s==')') { s++; continue; }
        if(*s==':') { /* keyword like :as :refer :only :rename */
            while(*s&&*s!=' '&&*s!='\t')s++;
            /* skip the next token (the alias/list) */
            s=ws(s);
            if(*s=='['||*s=='('){int d=1;s++;while(*s&&d>0){if(*s=='['||*s=='(')d++;if(*s==']'||*s==')')d--;s++;}}
            else { char sk[MAX_SYMBOL]={0}; s+=read_sym(s,sk,sizeof(sk)); }
            continue;
        }
        /* quote ' */
        if(*s=='\'') s++;
        char sym[MAX_SYMBOL]={0}; int l=read_sym(s,sym,sizeof(sym));
        if(l&&sym[0]&&sym[0]!='['&&(isalpha((unsigned char)sym[0])||sym[0]=='_'))store_ns(sym,fe);
        s+=l;
        if(!l)s++;
    }
}

/* Parse Java import: (java.util ArrayList) or java.util.ArrayList */
static void parse_java_import(const char *s, FileEntry *fe) {
    while(*s&&*s!=')') {
        s=ws(s);
        if(*s=='('||*s=='['||*s==']') { s++; continue; }
        if(*s==')') break;
        if(*s=='\'') s++;
        if(*s=='['||*s==']') { s++; continue; }
        char sym[MAX_SYMBOL]={0}; int l=read_sym(s,sym,sizeof(sym));
        if(l&&sym[0]&&isalpha((unsigned char)sym[0])) {
            /* "java.util" -> "java" */
            char root[MAX_SYMBOL]={0}; get_root(sym,root,sizeof(root));
            if(root[0]) as(fe->imports,&fe->import_count,MAX_IMPORTS,root);
        }
        s+=l; if(!l)s++;
    }
}

/* ================================================================ ns form parsing */

static void parse_ns(const char *line, FileEntry *fe) {
    /* (ns myapp.core (:require [...]) (:import ...)) */
    /* We process each directive keyword */
    const char *p=line;

    /* find :require */
    const char *rp;
    if((rp=strstr(p,":require"))!=NULL)
        parse_require_block(rp+8,fe);

    /* :use */
    if((rp=strstr(p,":use"))!=NULL)
        parse_require_block(rp+4,fe);

    /* :import */
    if((rp=strstr(p,":import"))!=NULL)
        parse_java_import(rp+7,fe);
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=ws(line);
    if(*p!='(') return;
    p++;

    static const char *def_kws[]={
        "defn-","defn","def","defmacro","defmulti","defmethod",
        "defprotocol","defrecord","deftype","definterface",
        "defonce","defstate","defsystem","defroutes",NULL
    };
    for(int i=0;def_kws[i];i++) {
        if(!sw(p,def_kws[i])) continue;
        int kl=(int)strlen(def_kws[i]);
        if(isalnum((unsigned char)*(p+kl))||*(p+kl)=='-') continue;
        p=ws(p+kl);
        char nm[MAX_SYMBOL]={0}; read_sym(p,nm,sizeof(nm));
        if(nm[0]) ad(defs,def_count,max_defs,nm,filename);
        return;
    }
}

/* ================================================================ load / require top-level */

static void parse_toplevel(const char *line, FileEntry *fe) {
    const char *p=ws(line);
    if(*p!='(') return; p++;

    /* (require '[clojure.string :as s]) */
    if(sw(p,"require ")) {
        parse_require_block(p+7,fe);
        return;
    }
    /* (use 'clojure.test) */
    if(sw(p,"use ")) {
        parse_require_block(p+4,fe);
        return;
    }
    /* (import 'java.util.HashMap) */
    if(sw(p,"import ")) {
        parse_java_import(p+7,fe);
        return;
    }
    /* (load "my/other/file") */
    if(sw(p,"load ")) {
        const char *q=p+5;q=ws(q);
        if(*q=='"') {
            q++;char path[MAX_PATH]={0};int i=0;
            while(*q&&*q!='"'&&i<MAX_PATH-1)path[i++]=*q++;path[i]=0;
            /* basename */
            const char *base=path;
            for(const char *c=path;*c;c++)if(*c=='/'||*c=='\\')base=c+1;
            if(base[0]){char t[MAX_SYMBOL]={0};snprintf(t,sizeof(t),"local:%s",base);as(fe->imports,&fe->import_count,MAX_IMPORTS,t);}
        }
    }
}

/* ================================================================ Public API */

void parser_clj_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r"); if(!f)return;
    char line[MAX_LINE];
    /* Accumulate ns form across multiple lines */
    static char ns_buf[MAX_LINE*32];
    int ns_depth=0, collecting_ns=0;
    ns_buf[0]=0;

    while(fgets(line,sizeof(line),f)) {
        str_trim(line);
        char *cm=strchr(line,';'); if(cm)*cm=0;
        const char *t=ws(line); if(!*t)continue;

        /* detect start of ns form */
        if(!collecting_ns && sw(t,"(ns ")) {
            collecting_ns=1; ns_depth=0; ns_buf[0]=0;
        }

        if(collecting_ns) {
            /* append line to buffer */
            int bl=(int)strlen(ns_buf);
            if(bl+MAX_LINE < (int)sizeof(ns_buf)-1) {
                strncat(ns_buf," ",sizeof(ns_buf)-bl-1);
                strncat(ns_buf,t,sizeof(ns_buf)-bl-2);
            }
            /* count parens to know when ns form ends */
            for(const char *c=t;*c;c++){if(*c=='(')ns_depth++;if(*c==')')ns_depth--;}
            if(ns_depth<=0) {
                parse_ns(ns_buf,fe);
                collecting_ns=0; ns_buf[0]=0;
            }
            continue;
        }

        parse_toplevel(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name);
    }
    fclose(f);
}
