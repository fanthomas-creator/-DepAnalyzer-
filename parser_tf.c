/* parser_tf.c  -  HCL / Terraform dependency parser
 *
 * Handles:
 *   module "vpc" { source = "./modules/vpc" }       → internal
 *   module "consul" { source = "hashicorp/consul/aws" } → external "hashicorp/consul"
 *   provider "aws" { }                              → external "aws"
 *   resource "aws_instance" "web" { }               → def "aws_instance.web"
 *   data "aws_ami" "ubuntu" { }                     → def "data.aws_ami.ubuntu"
 *   variable "region" { }                           → def "region"
 *   output "instance_ip" { }                        → def "instance_ip"
 *   required_providers { aws = { source = "..." } } → external dep
 *   source = "registry.terraform.io/foo/bar"        → external
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_tf.h"
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

/* Extract quoted string */
static int extract_quoted(const char *s, char *out, int out_size) {
    while(*s&&*s!='"'&&*s!='\'') s++;
    if(!*s) return 0;
    char q=*s++; int i=0;
    while(*s&&*s!=q&&i<out_size-1) out[i++]=*s++;
    out[i]='\0'; return i;
}

/* ================================================================ Source classification */

static void handle_source(const char *val, FileEntry *fe) {
    /* local: starts with . or / */
    if(val[0]=='.'||val[0]=='/') {
        const char *base=val;
        for(const char *c=val;*c;c++) if(*c=='/'||*c=='\\') base=c+1;
        char tagged[MAX_SYMBOL]={0};
        snprintf(tagged,sizeof(tagged),"local:%s",base[0]?base:val);
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,tagged);
        return;
    }
    /* Terraform registry: hashicorp/consul/aws → "hashicorp/consul" */
    /* strip registry prefix: registry.terraform.io/foo/bar → "foo/bar" */
    const char *s=val;
    if(starts_with(s,"registry.terraform.io/")) s+=22;
    if(starts_with(s,"app.terraform.io/"))      s+=17;
    /* get first two components: vendor/module */
    char mod[MAX_SYMBOL]={0}; int i=0,slashes=0;
    while(*s&&i<MAX_SYMBOL-1) {
        if(*s=='/') { slashes++; if(slashes>=2) break; }
        mod[i++]=*s++;
    }
    mod[i]='\0';
    if(mod[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,mod);
}

/* ================================================================ Public API */

void parser_tf_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if(!f){fprintf(stderr,"[parser_tf] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_required_providers=0;
    int brace_depth=0;
    int rp_depth=0;

    while(fgets(line,sizeof(line),f)) {
        str_trim(line);
        /* strip # and // comments */
        char *cmt=strstr(line,"#");  if(cmt)*cmt='\0';
        cmt=strstr(line,"//");       if(cmt)*cmt='\0';
        const char *t=skip_ws(line); if(!*t) continue;

        /* track brace depth */
        for(const char *c=t;*c;c++) {
            if(*c=='{') brace_depth++;
            if(*c=='}') {
                brace_depth--;
                if(in_required_providers&&brace_depth<=rp_depth)
                    in_required_providers=0;
            }
        }

        /* required_providers block */
        if(strstr(t,"required_providers")) {
            in_required_providers=1;
            rp_depth=brace_depth-1;
            continue;
        }

        /* Inside required_providers: aws = { source = "..." } */
        if(in_required_providers) {
            /* provider name is the key before = */
            const char *eq=strchr(t,'=');
            if(eq&&eq>t) {
                char key[MAX_SYMBOL]={0};
                int kl=(int)(eq-t);
                if(kl<MAX_SYMBOL) {
                    strncpy(key,t,kl);
                    while(kl>0&&(key[kl-1]==' '||key[kl-1]=='\t')) key[--kl]='\0';
                    static const char *skip_keys[]={"source","version","features","optional","default-features",NULL};
                    int skip_key=0;
                    for(int ki=0;skip_keys[ki];ki++) if(strcmp(key,skip_keys[ki])==0){skip_key=1;break;}
                    if(!skip_key&&key[0]&&key[0]!='#')
                        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,key);
                }
            }
            /* source = "registry/..." */
            if(starts_with(t,"source")) {
                const char *eq2=strchr(t,'=');
                if(eq2) {
                    char src[MAX_SYMBOL]={0};
                    if(extract_quoted(eq2+1,src,sizeof(src))&&src[0])
                        handle_source(src,fe);
                }
            }
            continue;
        }

        /* Block types: resource / data / module / provider / variable / output */

        /* module "name" { → check source inside */
        if(starts_with(t,"module ")) {
            char nm[MAX_SYMBOL]={0};
            extract_quoted(t+7,nm,sizeof(nm));
            if(nm[0]) add_def(defs,def_count,max_defs,nm,fe->name);
            continue;
        }

        /* source = "./path" or source = "registry/module" */
        if(starts_with(t,"source")) {
            const char *eq=strchr(t,'=');
            if(eq) {
                char src[MAX_SYMBOL]={0};
                if(extract_quoted(eq+1,src,sizeof(src))&&src[0])
                    handle_source(src,fe);
            }
            continue;
        }

        /* provider "aws" { → external dep */
        if(starts_with(t,"provider ")) {
            char nm[MAX_SYMBOL]={0};
            extract_quoted(t+9,nm,sizeof(nm));
            if(nm[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,nm);
            continue;
        }

        /* resource "type" "name" { → def "type.name" */
        if(starts_with(t,"resource ")) {
            char rtype[MAX_SYMBOL]={0}, rname[MAX_SYMBOL]={0};
            const char *p=t+9;
            extract_quoted(p,rtype,sizeof(rtype));
            p=strchr(p,'"'); if(p) p++; /* past first quote */
            p=strchr(p,'"'); if(p) p++; /* past closing quote */
            if(p) { p=skip_ws(p); extract_quoted(p,rname,sizeof(rname)); }
            if(rtype[0]&&rname[0]) {
                char full[MAX_SYMBOL]={0};
                snprintf(full,sizeof(full),"%s.%s",rtype,rname);
                add_def(defs,def_count,max_defs,full,fe->name);
            }
            continue;
        }

        /* data "type" "name" { */
        if(starts_with(t,"data ")) {
            char dtype[MAX_SYMBOL]={0}, dname[MAX_SYMBOL]={0};
            const char *p=t+5;
            extract_quoted(p,dtype,sizeof(dtype));
            p=strchr(p,'"'); if(p) p++;
            p=strchr(p,'"'); if(p) p++;
            if(p) { p=skip_ws(p); extract_quoted(p,dname,sizeof(dname)); }
            if(dtype[0]&&dname[0]) {
                char full[MAX_SYMBOL]={0};
                snprintf(full,sizeof(full),"data.%s.%s",dtype,dname);
                add_def(defs,def_count,max_defs,full,fe->name);
            }
            continue;
        }

        /* variable "name" { or output "name" { */
        if(starts_with(t,"variable ")||starts_with(t,"output ")) {
            const char *p=starts_with(t,"variable ")?t+9:t+7;
            char nm[MAX_SYMBOL]={0};
            extract_quoted(p,nm,sizeof(nm));
            if(nm[0]) add_def(defs,def_count,max_defs,nm,fe->name);
        }
    }
    fclose(f);
}
