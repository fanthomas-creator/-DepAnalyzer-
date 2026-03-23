/* parser_glsl.c  -  GLSL / WGSL / HLSL shader dependency parser */
#include "parser_glsl.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int sw(const char *s,const char *p){return strncmp(s,p,strlen(p))==0;}
static const char *ws(const char *s){while(*s==' '||*s=='\t')s++;return s;}
static int ri(const char *s,char *o,int n){int i=0;while(*s&&(isalnum((unsigned char)*s)||*s=='_')&&i<n-1)o[i++]=*s++;o[i]=0;return i;}
static void as(char a[][MAX_SYMBOL],int *c,int m,const char *v){if(!v||!v[0])return;for(int i=0;i<*c;i++)if(!strcmp(a[i],v))return;if(*c<m)strncpy(a[(*c)++],v,MAX_SYMBOL-1);}
static void ad(FunctionDef *d,int *c,int m,const char *f,const char *file){if(!f||!f[0])return;for(int i=0;i<*c;i++)if(!strcmp(d[i].function,f)&&!strcmp(d[i].file,file))return;if(*c<m){strncpy(d[*c].function,f,MAX_SYMBOL-1);strncpy(d[*c].file,file,MAX_NAME-1);(*c)++;}}

static void store_local(FileEntry *fe,const char *path){
    const char *base=path;
    for(const char *c=path;*c;c++)if(*c=='/'||*c=='\\')base=c+1;
    char no_ext[MAX_SYMBOL]={0};strncpy(no_ext,base,MAX_SYMBOL-1);
    char *dot=strrchr(no_ext,'.');if(dot)*dot=0;
    if(no_ext[0]){char t[MAX_SYMBOL]={0};snprintf(t,sizeof(t),"local:%s",no_ext);as(fe->imports,&fe->import_count,MAX_IMPORTS,t);}
}

/* GLSL type keywords - not function names */
static const char *GLSL_TYPES[]={
    "void","bool","int","uint","float","double",
    "vec2","vec3","vec4","bvec2","bvec3","bvec4",
    "ivec2","ivec3","ivec4","uvec2","uvec3","uvec4",
    "dvec2","dvec3","dvec4","mat2","mat3","mat4",
    "mat2x2","mat2x3","mat2x4","mat3x2","mat3x3","mat3x4",
    "mat4x2","mat4x3","mat4x4","sampler2D","sampler3D",
    "samplerCube","sampler2DShadow","sampler2DArray",
    "sampler2DMS","samplerBuffer","image2D","image3D",
    "layout","in","out","inout","uniform","attribute","varying",
    "precision","highp","mediump","lowp","main",NULL
};
static int is_glsl_type(const char *s){
    for(int i=0;GLSL_TYPES[i];i++)if(!strcmp(GLSL_TYPES[i],s))return 1;
    return 0;
}

void parser_glsl_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");if(!f)return;
    char line[MAX_LINE];int in_bc=0;

    while(fgets(line,sizeof(line),f)){
        str_trim(line);
        if(!in_bc&&strstr(line,"/*"))in_bc=1;
        if(in_bc){if(strstr(line,"*/"))in_bc=0;continue;}

        const char *t=ws(line);if(!*t)continue;

        /* #include "file.glsl" or #include <file.glsl> */
        if(sw(t,"#include")){
            const char *p=ws(t+8);
            int sys=(*p=='<');
            if(*p=='"'||*p=='<'){p++;
                char path[MAX_PATH]={0};int i=0;
                while(*p&&*p!='"'&&*p!='>'&&i<MAX_PATH-1)path[i++]=*p++;path[i]=0;
                if(path[0]){if(sys)as(fe->imports,&fe->import_count,MAX_IMPORTS,path);else store_local(fe,path);}
            }
            continue;
        }
        /* #pragma include("file.glsl") */
        if(sw(t,"#pragma include(")){
            const char *p=t+16;
            char path[MAX_PATH]={0};int i=0;
            while(*p&&*p!='"'&&*p!='\'')p++;if(*p)p++;
            while(*p&&*p!='"'&&*p!='\''&&i<MAX_PATH-1)path[i++]=*p++;path[i]=0;
            if(path[0])store_local(fe,path);
            continue;
        }

        /* strip // comments */
        char tmp[MAX_LINE];strncpy(tmp,line,MAX_LINE-1);
        char *cm=strstr(tmp,"//");if(cm)*cm=0;
        t=ws(tmp);if(!*t)continue;

        /* GLSL function: ReturnType funcName( ... */
        /* heuristic: two idents, second followed by ( */
        char first[MAX_SYMBOL]={0};int fl=ri(t,first,sizeof(first));
        if(fl&&!is_glsl_type(first)){
            /* could be a user type - skip if followed by ; or = */
            const char *a=ws(t+fl);
            if(*a==';'||*a=='=') continue;
        }
        if(fl){
            const char *a=ws(t+fl);
            /* skip type qualifiers */
            while(sw(a,"const ")||sw(a,"in ")||sw(a,"out ")||sw(a,"inout "))
                {while(*a&&*a!=' ')a++;a=ws(a);}
            char second[MAX_SYMBOL]={0};int sl=ri(a,second,sizeof(second));
            if(sl){
                const char *a2=ws(a+sl);
                if(*a2=='('){
                    const char *nm=is_glsl_type(first)?second:first;
                    if(!is_glsl_type(nm)&&nm[0])
                        ad(defs,def_count,max_defs,nm,fe->name);
                }
            }
        }

        /* uniform MyBlock / uniform sampler2D myTex */
        if(sw(t,"uniform ")){
            const char *p=ws(t+8);
            char type[MAX_SYMBOL]={0};int tl=ri(p,type,sizeof(type));p=ws(p+tl);
            char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));
            /* uniform block or named uniform */
            if(nm[0]&&!is_glsl_type(nm))as(fe->calls,&fe->call_count,MAX_CALLS,nm);
            else if(type[0]&&!is_glsl_type(type))as(fe->calls,&fe->call_count,MAX_CALLS,type);
        }

        /* WGSL: @group(0) @binding(0) var<uniform> myBuffer : MyType */
        if(sw(t,"@group")||sw(t,"@binding")){
            /* find var name */
            const char *p=strstr(t,"var");
            if(p){p+=3;while(*p&&*p!='>')p++;if(*p)p++;p=ws(p);
                char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));
                if(nm[0])as(fe->calls,&fe->call_count,MAX_CALLS,nm);}
        }

        /* HLSL: cbuffer MyBuffer : register(b0) */
        if(sw(t,"cbuffer ")||sw(t,"tbuffer ")){
            const char *p=ws(t+8);
            char nm[MAX_SYMBOL]={0};ri(p,nm,sizeof(nm));
            if(nm[0])ad(defs,def_count,max_defs,nm,fe->name);
        }
    }
    fclose(f);
}
