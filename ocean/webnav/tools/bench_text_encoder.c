#define _POSIX_C_SOURCE 200809L
#include "text_cache.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
static volatile float sink[WEB_TEXT_DIM];
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static int compare(const void *a,const void *b){double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
static char *read_file(const char *p){FILE *f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char *s=calloc((size_t)n+1,1);assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);fclose(f);return s;}
int main(int argc,char **argv){if(argc!=2)return 2;double start=now();WebTextEncoder *e=web_text_load("build/webnav/reference/potion-tokenizer.json","build/webnav/reference/potion-model.safetensors");assert(e);double load=now()-start;char *raw=read_file(argv[1]);cJSON *json=cJSON_Parse(raw);assert(cJSON_IsArray(json));int count=cJSON_GetArraySize(json);assert(count>0);const char **texts=malloc((size_t)count*sizeof *texts);assert(texts);size_t bytes=0;for(int i=0;i<count;i++){texts[i]=cJSON_GetArrayItem(json,i)->valuestring;assert(texts[i]);bytes+=strlen(texts[i]);}
 assert(!web_text_cache_write("build/webnav/corpus-text-cache.bin",e,texts,(uint32_t)count));start=now();WebTextCache *cache=web_text_cache_load("build/webnav/corpus-text-cache.bin");assert(cache);double cache_load=now()-start;
 int iterations=count*20;if(iterations<10000)iterations=10000;if(iterations>100000)iterations=100000;double *samples=malloc((size_t)iterations*sizeof *samples);assert(samples);double med[2],p95[2],mean[2];unsigned truncated=0;
 for(int mode=0;mode<2;mode++){double sum=0;for(int i=0;i<iterations;i++){const char *text=texts[((unsigned)i*7919u)%(unsigned)count];float vector[WEB_TEXT_DIM];double t=now();if(!mode){WebTextTokens tokens;assert(!web_text_encode(e,text,strlen(text),vector,&tokens));truncated+=tokens.truncated;for(int d=0;d<WEB_TEXT_DIM;d++)sink[d]=vector[d];}else{const float *v=web_text_cache_find(cache,text,strlen(text),NULL);assert(v);for(int d=0;d<WEB_TEXT_DIM;d++)sink[d]=v[d];}samples[i]=(now()-t)*1e6;sum+=samples[i];}qsort(samples,(size_t)iterations,sizeof *samples,compare);med[mode]=samples[iterations/2];p95[mode]=samples[(size_t)iterations*95/100];mean[mode]=sum/iterations;}
 struct rusage usage;getrusage(RUSAGE_SELF,&usage);printf("{\"scope\":\"single-thread native corpus benchmark; repeated warm corpus after cache construction\",\"strings\":%d,\"input_bytes\":%zu,\"iterations\":%d,\"model_load_seconds\":%.6f,\"cache_load_seconds\":%.6f,\"peak_rss_kib\":%ld,\"encode_us\":{\"mean\":%.6f,\"p50\":%.6f,\"p95\":%.6f},\"cache_hit_us\":{\"mean\":%.6f,\"p50\":%.6f,\"p95\":%.6f},\"truncated_encodes\":%u,\"page_workload_us\":[",count,bytes,iterations,load,cache_load,usage.ru_maxrss,mean[0],med[0],p95[0],mean[1],med[1],p95[1],truncated);
 int sizes[]={32,128,512};for(int k=0;k<3;k++){double elapsed[3];for(int mode=0;mode<3;mode++){double t=now();for(int rep=0;rep<100;rep++)for(int n=0;n<sizes[k];n++){const char *text=texts[(rep*sizes[k]+n)%count];float vector[WEB_TEXT_DIM];const float *v;if(mode==0||(mode==2&&n%10==0)){assert(!web_text_encode(e,text,strlen(text),vector,NULL));v=vector;}else{v=web_text_cache_find(cache,text,strlen(text),NULL);assert(v);}for(int d=0;d<WEB_TEXT_DIM;d++)sink[d]=v[d];}elapsed[mode]=(now()-t)*1e6/100;}printf("%s{\"nodes\":%d,\"all_encode\":%.6f,\"all_hit\":%.6f,\"approximately_10_percent_reencode\":%.6f}",k?",":"",sizes[k],elapsed[0],elapsed[1],elapsed[2]);}printf("]}\n");web_text_cache_free(cache);web_text_free(e);free(samples);free(texts);cJSON_Delete(json);free(raw);return 0;}
