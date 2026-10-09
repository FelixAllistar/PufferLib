#ifndef WEBNAV_TRANSPORT_UTF8_H
#define WEBNAV_TRANSPORT_UTF8_H
#include <stddef.h>
#include <stdint.h>

/* Text editing uses Unicode scalar values. Byte limits and UTF-8 conversion
 * are transport concerns; NUL is excluded from observable C-string fields. */
static inline unsigned wt_width(uint32_t cp) {
    if(!cp||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return 0;
    return cp<0x80?1:cp<0x800?2:cp<0x10000?3:4;
}
static inline int wt_decode(const char *text,size_t bytes,uint32_t *units,size_t capacity,size_t *count) {
    if(!text)return -1;
    size_t i=0,n=0;
    while(i<bytes){
        unsigned b=(unsigned char)text[i++],cp,extra,minimum;
        if(b<0x80){cp=b;extra=0;minimum=1;}
        else if(b>=0xc2&&b<=0xdf){cp=b&31;extra=1;minimum=0x80;}
        else if(b>=0xe0&&b<=0xef){cp=b&15;extra=2;minimum=0x800;}
        else if(b>=0xf0&&b<=0xf4){cp=b&7;extra=3;minimum=0x10000;}
        else return -1;
        if(bytes-i<extra)return -1;
        while(extra--){b=(unsigned char)text[i++];if((b&0xc0)!=0x80)return -1;cp=(cp<<6)|(b&63);}
        if(cp<minimum||!wt_width(cp)||n>=capacity)return -1;
        if(units)units[n]=cp;n++;
    }
    if(count)*count=n;return 0;
}
/* out reserves capacity+1 bytes; capacity excludes the trailing NUL. */
static inline int wt_encode(const uint32_t *units,size_t count,char *out,size_t capacity,size_t *bytes) {
    if(!units)return -1;
    size_t at=0;
    for(size_t i=0;i<count;i++){
        uint32_t cp=units[i];unsigned n=wt_width(cp);
        if(!n||n>capacity-at)return -1;
        if(out){
            if(n==1)out[at]=(char)cp;
            else if(n==2){out[at]=(char)(0xc0|(cp>>6));out[at+1]=(char)(0x80|(cp&63));}
            else if(n==3){out[at]=(char)(0xe0|(cp>>12));out[at+1]=(char)(0x80|((cp>>6)&63));out[at+2]=(char)(0x80|(cp&63));}
            else {out[at]=(char)(0xf0|(cp>>18));out[at+1]=(char)(0x80|((cp>>12)&63));out[at+2]=(char)(0x80|((cp>>6)&63));out[at+3]=(char)(0x80|(cp&63));}
        }
        at+=n;
    }
    if(out)out[at]=0;if(bytes)*bytes=at;return 0;
}
static inline size_t wt_prefix(const char *text,size_t bytes,size_t limit) {
    size_t n=bytes<limit?bytes:limit;
    while(n&&n<bytes&&((unsigned char)text[n]&0xc0)==0x80)n--;
    return n;
}
#endif
