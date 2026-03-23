/* parser_sql.c  -  SQL dependency parser
 *
 * Handles PostgreSQL, MySQL, SQLite dialects.
 *
 * File includes:
 *   \i migrations/001_init.sql      → internal (local:001_init)
 *   \ir ./schema.sql                → internal (local:schema)
 *   SOURCE ./seed.sql               → internal (MySQL)
 *   .read data.sql                  → internal (SQLite)
 *
 * Definitions (stored in FunctionIndex):
 *   CREATE TABLE users              → "users"
 *   CREATE VIEW active_users        → "active_users"
 *   CREATE [OR REPLACE] FUNCTION    → function name
 *   CREATE [OR REPLACE] PROCEDURE   → procedure name
 *   CREATE SCHEMA myschema          → "myschema"
 *
 * Cross-references (stored as calls):
 *   INSERT INTO tablename           → tablename
 *   UPDATE tablename                → tablename
 *   FROM tablename / JOIN tablename → tablename
 *   REFERENCES tablename            → tablename (FK)
 *   CREATE INDEX ON tablename       → tablename
 *   TRUNCATE tablename              → tablename
 *   DROP TABLE tablename            → tablename
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_sql.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================ Helpers */

static const char *skip_ws(const char *s) {
    while(*s==' '||*s=='\t') s++; return s;
}

/* Case-insensitive starts_with */
static int sw_ci(const char *s, const char *pre) {
    while (*pre) {
        if (tolower((unsigned char)*s)!=tolower((unsigned char)*pre)) return 0;
        s++; pre++;
    }
    return 1;
}

/* Read SQL identifier (letters, digits, underscore, dot for schema.table) */
static int read_sql_ident(const char *s, char *out, int out_size) {
    /* skip optional quotes: "table", `table`, [table] */
    int quoted=0; char close_q=0;
    if (*s=='"') { quoted=1; close_q='"'; s++; }
    else if (*s=='`') { quoted=1; close_q='`'; s++; }
    else if (*s=='[') { quoted=1; close_q=']'; s++; }

    int i=0;
    if (quoted) {
        while (*s&&*s!=close_q&&i<out_size-1) out[i++]=*s++;
    } else {
        while (*s&&(isalnum((unsigned char)*s)||*s=='_'||*s=='.')&&i<out_size-1)
            out[i++]=*s++;
    }
    out[i]='\0'; return i;
}

static void add_import(FileEntry *fe, const char *val) {
    if (!val||!val[0]) return;
    for (int i=0;i<fe->import_count;i++) if(strcmp(fe->imports[i],val)==0) return;
    if (fe->import_count<MAX_IMPORTS) strncpy(fe->imports[fe->import_count++],val,MAX_SYMBOL-1);
}
static void add_call(FileEntry *fe, const char *val) {
    if (!val||!val[0]) return;
    /* lowercase for consistency */
    char lc[MAX_SYMBOL]={0}; int i=0;
    while (val[i]&&i<MAX_SYMBOL-1){lc[i]=(char)tolower((unsigned char)val[i]);i++;}
    lc[i]='\0';
    for (int j=0;j<fe->call_count;j++) if(strcmp(fe->calls[j],lc)==0) return;
    if (fe->call_count<MAX_CALLS) strncpy(fe->calls[fe->call_count++],lc,MAX_SYMBOL-1);
}
static void add_def(FunctionDef *defs, int *count, int max,
                    const char *func, const char *file) {
    if (!func||!func[0]) return;
    char lc[MAX_SYMBOL]={0}; int i=0;
    while (func[i]&&i<MAX_SYMBOL-1){lc[i]=(char)tolower((unsigned char)func[i]);i++;}
    lc[i]='\0';
    for (int j=0;j<*count;j++)
        if(strcmp(defs[j].function,lc)==0&&strcmp(defs[j].file,file)==0) return;
    if (*count<max){
        strncpy(defs[*count].function,lc,MAX_SYMBOL-1);
        strncpy(defs[*count].file,file,MAX_NAME-1);
        (*count)++;
    }
}

/* Skip one or more SQL keywords */
static const char *skip_kw(const char *p, const char *kw) {
    const char *t=skip_ws(p);
    if (sw_ci(t,kw)) { t+=strlen(kw); return skip_ws(t); }
    return p;
}

/* ================================================================ File include */

static void parse_include(const char *line, FileEntry *fe) {
    const char *p=line;
    /* psql: \i file or \ir file */
    if (*p=='\\' && (*(p+1)=='i'||*(p+1)=='r'||
                     (*(p+1)=='i'&&*(p+2)=='r'))) {
        p++;
        if (*p=='i') p++;
        if (*p=='r') p++;
        p=skip_ws(p);
    }
    /* MySQL: SOURCE file */
    else if (sw_ci(p,"source ")) { p=skip_ws(p+7); }
    /* SQLite: .read file */
    else if (sw_ci(p,".read ")) { p=skip_ws(p+6); }
    else return;

    /* strip quotes */
    if (*p=='\''||*p=='"') p++;

    char mod[MAX_SYMBOL]={0};
    /* basename without extension */
    const char *base=p;
    for (const char *c=p;*c&&*c!='\''&&*c!='"'&&*c!=';';c++)
        if(*c=='/'||*c=='\\') base=c+1;
    int i=0;
    while (base[i]&&base[i]!='\''&&base[i]!='"'&&base[i]!='.'&&base[i]!=';'&&i<MAX_SYMBOL-1)
        mod[i++]=base[i];
    /* recopy correctly */
    i=0; const char *b=base;
    while (*b&&*b!='\''&&*b!='"'&&*b!='.'&&*b!=';'&&i<MAX_SYMBOL-1) mod[i++]=*b++;
    mod[i]='\0';
    if (mod[0]) {
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",mod);
        add_import(fe,tagged);
    }
}

/* ================================================================ CREATE parsing */

static void parse_create(const char *line, FileEntry *fe,
                          FunctionDef *defs, int *def_count, int max_defs) {
    const char *p=skip_ws(line+7); /* skip "CREATE " */

    /* OR REPLACE */
    if (sw_ci(p,"or replace ")) p=skip_ws(p+11);

    /* TEMPORARY / TEMP */
    if (sw_ci(p,"temporary ")) p=skip_ws(p+10);
    if (sw_ci(p,"temp "))      p=skip_ws(p+5);

    /* Determine object type */
    typedef struct { const char *kw; int is_def; } ObjType;
    static const ObjType types[]={
        {"table ",1},{"view ",1},{"materialized view ",1},
        {"function ",1},{"procedure ",1},{"aggregate ",1},
        {"schema ",1},{"sequence ",1},{"type ",1},
        {"index ",0},{"unique index ",0},
        {NULL,0}
    };

    for (int i=0;types[i].kw;i++) {
        if (!sw_ci(p,types[i].kw)) continue;
        p=skip_ws(p+strlen(types[i].kw));

        /* CREATE INDEX [IF NOT EXISTS] idx ON table */
        if (sw_ci(types[i].kw,"index ")||sw_ci(types[i].kw,"unique index ")) {
            /* skip optional index name */
            if (sw_ci(p,"if not exists ")) p=skip_ws(p+14);
            char idx_name[MAX_SYMBOL]={0}; int l=read_sql_ident(p,idx_name,sizeof(idx_name));
            p=skip_ws(p+l);
            if (sw_ci(p,"on ")) {
                p=skip_ws(p+3);
                char tbl[MAX_SYMBOL]={0}; read_sql_ident(p,tbl,sizeof(tbl));
                if (tbl[0]) add_call(fe,tbl);
            }
            return;
        }

        /* IF NOT EXISTS */
        if (sw_ci(p,"if not exists ")) p=skip_ws(p+14);

        char name[MAX_SYMBOL]={0}; read_sql_ident(p,name,sizeof(name));
        if (name[0] && types[i].is_def)
            add_def(defs,def_count,max_defs,name,fe->name);
        return;
    }
}

/* ================================================================ DML / reference parsing */

static void parse_table_ref(const char *line, FileEntry *fe) {
    /* Scan line for patterns: FROM tbl, JOIN tbl, INTO tbl, UPDATE tbl,
       REFERENCES tbl, TRUNCATE tbl */
    typedef struct { const char *kw; } RefKw;
    static const RefKw kws[]={
        {"from "},{"join "},{"inner join "},{"left join "},{"right join "},
        {"full join "},{"cross join "},{"into "},{"update "},
        {"references "},{"truncate "},{"truncate table "},
        {"drop table "},{"alter table "},{"lock table "},
        {NULL}
    };

    /* Work on lowercase copy */
    char lc[MAX_LINE]={0};
    for (int i=0;line[i]&&i<MAX_LINE-1;i++) lc[i]=(char)tolower((unsigned char)line[i]);

    for (int i=0;kws[i].kw;i++) {
        const char *p=lc;
        while ((p=strstr(p,kws[i].kw))!=NULL) {
            /* word boundary: char before must not be alpha */
            if (p>lc&&isalpha((unsigned char)*(p-1))) { p++; continue; }
            p+=strlen(kws[i].kw);
            p=skip_ws(p);
            /* skip IF EXISTS */
            if (sw_ci(p,"if exists ")) p=skip_ws(p+10);
            /* skip ONLY (PostgreSQL) */
            if (sw_ci(p,"only ")) p=skip_ws(p+5);
            char tbl[MAX_SYMBOL]={0};
            read_sql_ident(p,tbl,sizeof(tbl));
            if (tbl[0]&&strcmp(tbl,"select")!=0&&strcmp(tbl,"(")!=0&&
                strcmp(tbl,"*")!=0&&strcmp(tbl,"set")!=0)
                add_call(fe,tbl);
        }
    }
}

/* ================================================================ Public API */

void parser_sql_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_sql] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_block_comment=0;
    int in_string=0;
    char str_char=0;

    /* We do a multi-line accumulation for CREATE statements that span lines */
    char stmt[MAX_LINE*4]={0};
    int stmt_len=0;
    int in_stmt=0;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);

        /* block comments */
        if (!in_block_comment&&strstr(line,"/*")) in_block_comment=1;
        if (in_block_comment){if(strstr(line,"*/"))in_block_comment=0;continue;}

        /* strip -- comments */
        char *cmt=strstr(line,"--"); if(cmt)*cmt='\0';

        const char *t=skip_ws(line); if(!*t) continue;

        /* File includes */
        parse_include(t, fe);

        /* Table / object references in DML */
        parse_table_ref(t, fe);

        /* CREATE */
        if (sw_ci(t,"create ")) {
            parse_create(t, fe, defs, def_count, max_defs);
        }

        (void)in_string; (void)str_char;
        (void)stmt; (void)stmt_len; (void)in_stmt;
    }
    fclose(f);
}
