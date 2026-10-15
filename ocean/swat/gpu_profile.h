// Asynchronous whole-frame elapsed queries, read after the measured window.
// This is GPU timeline elapsed time, including any gaps in CPU submission.
#ifndef SWAT_GPU_PROFILE_H
#define SWAT_GPU_PROFILE_H
#ifdef _WIN32
#define SWAT_QUERY_CALL __stdcall
__declspec(dllimport) void* __stdcall wglGetProcAddress(const char*);
static void* swat_gl_proc(const char* name){return wglGetProcAddress(name);}
#else
#define SWAT_QUERY_CALL
extern void* glXGetProcAddressARB(const unsigned char*);
static void* swat_gl_proc(const char* name){return glXGetProcAddressARB((const unsigned char*)name);}
#endif
typedef struct SwatGpuProfile {
    void (SWAT_QUERY_CALL *gen)(int,unsigned int*);
    void (SWAT_QUERY_CALL *begin)(unsigned int,unsigned int);
    void (SWAT_QUERY_CALL *end)(unsigned int);
    void (SWAT_QUERY_CALL *result)(unsigned int,unsigned int,uint64_t*);
    void (SWAT_QUERY_CALL *remove)(int,const unsigned int*);
    unsigned int* queries;int count;
} SwatGpuProfile;
static bool swat_gpu_profile_open(SwatGpuProfile* p,int count) {
    p->gen=swat_gl_proc("glGenQueries");p->begin=swat_gl_proc("glBeginQuery");p->end=swat_gl_proc("glEndQuery");
    p->result=swat_gl_proc("glGetQueryObjectui64v");p->remove=swat_gl_proc("glDeleteQueries");
    if(!p->gen || !p->begin || !p->end || !p->result || !p->remove)return false;
    p->queries=calloc((size_t)count,sizeof(*p->queries));if(!p->queries)return false;
    p->count=count;p->gen(count,p->queries);return true;
}
static void swat_gpu_profile_begin(SwatGpuProfile* p,int frame){if(p->queries)p->begin(0x88BF,p->queries[frame]);}
static void swat_gpu_profile_end(SwatGpuProfile* p){if(p->queries)p->end(0x88BF);}
static void swat_gpu_profile_results(SwatGpuProfile* p,double* samples,int warmup) {
    for(int i=warmup;i<p->count;i++){uint64_t ns=0;p->result(p->queries[i],0x8866,&ns);samples[i-warmup]=(double)ns*1e-9;}
}
static void swat_gpu_profile_close(SwatGpuProfile* p){if(p->queries){p->remove(p->count,p->queries);free(p->queries);p->queries=NULL;}}
#endif
