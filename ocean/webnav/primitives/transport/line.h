#ifndef WEBNAV_JSON_LINE_H
#define WEBNAV_JSON_LINE_H
#include <stdio.h>
#include <stddef.h>
/* Bounded line framing: drain rejected lines so their suffix cannot become a
 * second request. buffer must have limit+1 bytes. EOF without newline is OK.
 * 1 valid framing, 0 EOF, -1 oversized/NUL input, -2 I/O failure. */
static int wl_read(FILE *input,char *buffer,size_t limit,size_t *bytes) {
    size_t n=0;int b,bad=0,seen=0;
    while((b=fgetc(input))!=EOF) {
        seen=1;
        if(b=='\n')break;
        if(!b)bad=1;
        if(n<limit)buffer[n++]=(char)b;
        else bad=1;
    }
    if(ferror(input))return -2;
    if(!seen)return 0;
    buffer[n]=0;*bytes=n;return bad?-1:1;
}
#endif
