/* parser_asm.c  -  Assembly dependency parser
 *
 * Handles NASM, GAS (GNU Assembler), MASM syntaxes:
 *
 * Includes:
 *   %include "macros.asm"         → internal (NASM)
 *   %include <unistd.h>           → external
 *   .include "utils.s"            → internal (GAS)
 *   INCLUDE utils.asm             → internal (MASM)
 *
 * External symbols:
 *   EXTERN printf / extern printf → external dep
 *   .extern symbol               → external (GAS)
 *   extrn symbol:PROC            → external (MASM)
 *
 * Definitions (exported labels):
 *   GLOBAL my_func / global my_func → FunctionIndex
 *   .global my_func                 → FunctionIndex (GAS)
 *   PROC my_func / PROC FAR         → FunctionIndex (MASM)
 *   my_label:                       → FunctionIndex (any label)
 *
 * Calls:
 *   call printf / call [rax]    → call
 *   bl func (ARM)               → call
 *   jmp target / b target       → call
 *   bsr / jsr (68k/AVR)         → call
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_asm.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ================================================================ Helpers */

static int sw_ci(const char *s, const char *pre) {
    while(*pre){if(tolower((unsigned char)*s)!=tolower((unsigned char)*pre))return 0;s++;pre++;}
    return 1;
}
static const char *skip_ws(const char *s) {
    while(*s==' '||*s=='\t') s++; return s;
}
static int read_asm_ident(const char *s, char *out, int out_size) {
    int i=0;
    while(*s&&(isalnum((unsigned char)*s)||*s=='_'||*s=='.'||*s=='@'||*s=='?')&&i<out_size-1)
        out[i++]=*s++;
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

/* ================================================================ Include parsing */

static void parse_include(const char *line, FileEntry *fe) {
    const char *p=line;

    /* NASM: %include "file.asm" or %include <file.h> */
    if(*p=='%') {
        p++;
        if(!sw_ci(p,"include")) return;
        p=skip_ws(p+7);
        int is_system=(*p=='<');
        if(*p=='"'||*p=='\'') {
            char q=*p++; char path[MAX_PATH]={0}; int i=0;
            while(*p&&*p!=q&&i<MAX_PATH-1) path[i++]=*p++;
            path[i]='\0';
            if(is_system) { add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,path); return; }
            /* local file */
            const char *base=path;
            for(const char *c=path;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
            char no_ext[MAX_SYMBOL]={0}; strncpy(no_ext,base,MAX_SYMBOL-1);
            char *dot=strrchr(no_ext,'.'); if(dot)*dot='\0';
            if(no_ext[0]) {
                char tagged[MAX_SYMBOL]={0};
                snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
                add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
            }
        } else if(*p=='<') {
            p++; char path[MAX_PATH]={0}; int i=0;
            while(*p&&*p!='>'&&i<MAX_PATH-1) path[i++]=*p++;
            path[i]='\0';
            /* strip extension for system includes */
            char no_ext[MAX_SYMBOL]={0}; strncpy(no_ext,path,MAX_SYMBOL-1);
            char *dot=strrchr(no_ext,'.'); if(dot)*dot='\0';
            /* keep only basename */
            const char *base=no_ext;
            for(const char *c=no_ext;*c;c++) if(*c=='/'||*c=='\\') base=(char*)c+1;
            if(base[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,base);
        }
        return;
    }

    /* GAS: .include "file.s" */
    if(*p=='.'&&sw_ci(p+1,"include")) {
        p=skip_ws(p+8);
        if(*p=='"'||*p=='\'') {
            char q=*p++; char path[MAX_PATH]={0}; int i=0;
            while(*p&&*p!=q&&i<MAX_PATH-1) path[i++]=*p++;
            path[i]='\0';
            const char *base=path;
            for(const char *c=path;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
            char no_ext[MAX_SYMBOL]={0}; strncpy(no_ext,base,MAX_SYMBOL-1);
            char *dot=strrchr(no_ext,'.'); if(dot)*dot='\0';
            if(no_ext[0]) {
                char tagged[MAX_SYMBOL]={0};
                snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
                add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
            }
        }
        return;
    }

    /* MASM: INCLUDE utils.asm */
    if(sw_ci(p,"include ")) {
        p=skip_ws(p+8);
        char path[MAX_PATH]={0}; int i=0;
        while(*p&&*p!=' '&&*p!='\t'&&*p!='\n'&&i<MAX_PATH-1) path[i++]=*p++;
        path[i]='\0';
        const char *base=path;
        for(const char *c=path;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
        char no_ext[MAX_SYMBOL]={0}; strncpy(no_ext,base,MAX_SYMBOL-1);
        char *dot=strrchr(no_ext,'.'); if(dot)*dot='\0';
        if(no_ext[0]) {
            char tagged[MAX_SYMBOL]={0};
            snprintf(tagged,sizeof(tagged),"local:%s",no_ext);
            add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
        }
    }
}

/* ================================================================ EXTERN */

static void parse_extern(const char *line, FileEntry *fe) {
    const char *p=line;

    /* NASM/MASM: EXTERN symbol / extrn symbol:PROC */
    if(sw_ci(p,"extern ")) { p=skip_ws(p+7); }
    else if(sw_ci(p,"extrn ")) { p=skip_ws(p+6); }
    else if(p[0]=='.'&&sw_ci(p+1,"extern ")) { p=skip_ws(p+8); } /* GAS */
    else return;

    char sym[MAX_SYMBOL]={0}; read_asm_ident(p,sym,sizeof(sym));
    /* strip type annotation: symbol:PROC → symbol */
    char *colon=strchr(sym,':'); if(colon)*colon='\0';
    if(sym[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,sym);
}

/* ================================================================ GLOBAL / label definitions */

static void parse_global(const char *line, FunctionDef *defs, int *def_count,
                          int max_defs, const char *filename) {
    const char *p=line;

    /* GLOBAL sym / global sym */
    if(sw_ci(p,"global ")) { p=skip_ws(p+7); }
    else if(p[0]=='.'&&sw_ci(p+1,"global ")) { p=skip_ws(p+8); } /* GAS */
    else if(sw_ci(p,"public ")) { p=skip_ws(p+7); } /* MASM */
    else return;

    char sym[MAX_SYMBOL]={0}; read_asm_ident(p,sym,sizeof(sym));
    if(sym[0]) add_def(defs,def_count,max_defs,sym,filename);
}

/* Label definition: sym: at start of line (no leading whitespace) */
static void parse_label(const char *line, FunctionDef *defs, int *def_count,
                         int max_defs, const char *filename) {
    /* Labels start at column 0 (no leading space) and end with : */
    if(line[0]==' '||line[0]=='\t'||line[0]=='.'||line[0]=='%') return;
    const char *colon=strchr(line,':');
    if(!colon||colon==line) return;
    /* make sure it's not a :: (C++ scope) */
    if(*(colon+1)==':') return;

    char nm[MAX_SYMBOL]={0};
    int l=(int)(colon-line);
    if(l<=0||l>=MAX_SYMBOL) return;
    strncpy(nm,line,l); nm[l]='\0';

    /* must be a valid identifier */
    for(int i=0;i<l;i++)
        if(!isalnum((unsigned char)nm[i])&&nm[i]!='_'&&nm[i]!='.'&&nm[i]!='@') return;

    /* skip local labels like .Lloop or @@: */
    if(nm[0]=='.'||nm[0]=='@') return;

    add_def(defs,def_count,max_defs,nm,filename);
}

/* MASM PROC definition */
static void parse_proc(const char *line, FunctionDef *defs, int *def_count,
                        int max_defs, const char *filename) {
    /* pattern: func_name PROC [FAR|NEAR] */
    const char *proc=strstr(line," PROC");
    if(!proc) proc=strstr(line," proc");
    if(!proc) return;

    char nm[MAX_SYMBOL]={0};
    int l=(int)(proc-line);
    if(l<=0||l>=MAX_SYMBOL) return;
    strncpy(nm,line,l); nm[l]='\0';
    /* trim leading whitespace */
    const char *start=skip_ws(nm);
    if(start!=nm) memmove(nm,start,strlen(start)+1);
    /* trim trailing whitespace */
    int tl=(int)strlen(nm);
    while(tl>0&&(nm[tl-1]==' '||nm[tl-1]=='\t')) nm[--tl]='\0';

    if(nm[0]) add_def(defs,def_count,max_defs,nm,filename);
}

/* ================================================================ Call parsing */

/* Instructions that transfer control */
static const char *CALL_OPS[]={
    /* x86 */
    "call","jmp","je","jne","jz","jnz","jg","jge","jl","jle",
    "ja","jae","jb","jbe","jo","jno","js","jns","jcxz","jecxz","jrcxz",
    "loop","loope","loopne","loopz","loopnz",
    /* ARM/AArch64 */
    "bl","blx","blr","b","beq","bne","bgt","bge","blt","ble","bhi","blo",
    "cbz","cbnz","tbz","tbnz","br","bx",
    /* RISC-V */
    "jal","jalr","beq","bne","blt","bge","bltu","bgeu",
    /* MIPS */
    "jal","jr","j","beq","bne","blez","bgtz","bltz","bgez",
    /* 68k/AVR */
    "bsr","jsr","rjmp","rcall",
    NULL
};

static int is_call_op(const char *s) {
    char lc[16]={0}; int i=0;
    while(s[i]&&i<15) { lc[i]=(char)tolower((unsigned char)s[i]); i++; }
    lc[i]='\0';
    for(int j=0;CALL_OPS[j];j++) if(strcmp(CALL_OPS[j],lc)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    /* Instructions start with whitespace in most ASM dialects */
    const char *p=line;
    if(*p!=' '&&*p!='\t') return; /* skip labels and directives */
    p=skip_ws(p);

    /* read opcode */
    char op[MAX_SYMBOL]={0}; int l=read_asm_ident(p,op,sizeof(op)); p+=l;
    if(!is_call_op(op)) return;

    p=skip_ws(p);
    /* skip memory operands: [rax], [rel func] */
    if(*p=='[') return;
    /* skip register names (single letter combos) */
    if(sw_ci(p,"rax")||sw_ci(p,"rbx")||sw_ci(p,"rip")||
       sw_ci(p,"eax")||sw_ci(p,"r0")||sw_ci(p,"x0")) return;

    char target[MAX_SYMBOL]={0};
    read_asm_ident(p,target,sizeof(target));
    /* skip numeric addresses */
    if(target[0]&&!isdigit((unsigned char)target[0])&&target[0]!='.'&&target[0]!='$')
        add_sym(fe->calls,&fe->call_count,MAX_CALLS,target);
}

/* ================================================================ Public API */

void parser_asm_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if(!f){fprintf(stderr,"[parser_asm] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_comment=0; /* block comments (C-style and braces) */

    while(fgets(line,sizeof(line),f)) {
        str_trim(line);

        /* strip comment markers */
        /* NASM/GAS/MASM: ; comment */
        char *cmt=strchr(line,';'); if(cmt)*cmt='\0';
        /* GAS: # comment (but not directives) */
        const char *t=skip_ws(line);
        if(*t=='#'&&*(t+1)!='!') { /* shebang lines kept */ *((char*)t)='\0'; }

        t=skip_ws(line); if(!*t) continue;
        (void)in_comment;

        parse_include(t,fe);
        parse_extern(t,fe);
        parse_global(t,defs,def_count,max_defs,fe->name);
        parse_proc(t,defs,def_count,max_defs,fe->name);
        parse_label(t,defs,def_count,max_defs,fe->name);
        parse_calls(t,fe);
    }
    fclose(f);
}
