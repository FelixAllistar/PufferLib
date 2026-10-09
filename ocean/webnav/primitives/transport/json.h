#ifndef WEBNAV_TRANSPORT_JSON_H
#define WEBNAV_TRANSPORT_JSON_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Strict JSON syntax and Unicode validation, without converting numbers or
 * allocating a tree. Limits are explicit transport presets. Strings can carry
 * escaped NUL unless the caller will hand them to a C-string-only parser.
 * Object-key uniqueness and application schemas are separate from JSON syntax. */
enum { WJ_NULL, WJ_BOOL, WJ_NUMBER, WJ_STRING, WJ_ARRAY, WJ_OBJECT };
enum { WJ_NO_NUL_STRING=1u };
typedef struct {
    const unsigned char *p,*end;
    unsigned max_depth,flags,root,array_item_types;
} WJCursor;
typedef struct { unsigned type,array_item_types; } WJInfo;

static inline void wj_space(WJCursor *j) {
    while (j->p<j->end && (*j->p==' ' || *j->p=='\t' || *j->p=='\r' || *j->p=='\n')) j->p++;
}
static inline int wj_hex4(WJCursor *j,unsigned *out) {
    unsigned value=0;
    for (unsigned i=0;i<4;i++) {
        if (j->p==j->end) return 0;
        unsigned b=*j->p++,digit;
        if (b>='0' && b<='9') digit=b-'0';
        else if (b>='a' && b<='f') digit=b-'a'+10;
        else if (b>='A' && b<='F') digit=b-'A'+10;
        else return 0;
        value=value*16+digit;
    }
    *out=value;
    return 1;
}
static inline int wj_string(WJCursor *j) {
    if (j->p==j->end || *j->p++!='"') return 0;
    while (j->p<j->end) {
        unsigned b=*j->p++;
        if (b=='"') return 1;
        if (b<32) return 0;
        if (b=='\\') {
            if (j->p==j->end) return 0;
            b=*j->p++;
            if (b=='u') {
                unsigned code;
                if (!wj_hex4(j,&code) || (!code && (j->flags&WJ_NO_NUL_STRING))) return 0;
                if (code>=0xd800 && code<=0xdbff) {
                    if (j->end-j->p<2 || j->p[0]!='\\' || j->p[1]!='u') return 0;
                    j->p+=2;
                    if (!wj_hex4(j,&code) || code<0xdc00 || code>0xdfff) return 0;
                } else if (code>=0xdc00 && code<=0xdfff) return 0;
            } else if (b!='"' && b!='\\' && b!='/' && b!='b' && b!='f' && b!='n' && b!='r' && b!='t') return 0;
        } else if (b>=128) {
            unsigned extra,code,minimum;
            if (b>=0xc2 && b<=0xdf) { extra=1;code=b&31;minimum=0x80; }
            else if (b>=0xe0 && b<=0xef) { extra=2;code=b&15;minimum=0x800; }
            else if (b>=0xf0 && b<=0xf4) { extra=3;code=b&7;minimum=0x10000; }
            else return 0;
            if ((size_t)(j->end-j->p)<extra) return 0;
            while (extra--) {
                b=*j->p++;
                if ((b&0xc0)!=0x80) return 0;
                code=(code<<6)|(b&63);
            }
            if (code<minimum || code>0x10ffff || (code>=0xd800 && code<=0xdfff)) return 0;
        }
    }
    return 0;
}
static inline int wj_digit(unsigned b) { return b>='0' && b<='9'; }
static inline int wj_number(WJCursor *j) {
    if (j->p<j->end && *j->p=='-') j->p++;
    if (j->p==j->end) return 0;
    if (*j->p=='0') j->p++;
    else {
        if (*j->p<'1' || *j->p>'9') return 0;
        do j->p++; while (j->p<j->end && wj_digit(*j->p));
    }
    if (j->p<j->end && *j->p=='.') {
        j->p++;
        if (j->p==j->end || !wj_digit(*j->p)) return 0;
        do j->p++; while (j->p<j->end && wj_digit(*j->p));
    }
    if (j->p<j->end && (*j->p=='e' || *j->p=='E')) {
        j->p++;
        if (j->p<j->end && (*j->p=='+' || *j->p=='-')) j->p++;
        if (j->p==j->end || !wj_digit(*j->p)) return 0;
        do j->p++; while (j->p<j->end && wj_digit(*j->p));
    }
    return 1;
}
static int wj_value(WJCursor *j,unsigned depth) {
    if (depth>j->max_depth) return 0;
    wj_space(j);
    if (j->p==j->end) return 0;
    unsigned b=*j->p;
    unsigned kind=b=='"'?WJ_STRING:b=='{'?WJ_OBJECT:b=='['?WJ_ARRAY:
                  b=='n'?WJ_NULL:(b=='t'||b=='f')?WJ_BOOL:WJ_NUMBER;
    if (!depth) j->root=kind;
    if (depth==1 && j->root==WJ_ARRAY) j->array_item_types|=1u<<kind;
    if (b=='"') return wj_string(j);
    if (b=='{' || b=='[') {
        unsigned close=b=='{'?'}':']';
        j->p++;wj_space(j);
        if (j->p<j->end && *j->p==close) { j->p++;return 1; }
        for (;;) {
            if (b=='{') {
                if (!wj_string(j)) return 0;
                wj_space(j);
                if (j->p==j->end || *j->p++!=':') return 0;
            }
            if (!wj_value(j,depth+1)) return 0;
            wj_space(j);
            if (j->p==j->end) return 0;
            if (*j->p==close) { j->p++;return 1; }
            if (*j->p++!=',') return 0;
            wj_space(j);
        }
    }
    const char *literal=b=='t'?"true":b=='f'?"false":b=='n'?"null":NULL;
    if (literal) {
        size_t n=strlen(literal);
        if ((size_t)(j->end-j->p)<n || memcmp(j->p,literal,n)) return 0;
        j->p+=n;
        return 1;
    }
    return wj_number(j);
}
static inline int wj_validate(const char *text,size_t bytes,unsigned max_depth,
                              unsigned flags,WJInfo *info) {
    if (!text || flags&~WJ_NO_NUL_STRING || max_depth>64) return 0;
    WJCursor j={(const unsigned char *)text,(const unsigned char *)text+bytes,
                max_depth,flags,0,0};
    if (!wj_value(&j,0)) return 0;
    wj_space(&j);
    if (j.p!=j.end) return 0;
    if (info) *info=(WJInfo){j.root,j.array_item_types};
    return 1;
}
#endif
