/* Development probe for the pinned Potion model. ASCII WordPiece only;
 * deliberately rejects Unicode and special-token spellings. Not a production
 * tokenizer, and not yet certified against upstream tokenizer outputs. */
#define _POSIX_C_SOURCE 200809L
#include "cJSON.h"
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define DIM 256
#define SLOTS 65536
static const char *keys[SLOTS];
static int values[SLOTS];
static float *vectors;
static volatile float sink[DIM];
static uint32_t hash(const char *s){uint32_t h=2166136261u;while(*s)h=(h^(unsigned char)*s++)*16777619u;return h;}
static int find(const char *s){unsigned h=hash(s)%SLOTS;while(keys[h]){if(!strcmp(keys[h],s))return values[h];h=(h+1)%SLOTS;}return -1;}
static char *read_file(const char *p,size_t *n){FILE *f=fopen(p,"rb");if(!f){perror(p);exit(1);}assert(!fseek(f,0,SEEK_END));long len=ftell(f);assert(len>=0);*n=(size_t)len;rewind(f);char *s=malloc(*n+1);assert(s);assert(fread(s,1,*n,f)==*n);s[*n]=0;fclose(f);return s;}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static int tokenize(const char *s,int *ids){
    int n=0;
    while(*s){
        unsigned char c=(unsigned char)*s;
        if(c>=128||c=='['||c==']'){fprintf(stderr,"Probe supports plain ASCII only\n");exit(2);}
        if(isspace(c)){s++;continue;}
        if(iscntrl(c)){s++;continue;}
        char word[128];int len=0;
        if(ispunct(c)){word[len++]=(char)c;s++;}
        else while(*s&&!isspace((unsigned char)*s)&&!ispunct((unsigned char)*s)){
            if((unsigned char)*s>=128||len>=100){fprintf(stderr,"Unsupported probe input\n");exit(2);}
            word[len++]=(char)tolower((unsigned char)*s++);
        }
        word[len]=0;int start=0,base=n;
        while(start<len){int id=-1,end=len;char piece[132];
            for(;end>start;end--){int prefix=start?2:0;if(prefix){piece[0]='#';piece[1]='#';}memcpy(piece+prefix,word+start,(size_t)(end-start));piece[prefix+end-start]=0;id=find(piece);if(id>=0)break;}
            if(id<0){n=base;break;} /* Model2Vec removes unknown tokens. */
            if(n>=512){fprintf(stderr,"Probe token limit exceeded\n");exit(2);}
            ids[n++]=id;start=end;
        }
    }
    return n;
}
static void normalize(float *v){double sum=0;for(int d=0;d<DIM;d++)sum+=v[d]*v[d];float scale=sum>0?(float)(1/sqrt(sum)):0;for(int d=0;d<DIM;d++)v[d]*=scale;}
static void encode(const char *s,float *out,int semantic){int ids[512],n=tokenize(s,ids);memset(out,0,DIM*sizeof(float));for(int i=0;i<n;i++){if(semantic){const float *v=vectors+(size_t)ids[i]*DIM;for(int d=0;d<DIM;d++)out[d]+=v[d];}else out[(unsigned)ids[i]%DIM]+=1;}normalize(out);}
static float similarity(const float *a,const float *b){float s=0;for(int d=0;d<DIM;d++)s+=a[d]*b[d];return s;}
int main(int argc,char **argv){
    if(argc!=4){fprintf(stderr,"usage: text_probe tokenizer.json model.safetensors cases.json\n");return 2;}
    double load_start=now();size_t nt,nm,nc;char *token_data=read_file(argv[1],&nt),*model=read_file(argv[2],&nm),*case_data=read_file(argv[3],&nc);
    cJSON *tok=cJSON_Parse(token_data),*cases=cJSON_Parse(case_data);assert(tok&&cases);
    cJSON *vocab=cJSON_GetObjectItem(cJSON_GetObjectItem(tok,"model"),"vocab"),*item;
    int vocab_size=0;cJSON_ArrayForEach(item,vocab){unsigned h=hash(item->string)%SLOTS;while(keys[h])h=(h+1)%SLOTS;keys[h]=item->string;values[h]=item->valueint;vocab_size++;}
    assert(nm>=8);uint64_t header;memcpy(&header,model,8);assert(header<nm-8);
    char *json=calloc((size_t)header+1,1);assert(json);memcpy(json,model+8,(size_t)header);
    cJSON *meta=cJSON_Parse(json),*tensor=cJSON_GetObjectItem(meta,"embeddings");assert(tensor);
    assert(!strcmp(cJSON_GetObjectItem(tensor,"dtype")->valuestring,"F32"));
    cJSON *shape=cJSON_GetObjectItem(tensor,"shape"),*offsets=cJSON_GetObjectItem(tensor,"data_offsets");
    assert(cJSON_GetArrayItem(shape,0)->valueint==vocab_size&&cJSON_GetArrayItem(shape,1)->valueint==DIM);
    assert(cJSON_GetArrayItem(offsets,0)->valueint==0&&nm==8+header+(size_t)vocab_size*DIM*4);
    vectors=(float*)(model+8+header);
    int count=cJSON_GetArraySize(cases),correct[2]={0};assert(count>0&&count<=256);
    float cached[256][DIM],q[DIM],v[DIM];
    for(int i=0;i<count;i++){
        cJSON *row=cJSON_GetArrayItem(cases,i);const char *query=cJSON_GetObjectItem(row,"query")->valuestring;
        cJSON *options=cJSON_GetObjectItem(row,"options");int expected=cJSON_GetObjectItem(row,"expected")->valueint;
        for(int mode=0;mode<2;mode++){encode(query,q,mode);float best=-INFINITY;int chosen=-1;
            for(int j=0;j<cJSON_GetArraySize(options);j++){encode(cJSON_GetArrayItem(options,j)->valuestring,v,mode);float score=similarity(q,v);if(score>best){best=score;chosen=j;}}
            correct[mode]+=chosen==expected;if(mode==1){memcpy(cached[i],q,sizeof q);fprintf(stderr,"%s: %s -> %s (expected %s)\n",chosen==expected?"PASS":"MISS",query,cJSON_GetArrayItem(options,chosen)->valuestring,cJSON_GetArrayItem(options,expected)->valuestring);}
        }
    }
    double load_and_checks=now()-load_start;
    const int iterations=100000;volatile float checksum=0;double timings[3];
    for(int mode=0;mode<3;mode++){double start=now();for(int i=0;i<iterations;i++){int at=i%count;if(mode<2)encode(cJSON_GetObjectItem(cJSON_GetArrayItem(cases,at),"query")->valuestring,q,mode);else memcpy(q,cached[at],sizeof q);for(int d=0;d<DIM;d++)sink[d]=q[d];checksum+=sink[i%DIM];}timings[mode]=(now()-start)*1e6/iterations;}
    printf("{\"scope\":\"ASCII development probe; not upstream tokenizer parity or web-agent accuracy\",\"vocabulary\":%d,\"dimensions\":%d,\"weights_bytes\":%zu,\"cases\":%d,\"hashed_token_top1\":%d,\"potion_top1\":%d,\"iterations\":%d,\"hashed_token_us_per_string\":%.6f,\"potion_us_per_string\":%.6f,\"cached_vector_materialize_us\":%.6f,\"load_and_checks_seconds\":%.6f,\"checksum\":%.6f}\n",vocab_size,DIM,nm,count,correct[0],correct[1],iterations,timings[0],timings[1],timings[2],load_and_checks,(double)checksum);
    cJSON_Delete(meta);cJSON_Delete(tok);cJSON_Delete(cases);free(json);free(token_data);free(model);free(case_data);return 0;
}
