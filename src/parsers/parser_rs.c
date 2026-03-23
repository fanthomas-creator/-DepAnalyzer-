/* parser_rs.c  -  Rust dependency parser
 *
 * Handles:
 *   use std::io::{Read, Write}
 *   use crate::utils::helper         → internal
 *   use super::models::User          → internal
 *   extern crate serde               → external
 *   mod utils;                       → internal module file
 *   fn func_name(...) / pub fn / async fn / pub async fn
 *   struct / enum / trait / impl
 *   Type::method() / obj.method()    → calls
 *   #[derive(Debug, Serialize)]      → macro deps
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_rs.h"
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
    while (*s&&(isalnum((unsigned char)*s)||*s=='_')&&i<out_size-1) out[i++]=*s++;
    out[i]='\0'; return i;
}
static void add_sym(char arr[][MAX_SYMBOL], int *count, int max, const char *val) {
    if (!val||!val[0]) return;
    for (int i=0;i<*count;i++) if(strcmp(arr[i],val)==0) return;
    if (*count<max) strncpy(arr[(*count)++],val,MAX_SYMBOL-1);
}
static void add_def(FunctionDef *defs, int *count, int max,
                    const char *func, const char *file) {
    if (!func||!func[0]) return;
    for (int i=0;i<*count;i++)
        if(strcmp(defs[i].function,func)==0&&strcmp(defs[i].file,file)==0) return;
    if (*count<max){
        strncpy(defs[*count].function,func,MAX_SYMBOL-1);
        strncpy(defs[*count].file,file,MAX_NAME-1);
        (*count)++;
    }
}

/* ================================================================ use parsing */

/* Parse: use crate::foo::bar  or  use std::io::{Read,Write}
 * Returns root segment and whether it is internal */
static void parse_use(const char *line, FileEntry *fe) {
    const char *p=line;
    if (!starts_with(p,"use ")) return;
    p=skip_ws(p+4);

    /* determine scope */
    int is_internal=0;
    char root[MAX_SYMBOL]={0};
    int rlen=read_ident(p,root,sizeof(root));

    if (strcmp(root,"crate")==0||strcmp(root,"super")==0||strcmp(root,"self")==0)
        is_internal=1;

    /* advance past first :: */
    p+=rlen;
    if (p[0]==':'&&p[1]==':') p+=2;

    /* get the actual module/crate name (first component after scope) */
    char mod[MAX_SYMBOL]={0}; read_ident(p,mod,sizeof(mod));

    if (is_internal) {
        /* e.g. crate::utils → "local:utils"
         * crate::{foo,bar} → store each sub below */
        if (mod[0]) {
            char tagged[MAX_SYMBOL]={0};
            snprintf(tagged,sizeof(tagged),"local:%s",mod);
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
        }
    } else {
        /* external: use std::... → "std", use serde::{..} → "serde" */
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,root[0]?root:mod);
    }

    /* handle grouped: use foo::{Bar, Baz} */
    while (*p&&*p!='{') p++;
    if (*p=='{') {
        p++;
        while (*p&&*p!='}') {
            p=skip_ws(p);
            char sub[MAX_SYMBOL]={0}; int l=read_ident(p,sub,sizeof(sub));
            if (l&&strcmp(sub,"self")!=0) {
                if (is_internal) {
                    char tagged[MAX_SYMBOL]={0};
                    snprintf(tagged,sizeof(tagged),"local:%s",sub);
                    add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
                }
                /* no need to add externals individually */
            }
            p+=l;
            while (*p&&*p!=','&&*p!='}') p++;
            if (*p==',') p++;
        }
    }
}

/* ================================================================ extern crate */

static void parse_extern_crate(const char *line, FileEntry *fe) {
    /* extern crate serde; */
    const char *p=line;
    if (!starts_with(p,"extern crate ")) return;
    p=skip_ws(p+13);
    char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
    if (nm[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,nm);
}

/* ================================================================ mod declaration */

static void parse_mod(const char *line, FileEntry *fe) {
    /* mod utils;  → utils.rs is an internal file */
    const char *p=line;
    /* skip pub */
    if (starts_with(p,"pub ")) p=skip_ws(p+4);
    if (!starts_with(p,"mod ")) return;
    p=skip_ws(p+4);
    char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
    p+=strlen(nm); p=skip_ws(p);
    /* only if it's a file mod (ends with ;) not inline mod { */
    if (*p==';') {
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",nm);
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
    }
}

/* ================================================================ derive macros */

static void parse_derive(const char *line, FileEntry *fe) {
    /* #[derive(Debug, Serialize, Deserialize)] */
    const char *p=strstr(line,"derive(");
    if (!p) return;
    p+=7;
    while (*p&&*p!=')') {
        p=skip_ws(p);
        char nm[MAX_SYMBOL]={0}; int l=read_ident(p,nm,sizeof(nm));
        if (l&&nm[0]) add_sym(fe->calls,&fe->call_count,MAX_CALLS,nm);
        p+=l;
        while (*p&&*p!=','&&*p!=')') p++;
        if (*p==',') p++;
    }
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* skip visibility + async modifiers */
    const char *mods[]={"pub(crate) ","pub(super) ","pub(in ","pub ","async ","unsafe ","extern \"C\" ",NULL};
    int changed=1;
    while(changed){changed=0;
        for(int i=0;mods[i];i++){
            if(starts_with(p,mods[i])){p=skip_ws(p+strlen(mods[i]));changed=1;}
        }
    }

    /* fn func_name */
    if (starts_with(p,"fn ")) {
        p=skip_ws(p+3);
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }

    /* struct / enum / trait / type */
    const char *kws[]={"struct ","enum ","trait ","type ","impl ",NULL};
    for (int i=0;kws[i];i++) {
        if (!starts_with(p,kws[i])) continue;
        p=skip_ws(p+strlen(kws[i]));
        /* skip generic params < ... > */
        char nm[MAX_SYMBOL]={0}; int j=0;
        while (*p&&(isalnum((unsigned char)*p)||*p=='_')&&j<MAX_SYMBOL-1) nm[j++]=*p++;
        nm[j]='\0';
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        return;
    }
}

/* ================================================================ Call parsing */

static const char *RS_KW[]={
    "if","else","match","loop","while","for","in","return","break",
    "continue","let","mut","ref","move","fn","struct","enum","trait",
    "impl","type","where","use","mod","crate","super","self","Self",
    "pub","extern","static","const","unsafe","async","await","dyn",
    "true","false","Some","None","Ok","Err","Box","Vec","String",
    "Option","Result","println","eprintln","print","eprint","format",
    "vec","assert","assert_eq","assert_ne","panic","todo","unimplemented",
    "unreachable","dbg","write","writeln",NULL
};
static int is_kw(const char *s){
    for(int i=0;RS_KW[i];i++) if(strcmp(RS_KW[i],s)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while (*p) {
        if (isalpha((unsigned char)*p)||*p=='_') {
            char ident[MAX_SYMBOL]={0}; int l=read_ident(p,ident,sizeof(ident)); p+=l;
            /* :: chaining: Type::method */
            while (p[0]==':'&&p[1]==':'){
                p+=2;
                char sub[MAX_SYMBOL]={0}; int sl=read_ident(p,sub,sizeof(sub)); p+=sl;
                if (sl) strncpy(ident,sub,MAX_SYMBOL-1);
            }
            /* . chaining */
            while (*p=='.'){
                p++;
                char sub[MAX_SYMBOL]={0}; int sl=read_ident(p,sub,sizeof(sub)); p+=sl;
                if (sl) strncpy(ident,sub,MAX_SYMBOL-1);
            }
            if (*p=='('&&!is_kw(ident)&&ident[0])
                add_sym(fe->calls,&fe->call_count,MAX_CALLS,ident);
        } else p++;
    }
}

/* ================================================================ Public API */

void parser_rs_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_rs] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_block_comment=0;
    int in_string=0;   /* simplified: track raw strings r#"..."# */

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);

        /* block comments */
        if (!in_block_comment&&strstr(line,"/*")) in_block_comment=1;
        if (in_block_comment){if(strstr(line,"*/"))in_block_comment=0;continue;}

        /* strip // comment (careful: not inside strings — simplified) */
        char *cmt=strstr(line,"//"); if(cmt)*cmt='\0';

        (void)in_string;

        const char *t=skip_ws(line); if(!*t) continue;

        /* derive macros */
        if (*t=='#') { parse_derive(t,fe); continue; }

        parse_use(t,fe);
        parse_extern_crate(t,fe);
        parse_mod(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name);
        parse_calls(t,fe);
    }
    fclose(f);
}
