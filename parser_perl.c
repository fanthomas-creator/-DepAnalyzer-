/* parser_perl.c  -  Perl dependency parser
 *
 * Handles:
 *   use Moose;                        → external dep "Moose"
 *   use DBI;                          → external dep
 *   use MyApp::Models::User;          → root "MyApp" (local candidate)
 *   use parent 'MyApp::Base';         → dep
 *   use base qw(MyApp::Controller);   → dep
 *   use POSIX qw(floor ceil);         → external
 *   require './lib/utils.pl';         → internal
 *   require MyApp::Config;            → dep
 *   do './config.pl';                 → internal
 *   sub my_func { }                   → FunctionIndex
 *   package MyApp::Controller;        → namespace
 *   $obj->method()                    → call
 *   ClassName->static_method()        → call
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_perl.h"
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
    /* Perl idents can include :: */
    while(*s&&(isalnum((unsigned char)*s)||*s=='_'||(*s==':'&&*(s+1)==':'))&&i<out_size-1) {
        out[i++]=*s++;
        if(*(s-1)==':') { out[i++]=*s++; } /* consume second : */
    }
    out[i]='\0'; return i;
}
static void add_sym(char arr[][MAX_SYMBOL], int *count, int max, const char *val) {
    if(!val||!val[0]) return;
    for(int i=0;i<*count;i++) if(strcmp(arr[i],val)==0) return;
    if(*count<max) strncpy(arr[(*count)++],val,MAX_SYMBOL-1);
}
static void add_def(FunctionDef *defs, int *count, int max,
                    const char *func, const char *file) {
    if(!func||!func[0]) return;
    for(int i=0;i<*count;i++)
        if(strcmp(defs[i].function,func)==0&&strcmp(defs[i].file,file)==0) return;
    if(*count<max) {
        strncpy(defs[*count].function,func,MAX_SYMBOL-1);
        strncpy(defs[*count].file,file,MAX_NAME-1);
        (*count)++;
    }
}

/* Known Perl core/pragma modules to skip */
static int is_pragma(const char *m) {
    static const char *p[]={
        "strict","warnings","utf8","feature","vars","constant","Carp",
        "Exporter","base","parent","overload","POSIX","Scalar::Util",
        "List::Util","Data::Dumper","File::Basename","File::Path",
        "File::Spec","IO::File","MIME::Base64","Encode","Fcntl",
        "Getopt::Long","Pod::Usage","Storable","Sys::Hostname",NULL
    };
    for(int i=0;p[i];i++) if(strcmp(p[i],m)==0) return 1;
    return 0;
}

/* Get root namespace: "MyApp::Models::User" → "MyApp" */
static void get_root_ns(const char *s, char *out, int out_size) {
    int i=0;
    while(*s&&!(*s==':'&&*(s+1)==':')&&i<out_size-1) out[i++]=*s++;
    out[i]='\0';
}

/* Extract quoted or bareword module from a line after "use"/"require" */
static int extract_module(const char *s, char *out, int out_size) {
    const char *p=skip_ws(s);
    /* quoted: 'Module' or "Module" */
    if(*p=='\''||*p=='"') {
        char q=*p++; int i=0;
        while(*p&&*p!=q&&i<out_size-1) out[i++]=*p++;
        out[i]='\0'; return i;
    }
    /* bareword */
    return read_ident(p,out,out_size);
}

/* ================================================================ use / require */

static void parse_use(const char *line, FileEntry *fe) {
    const char *p=line;
    if(!starts_with(p,"use ")) return;
    p=skip_ws(p+4);

    char mod[MAX_SYMBOL]={0};
    if(!extract_module(p,mod,sizeof(mod))||!mod[0]) return;

    /* skip version numbers: use 5.010; */
    if(isdigit((unsigned char)mod[0])) return;

    /* parent/base: use parent 'MyApp::Base' */
    if(strcmp(mod,"parent")==0||strcmp(mod,"base")==0) {
        p=skip_ws(p+strlen(mod));
        /* skip qw( or quoted */
        if(starts_with(p,"qw(")) p+=3;
        char dep[MAX_SYMBOL]={0};
        extract_module(p,dep,sizeof(dep));
        if(dep[0]) {
            char root[MAX_SYMBOL]={0}; get_root_ns(dep,root,sizeof(root));
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,root[0]?root:dep);
        }
        return;
    }

    if(is_pragma(mod)) return;

    char root[MAX_SYMBOL]={0}; get_root_ns(mod,root,sizeof(root));
    add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,root[0]?root:mod);
}

static void parse_require(const char *line, FileEntry *fe) {
    const char *p=line;
    int is_do=0;

    if(starts_with(p,"require "))    p=skip_ws(p+8);
    else if(starts_with(p,"do "))  { p=skip_ws(p+3); is_do=1; }
    else return;

    char mod[MAX_SYMBOL]={0};
    extract_module(p,mod,sizeof(mod));
    if(!mod[0]) return;

    /* file path → internal */
    if(mod[0]=='.'||mod[0]=='/'||strchr(mod,'/')) {
        const char *base=mod;
        for(const char *c=mod;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
        char no_ext[MAX_SYMBOL]={0};
        strncpy(no_ext,base,MAX_SYMBOL-1);
        char *dot=strrchr(no_ext,'.'); if(dot)*dot='\0';
        if(no_ext[0]) {
            char tagged[MAX_SYMBOL]={0};
            snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
        }
        return;
    }

    if(is_pragma(mod)) return;
    char root[MAX_SYMBOL]={0}; get_root_ns(mod,root,sizeof(root));
    add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,root[0]?root:mod);
    (void)is_do;
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* sub my_func { or sub my_func : lvalue { */
    if(starts_with(p,"sub ")) {
        p=skip_ws(p+4);
        char nm[MAX_SYMBOL]={0}; int l=read_ident(p,nm,sizeof(nm));
        if(l&&nm[0]) add_def(defs,def_count,max_defs,nm,filename);
    }

    /* my $sub = sub { ... }; → anonymous, skip */
    /* Moose/Moo methods: has 'attr' => ... */
    if(starts_with(p,"has '")) {
        p+=5;
        char nm[MAX_SYMBOL]={0}; int i=0;
        while(*p&&*p!='\''&&i<MAX_SYMBOL-1) nm[i++]=*p++;
        nm[i]='\0';
        if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
    }
}

/* ================================================================ Call parsing */

static const char *PERL_KW[]={
    "if","else","elsif","unless","while","until","for","foreach","do",
    "last","next","redo","return","local","my","our","use","require",
    "package","sub","print","say","warn","die","eval","ref","defined",
    "undef","wantarray","caller","shift","unshift","push","pop","splice",
    "scalar","keys","values","each","delete","exists","grep","map","sort",
    "reverse","join","split","chomp","chop","chdir","mkdir","opendir",
    "readdir","closedir","open","close","read","write","seek","tell",
    "sprintf","printf","length","substr","index","rindex","lc","uc",
    "lcfirst","ucfirst","abs","int","chr","ord","hex","oct","rand","srand",NULL
};
static int is_kw(const char *s) {
    for(int i=0;PERL_KW[i];i++) if(strcmp(PERL_KW[i],s)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while(*p) {
        /* $obj->method() */
        if(*p=='$'||*p=='@'||*p=='%') {
            p++;
            char var[MAX_SYMBOL]={0}; p+=read_ident(p,var,sizeof(var));
            if(starts_with(p,"->")) {
                p+=2;
                char method[MAX_SYMBOL]={0}; read_ident(p,method,sizeof(method));
                if(method[0]&&!is_kw(method)) add_sym(fe->calls,&fe->call_count,MAX_CALLS,method);
            }
            continue;
        }
        /* ClassName->method() */
        if(isupper((unsigned char)*p)) {
            char cls[MAX_SYMBOL]={0}; int l=read_ident(p,cls,sizeof(cls)); p+=l;
            if(starts_with(p,"->")) {
                p+=2;
                char method[MAX_SYMBOL]={0}; read_ident(p,method,sizeof(method));
                if(!is_kw(cls)) add_sym(fe->calls,&fe->call_count,MAX_CALLS,cls);
                if(method[0]&&!is_kw(method)) add_sym(fe->calls,&fe->call_count,MAX_CALLS,method);
                continue;
            }
            /* function call: Foo() */
            if(*p=='('&&!is_kw(cls)) add_sym(fe->calls,&fe->call_count,MAX_CALLS,cls);
        } else {
            p++;
        }
    }
}

/* ================================================================ Public API */

void parser_perl_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if(!f){fprintf(stderr,"[parser_perl] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_pod=0; /* Plain Old Documentation blocks */

    while(fgets(line,sizeof(line),f)) {
        str_trim(line);

        /* POD documentation blocks: =pod ... =cut */
        if(line[0]=='='&&starts_with(line,"=pod")) { in_pod=1; continue; }
        if(line[0]=='='&&starts_with(line,"=cut")) { in_pod=0; continue; }
        if(in_pod) continue;

        /* strip # comments */
        char *cmt=strchr(line,'#');
        while(cmt) {
            int in_str=0; char sc=0;
            for(const char *c=line;c<cmt;c++){
                if(!in_str&&(*c=='\''||*c=='"')){in_str=1;sc=*c;}
                else if(in_str&&*c==sc) in_str=0;
            }
            if(!in_str){*cmt='\0';break;}
            cmt=strchr(cmt+1,'#');
        }

        const char *t=skip_ws(line); if(!*t) continue;

        parse_use(t,fe);
        parse_require(t,fe);
        parse_def(t,defs,def_count,max_defs,fe->name);
        parse_calls(t,fe);
    }
    fclose(f);
}
