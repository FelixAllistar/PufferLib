// performance_tool only: per-frame GL draw/upload counts through raylib's
// exported glad function pointers. With skip set, draw/blit/clear calls are
// counted but not executed, so software GL does no vertex or fragment work and
// the remaining main-thread time is game CPU plus GL state/upload overhead.
// This is a measurement proxy only; it never runs in the game.
#ifndef SWAT_GL_COUNTERS_H
#define SWAT_GL_COUNTERS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32) && !defined(_WIN64)
#define SWAT_GL_CALL __stdcall
#else
#define SWAT_GL_CALL
#endif
typedef struct SwatGlCounters {
    uint64_t draws,vertices,instances,uploads,upload_bytes,texture_uploads,texture_bytes,blits,clears;
    bool skip;
} SwatGlCounters;
static SwatGlCounters swat_gl_counters;
typedef void (SWAT_GL_CALL *SwatGlDrawArrays)(unsigned int,int,int);
typedef void (SWAT_GL_CALL *SwatGlDrawElements)(unsigned int,int,unsigned int,const void*);
typedef void (SWAT_GL_CALL *SwatGlDrawArraysInstanced)(unsigned int,int,int,int);
typedef void (SWAT_GL_CALL *SwatGlDrawElementsInstanced)(unsigned int,int,unsigned int,const void*,int);
typedef void (SWAT_GL_CALL *SwatGlBufferData)(unsigned int,ptrdiff_t,const void*,unsigned int);
typedef void (SWAT_GL_CALL *SwatGlBufferSubData)(unsigned int,ptrdiff_t,ptrdiff_t,const void*);
typedef void (SWAT_GL_CALL *SwatGlTexImage2D)(unsigned int,int,int,int,int,int,unsigned int,unsigned int,const void*);
typedef void (SWAT_GL_CALL *SwatGlTexSubImage2D)(unsigned int,int,int,int,int,int,unsigned int,unsigned int,const void*);
typedef void (SWAT_GL_CALL *SwatGlBlit)(int,int,int,int,int,int,int,int,unsigned int,unsigned int);
typedef void (SWAT_GL_CALL *SwatGlClear)(unsigned int);
extern SwatGlDrawArrays glad_glDrawArrays;extern SwatGlDrawElements glad_glDrawElements;
extern SwatGlDrawArraysInstanced glad_glDrawArraysInstanced;extern SwatGlDrawElementsInstanced glad_glDrawElementsInstanced;
extern SwatGlBufferData glad_glBufferData;extern SwatGlBufferSubData glad_glBufferSubData;
extern SwatGlTexImage2D glad_glTexImage2D;extern SwatGlTexSubImage2D glad_glTexSubImage2D;
extern SwatGlBlit glad_glBlitFramebuffer;extern SwatGlClear glad_glClear;
static SwatGlDrawArrays swat_gl_draw_arrays;static SwatGlDrawElements swat_gl_draw_elements;
static SwatGlDrawArraysInstanced swat_gl_draw_arrays_instanced;static SwatGlDrawElementsInstanced swat_gl_draw_elements_instanced;
static SwatGlBufferData swat_gl_buffer_data;static SwatGlBufferSubData swat_gl_buffer_sub_data;
static SwatGlTexImage2D swat_gl_tex_image;static SwatGlTexSubImage2D swat_gl_tex_sub_image;
static SwatGlBlit swat_gl_blit;static SwatGlClear swat_gl_clear;
static void SWAT_GL_CALL swat_gl_counted_arrays(unsigned int m,int first,int count) {
    swat_gl_counters.draws++;swat_gl_counters.vertices+=(uint64_t)count;swat_gl_counters.instances++;
    if(!swat_gl_counters.skip)swat_gl_draw_arrays(m,first,count);
}
static void SWAT_GL_CALL swat_gl_counted_elements(unsigned int m,int count,unsigned int type,const void* at) {
    swat_gl_counters.draws++;swat_gl_counters.vertices+=(uint64_t)count;swat_gl_counters.instances++;
    if(!swat_gl_counters.skip)swat_gl_draw_elements(m,count,type,at);
}
static void SWAT_GL_CALL swat_gl_counted_arrays_instanced(unsigned int m,int first,int count,int n) {
    swat_gl_counters.draws++;swat_gl_counters.vertices+=(uint64_t)count*(uint64_t)n;swat_gl_counters.instances+=(uint64_t)n;
    if(!swat_gl_counters.skip)swat_gl_draw_arrays_instanced(m,first,count,n);
}
static void SWAT_GL_CALL swat_gl_counted_elements_instanced(unsigned int m,int count,unsigned int type,const void* at,int n) {
    swat_gl_counters.draws++;swat_gl_counters.vertices+=(uint64_t)count*(uint64_t)n;swat_gl_counters.instances+=(uint64_t)n;
    if(!swat_gl_counters.skip)swat_gl_draw_elements_instanced(m,count,type,at,n);
}
static void SWAT_GL_CALL swat_gl_counted_buffer(unsigned int target,ptrdiff_t size,const void* data,unsigned int usage) {
    swat_gl_counters.uploads++;if(data)swat_gl_counters.upload_bytes+=(uint64_t)size;swat_gl_buffer_data(target,size,data,usage);
}
static void SWAT_GL_CALL swat_gl_counted_sub_buffer(unsigned int target,ptrdiff_t offset,ptrdiff_t size,const void* data) {
    swat_gl_counters.uploads++;swat_gl_counters.upload_bytes+=(uint64_t)size;swat_gl_buffer_sub_data(target,offset,size,data);
}
static void SWAT_GL_CALL swat_gl_counted_texture(unsigned int t,int level,int f,int w,int h,int b,unsigned int format,unsigned int type,const void* data) {
    swat_gl_counters.texture_uploads++;if(data)swat_gl_counters.texture_bytes+=(uint64_t)w*(uint64_t)h*4;
    swat_gl_tex_image(t,level,f,w,h,b,format,type,data);
}
static void SWAT_GL_CALL swat_gl_counted_sub_texture(unsigned int t,int level,int x,int y,int w,int h,unsigned int format,unsigned int type,const void* data) {
    swat_gl_counters.texture_uploads++;swat_gl_counters.texture_bytes+=(uint64_t)w*(uint64_t)h*4;
    swat_gl_tex_sub_image(t,level,x,y,w,h,format,type,data);
}
static void SWAT_GL_CALL swat_gl_counted_blit(int a,int b,int c,int d,int e,int f,int g,int h,unsigned int mask,unsigned int filter) {
    swat_gl_counters.blits++;if(!swat_gl_counters.skip)swat_gl_blit(a,b,c,d,e,f,g,h,mask,filter);
}
static void SWAT_GL_CALL swat_gl_counted_clear(unsigned int mask) {
    swat_gl_counters.clears++;if(!swat_gl_counters.skip)swat_gl_clear(mask);
}
// Call after the window/context exists (raylib has loaded glad).
static void swat_gl_counters_install(bool skip) {
    swat_gl_counters.skip=skip;
#define SWAT_GL_WRAP(real,saved,counted) if(real && real!=counted){saved=real;real=counted;}
    SWAT_GL_WRAP(glad_glDrawArrays,swat_gl_draw_arrays,swat_gl_counted_arrays)
    SWAT_GL_WRAP(glad_glDrawElements,swat_gl_draw_elements,swat_gl_counted_elements)
    SWAT_GL_WRAP(glad_glDrawArraysInstanced,swat_gl_draw_arrays_instanced,swat_gl_counted_arrays_instanced)
    SWAT_GL_WRAP(glad_glDrawElementsInstanced,swat_gl_draw_elements_instanced,swat_gl_counted_elements_instanced)
    SWAT_GL_WRAP(glad_glBufferData,swat_gl_buffer_data,swat_gl_counted_buffer)
    SWAT_GL_WRAP(glad_glBufferSubData,swat_gl_buffer_sub_data,swat_gl_counted_sub_buffer)
    SWAT_GL_WRAP(glad_glTexImage2D,swat_gl_tex_image,swat_gl_counted_texture)
    SWAT_GL_WRAP(glad_glTexSubImage2D,swat_gl_tex_sub_image,swat_gl_counted_sub_texture)
    SWAT_GL_WRAP(glad_glBlitFramebuffer,swat_gl_blit,swat_gl_counted_blit)
    SWAT_GL_WRAP(glad_glClear,swat_gl_clear,swat_gl_counted_clear)
#undef SWAT_GL_WRAP
}
#endif
