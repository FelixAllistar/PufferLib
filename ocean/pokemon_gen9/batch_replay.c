/* Group next ready operations across independently ordered source traces.
 * This validates source-generated kernels and scheduling, not autonomous games.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

extern int pg9_kernel_slots(unsigned op);
extern void pg9_kernel(unsigned,size_t,const double *const *,double *,uint32_t *,double *);
extern const char *pg9_kernel_source_hash(void);
#define FIELDS 16
typedef struct {
    uint32_t op,context,tag;
    double before,after,result,input[FIELDS];
} Row;
typedef struct {
    uint32_t count,position,contexts;
    Row *rows;
    double *modifiers;
    unsigned char *initialized;
} Lane;
static void read_exact(FILE *f,void *p,size_t count){assert(fread(p,1,count,f)==count);}
static uint32_t read_u32(FILE *f){uint32_t n;read_exact(f,&n,4);return n;}
static double read_double(FILE *f){double n;read_exact(f,&n,8);return n;}
static int same(double a,double b){return (isnan(a)&&isnan(b))||(a==b&&(a!=0||signbit(a)==signbit(b)));}
int main(int argc,char **argv){
    assert(argc==3);int rotating=!strcmp(argv[2],"rotate");assert(rotating||!strcmp(argv[2],"largest"));
    FILE *file=fopen(argv[1],"rb");assert(file);
    assert(read_u32(file)==0x54424739u&&read_u32(file)==1);
    char digest[65]={0};read_exact(file,digest,64);assert(!strcmp(digest,pg9_kernel_source_hash()));
    uint32_t n=read_u32(file),ops=read_u32(file);assert(n>0&&n<=1024&&ops>0&&ops<=1024);
    Lane *lanes=(Lane*)calloc(n,sizeof(Lane));uint64_t total=0;
    for(unsigned lane=0;lane<n;lane++){
        Lane *l=lanes+lane;l->count=read_u32(file);assert(l->count<=1000000);total+=l->count;
        l->rows=(Row*)calloc(l->count,sizeof(Row));
        for(unsigned j=0;j<l->count;j++){
            Row *r=l->rows+j;r->op=read_u32(file);r->context=read_u32(file);r->tag=read_u32(file);
            assert(r->op<ops&&r->tag<=3&&r->context<1000000);
            r->before=read_double(file);r->after=read_double(file);r->result=read_double(file);
            int fields=pg9_kernel_slots(r->op);assert(fields>=0&&fields<=FIELDS);
            for(int k=0;k<fields;k++)r->input[k]=read_double(file);
            if(r->context>=l->contexts)l->contexts=r->context+1;
        }
        l->modifiers=(double*)calloc(l->contexts,sizeof(double));l->initialized=(unsigned char*)calloc(l->contexts,1);
    }
    assert(fgetc(file)==EOF);fclose(file);
    unsigned *counts=(unsigned*)calloc(ops,sizeof(unsigned)),*ready=(unsigned*)calloc((size_t)n*ops,sizeof(unsigned));
    for(unsigned lane=0;lane<n;lane++)if(lanes[lane].count){unsigned op=lanes[lane].rows[0].op;ready[(size_t)op*n+counts[op]++]=lane;}
    double *columns[FIELDS],*modifier=(double*)calloc(n,sizeof(double)),*output=(double*)calloc(n,sizeof(double));
    uint32_t *tags=(uint32_t*)calloc(n,sizeof(uint32_t));unsigned *batch=(unsigned*)calloc(n,sizeof(unsigned));
    for(unsigned k=0;k<FIELDS;k++)columns[k]=(double*)calloc(n,sizeof(double));
    uint64_t completed=0,batches=0,imports=0,resets=0;unsigned maxBatch=0,cursor=0;
    while(completed<total){
        unsigned op=ops;
        if(rotating){for(unsigned k=0;k<ops;k++){unsigned candidate=(cursor+k)%ops;if(counts[candidate]){op=candidate;cursor=(op+1)%ops;break;}}}
        else{unsigned most=0;for(unsigned k=0;k<ops;k++)if(counts[k]>most){most=counts[k];op=k;}}
        assert(op<ops);unsigned count=counts[op];counts[op]=0;
        memcpy(batch,ready+(size_t)op*n,count*sizeof(unsigned));int fields=pg9_kernel_slots(op);
        for(unsigned j=0;j<count;j++){
            Lane *l=lanes+batch[j];Row *r=l->rows+l->position;
            assert(r->op==op);
            if(!l->initialized[r->context]){l->initialized[r->context]=1;l->modifiers[r->context]=r->before;imports++;}
            else if(!same(l->modifiers[r->context],r->before)){
                // The unconverted source engine can reset/rebind a frame between
                // captured operations. Count imported transitions explicitly.
                l->modifiers[r->context]=r->before;resets++;
            }
            modifier[j]=l->modifiers[r->context];
            for(int k=0;k<fields;k++)columns[k][j]=r->input[k];
        }
        pg9_kernel(op,count,(const double *const*)columns,modifier,tags,output);
        for(unsigned j=0;j<count;j++){
            unsigned id=batch[j];Lane *l=lanes+id;Row *r=l->rows+l->position;
            if(tags[j]!=r->tag||!same(modifier[j],r->after)||(tags[j]&& !same(output[j],r->result))){
                fprintf(stderr,"Mismatch lane=%u row=%u kernel=%u: tag=%u/%u value=%.17g/%.17g modifier=%.17g/%.17g\n",
                    id,l->position,op,tags[j],r->tag,output[j],r->result,modifier[j],r->after);return 1;
            }
            l->modifiers[r->context]=modifier[j];l->position++;completed++;
            if(l->position<l->count){unsigned next=l->rows[l->position].op;assert(counts[next]<n);ready[(size_t)next*n+counts[next]++]=id;}
        }
        batches++;if(count>maxBatch)maxBatch=count;
    }
    printf("{\"order\":\"%s\",\"lanes\":%u,\"operations\":%llu,\"batches\":%llu,\"max_batch\":%u,\"mean_batch\":%.3f,\"reference_frame_imports\":%llu,\"unconverted_frame_resets\":%llu}\n",
        argv[2],n,(unsigned long long)completed,(unsigned long long)batches,maxBatch,(double)completed/batches,(unsigned long long)imports,(unsigned long long)resets);
    for(unsigned lane=0;lane<n;lane++){free(lanes[lane].rows);free(lanes[lane].modifiers);free(lanes[lane].initialized);}free(lanes);
    for(unsigned k=0;k<FIELDS;k++)free(columns[k]);free(modifier);free(output);free(tags);free(batch);free(ready);free(counts);return 0;
}
