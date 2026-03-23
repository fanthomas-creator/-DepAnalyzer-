/* parser_swift.c  -  Swift dependency parser
 *
 * Handles:
 *   import Foundation / UIKit / SwiftUI / Combine
 *   import MyLocalModule
 *   class Foo / struct Foo / enum Foo / protocol Foo / actor Foo
 *   extension Foo / extension Foo: Bar
 *   func myFunc(...) → throws → Type
 *   static func / class func / override func / mutating func
 *   @propertyWrapper / @Observable / @State / @Binding etc.
 *   obj.method() / Type.method() / Type.self
 *
 * Zero external deps. Windows/Linux compatible.
 */

#include "parser_swift.h"
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
    /* Swift allows backtick-escaped identifiers */
    if (*s=='`'){s++;while(*s&&*s!='`'&&i<out_size-1) out[i++]=*s++;out[i]='\0';return i+2;}
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

/* ================================================================ Well-known Apple frameworks */

static const char *APPLE_FRAMEWORKS[]={
    "Foundation","UIKit","AppKit","SwiftUI","Combine","CoreData",
    "CoreLocation","CoreMotion","CoreBluetooth","CoreNFC","CoreHaptics",
    "AVFoundation","ARKit","RealityKit","SceneKit","SpriteKit","GameKit",
    "MapKit","StoreKit","CloudKit","HealthKit","HomeKit","WatchKit",
    "TVUIKit","CarPlay","CryptoKit","AuthenticationServices",
    "LocalAuthentication","Network","NetworkExtension","SystemConfiguration",
    "WebKit","SafariServices","MessageUI","EventKit","Contacts",
    "ContactsUI","Photos","PhotosUI","Vision","NaturalLanguage","CreateML",
    "CoreML","TabularData","Swift","XCTest",NULL
};
static int is_apple_fw(const char *s){
    for(int i=0;APPLE_FRAMEWORKS[i];i++) if(strcmp(APPLE_FRAMEWORKS[i],s)==0) return 1;
    return 0;
}

/* ================================================================ Import parsing */

static void parse_import(const char *line, FileEntry *fe) {
    const char *p=line;
    /* @import or import */
    if (*p=='@') p++;
    if (!starts_with(p,"import ")) return;
    p=skip_ws(p+7);
    /* skip kind modifiers: typealias / struct / class / enum / protocol / var / func */
    const char *kinds[]={"typealias ","struct ","class ","enum ","protocol ","var ","func ",NULL};
    for (int i=0;kinds[i];i++){if(starts_with(p,kinds[i])){p=skip_ws(p+strlen(kinds[i]));break;}}

    char mod[MAX_SYMBOL]={0};
    /* read dotted path first component */
    int i=0;
    while (*p&&(isalnum((unsigned char)*p)||*p=='_')&&i<MAX_SYMBOL-1) mod[i++]=*p++;
    mod[i]='\0';

    if (mod[0]) add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,mod);
}

/* ================================================================ Attribute / property wrapper */

static void parse_attribute(const char *line, FileEntry *fe) {
    const char *p=line;
    if (*p!='@') return;
    p++;
    char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));

    /* skip standard Swift attributes */
    static const char *SKIP[]={
        "available","objc","objcMembers","nonobjc","IBOutlet","IBAction",
        "IBInspectable","IBDesignable","NSManaged","NSCopying","GKInspectable",
        "discardableResult","inlinable","usableFromInline","frozen","unknown",
        "dynamicMemberLookup","dynamicCallable","propertyWrapper","resultBuilder",
        "differentiable","main","testable","UIApplicationMain","NSApplicationMain",
        "escaping","autoclosure","Sendable","MainActor","globalActor",NULL
    };
    for (int i=0;SKIP[i];i++) if(strcmp(SKIP[i],nm)==0) return;

    if (nm[0]&&isupper((unsigned char)nm[0]))
        add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,nm);
}

/* ================================================================ Definition parsing */

static void parse_def(const char *line, FileEntry *fe, FunctionDef *defs, int *def_count,
                       int max_defs, const char *filename) {
    const char *p=skip_ws(line);

    /* skip modifiers */
    const char *mods[]={
        "public ","private ","internal ","fileprivate ","open ",
        "static ","class ","override ","final ","weak ","lazy ",
        "mutating ","nonmutating ","dynamic ","required ","convenience ",
        "indirect ","nonisolated ","isolated ","async ","throws ","rethrows ",NULL
    };
    int changed=1;
    while(changed){changed=0;
        for(int i=0;mods[i];i++){
            if(starts_with(p,mods[i])){p=skip_ws(p+strlen(mods[i]));changed=1;}
        }
    }

    /* type declarations */
    const char *kws[]={"class ","struct ","enum ","protocol ","actor ","extension ","typealias ",NULL};
    for (int i=0;kws[i];i++){
        if (!starts_with(p,kws[i])) continue;
        p=skip_ws(p+strlen(kws[i]));
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
        /* extension Foo: Bar → Bar is a protocol conformance */
        p+=strlen(nm); p=skip_ws(p);
        if (*p==':'){
            p=skip_ws(p+1);
            char proto[MAX_SYMBOL]={0}; read_ident(p,proto,sizeof(proto));
            if (proto[0]&&!is_apple_fw(proto))
                add_sym(fe->imports,&fe->import_count,MAX_IMPORTS,proto);
        }
        return;
    }

    /* func myFunc */
    if (starts_with(p,"func ")||starts_with(p,"init(")||starts_with(p,"init?(")) {
        if (starts_with(p,"func ")) p=skip_ws(p+5);
        /* skip generic <T> */
        if (*p=='<'){while(*p&&*p!='>') p++;if(*p) p++;p=skip_ws(p);}
        char nm[MAX_SYMBOL]={0}; read_ident(p,nm,sizeof(nm));
        if (nm[0]) add_def(defs,def_count,max_defs,nm,filename);
    }
}

/* ================================================================ Call parsing */

static const char *SWIFT_KW[]={
    "if","else","guard","for","in","while","repeat","switch","case",
    "default","return","break","continue","fallthrough","throw","do","try",
    "catch","defer","import","let","var","func","class","struct","enum",
    "protocol","extension","actor","typealias","associatedtype","operator",
    "subscript","init","deinit","where","as","is","in","nil","true","false",
    "self","Self","super","some","any","async","await","throws","rethrows",
    "inout","mutating","nonmutating","static","dynamic","override","final",
    "public","private","internal","fileprivate","open","weak","unowned","lazy",
    "willSet","didSet","get","set","print","debugPrint","fatalError","precondition",
    "assert","assertionFailure","preconditionFailure",NULL
};
static int is_kw(const char *s){
    for(int i=0;SWIFT_KW[i];i++) if(strcmp(SWIFT_KW[i],s)==0) return 1;
    return 0;
}

static void parse_calls(const char *line, FileEntry *fe) {
    const char *p=line;
    while (*p) {
        if (isalpha((unsigned char)*p)||*p=='_') {
            char ident[MAX_SYMBOL]={0}; int l=read_ident(p,ident,sizeof(ident)); p+=l;
            /* . chaining (including optional ?.) */
            while (*p=='.'||(*p=='?'&&*(p+1)=='.')){
                p+=(*p=='?')?2:1;
                char sub[MAX_SYMBOL]={0}; int sl=read_ident(p,sub,sizeof(sub)); p+=sl;
                if (sl) strncpy(ident,sub,MAX_SYMBOL-1);
            }
            if (*p=='('&&!is_kw(ident)&&ident[0])
                add_sym(fe->calls,&fe->call_count,MAX_CALLS,ident);
        } else p++;
    }
}

/* ================================================================ Public API */

void parser_swift_parse(FileEntry *fe, FunctionDef *defs, int *def_count, int max_defs) {
    FILE *f=fopen(fe->path,"r");
    if (!f){fprintf(stderr,"[parser_swift] Cannot open: %s\n",fe->path);return;}

    char line[MAX_LINE];
    int in_block_comment=0;

    while (fgets(line,sizeof(line),f)) {
        str_trim(line);

        if (!in_block_comment&&strstr(line,"/*")) in_block_comment=1;
        if (in_block_comment){if(strstr(line,"*/"))in_block_comment=0;continue;}

        char *cmt=strstr(line,"//"); if(cmt)*cmt='\0';

        const char *t=skip_ws(line); if(!*t) continue;

        if (*t=='@')                   parse_attribute(t,fe);
        if (starts_with(t,"import ")||
            starts_with(t,"@import ")) parse_import(t,fe);
        parse_def(t,fe,defs,def_count,max_defs,fe->name);
        parse_calls(t,fe);
    }
    fclose(f);
}
