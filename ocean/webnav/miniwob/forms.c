/* Transport/serialization only. Generate.bend owns all sampling, text and goals.
 * Deliberately never export the private target bits at words 18+3*i. */
#include "bridge.h"
#include "cJSON.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int number(const char *s,unsigned *out){char *end;errno=0;unsigned long n=strtoul(s,&end,10);if(errno||!*s||*end||*s=='-'||n>UINT_MAX)return -1;*out=(unsigned)n;return 0;}
static void string_at(const uint32_t *r,unsigned start,unsigned cap,char *out){unsigned i=0;for(;i+1<cap&&r[start+i];i++)out[i]=(char)r[start+i];out[i]=0;}
int main(int argc,char **argv){
    unsigned count=32,seed=0;
    const char *task=argc>1?argv[1]:"click-checkboxes";unsigned kind=!strcmp(task,"click-option")?3:1;
    if((strcmp(task,"click-checkboxes")&&strcmp(task,"click-option"))||argc>4||(argc>2&&number(argv[2],&count))||(argc>3&&number(argv[3],&seed))||!count||count>1000000||seed>UINT_MAX-(count-1)){
        fprintf(stderr,"usage: generate_forms click-checkboxes|click-option [count=32, max=1000000] [first_seed=0]\n");return 2;
    }
    uint32_t words[8192]={0};
    for(unsigned start=0;start<count;start+=32){
        for(unsigned lane=0;lane<32;lane++){uint32_t *r=words+lane*256;r[0]=kind;r[7]=3;r[13]=seed+start+(lane<count-start?lane:0);}
        webnav_batch(words);
        for(unsigned lane=0;lane<32&&start+lane<count;lane++){
            const uint32_t *r=words+lane*256;char query[128];string_at(r,128,sizeof query,query);
            cJSON *root=cJSON_CreateObject();cJSON_AddStringToObject(root,"schema","webnav-generated-form-v1");cJSON_AddStringToObject(root,"generator","stock-cpu-bend/forms-v1");cJSON_AddStringToObject(root,"task",task);cJSON_AddNumberToObject(root,"seed",r[13]);cJSON_AddStringToObject(root,"instruction",query);
            cJSON *nodes=cJSON_AddArrayToObject(root,"controls");
            for(unsigned i=0;i<r[1];i++){char name[8];string_at(r,64+8*i,sizeof name,name);cJSON *node=cJSON_CreateObject();cJSON_AddNumberToObject(node,"ref",i+1);cJSON_AddStringToObject(node,"role",r[16+3*i]==1?"button":r[16+3*i]==5?"radio":"checkbox");cJSON_AddStringToObject(node,"name",name);cJSON_AddBoolToObject(node,"checked",r[17+3*i]);cJSON_AddItemToArray(nodes,node);}
            char *line=cJSON_PrintUnformatted(root);cJSON_Delete(root);if(!line)return 1;int bad=puts(line)<0;free(line);if(bad)return 1;
        }
    }
    return fflush(stdout)?1:0;
}
