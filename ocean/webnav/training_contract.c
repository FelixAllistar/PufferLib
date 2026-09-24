#include "training_contract.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef WT_SOURCE_HASH
#define WT_SOURCE_HASH "development-unversioned"
#endif
static int get(const cJSON *o,const char *k){const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,k);return cJSON_IsNumber(v)?v->valueint:-1;}
int wt_contract_write(const char *path,int potion,int hidden,int layers,unsigned seed,unsigned tasks){
 char out[8192],tmp[8192];if(snprintf(out,sizeof out,"%s.webnav.json",path)>=(int)sizeof out||snprintf(tmp,sizeof tmp,"%s.tmp",out)>=(int)sizeof tmp)return -1;
 cJSON *j=cJSON_CreateObject();cJSON_AddStringToObject(j,"schema","webnav-training-v1");cJSON_AddStringToObject(j,"source_sha256",WT_SOURCE_HASH);cJSON_AddNumberToObject(j,"version",WT_VERSION);cJSON_AddNumberToObject(j,"features",WT_FEATURES);cJSON_AddNumberToObject(j,"actions",WT_ACTIONS);cJSON_AddNumberToObject(j,"potion",potion);cJSON_AddNumberToObject(j,"hidden",hidden);cJSON_AddNumberToObject(j,"layers",layers);cJSON_AddNumberToObject(j,"seed",seed);cJSON_AddNumberToObject(j,"task_mask",tasks);cJSON_AddStringToObject(j,"tokenizer","ordered-utf8-byte-bits-v1");cJSON_AddStringToObject(j,"semantic_profile",potion?"potion-8m-30192f937c6e10f3-fixed32-v1":"disabled");char *text=cJSON_Print(j);cJSON_Delete(j);if(!text)return -1;FILE *f=fopen(tmp,"w");if(!f){free(text);return -1;}int bad=fputs(text,f)<0;free(text);bad|=fclose(f)!=0;if(bad)return -1;return rename(tmp,out);
}
int wt_contract_read(const char *path,int *potion,int *hidden,int *layers){
 char sidecar[8192],buf[8192];if(snprintf(sidecar,sizeof sidecar,"%s.webnav.json",path)>=(int)sizeof sidecar)return -1;FILE *f=fopen(sidecar,"r");if(!f)return -1;size_t n=fread(buf,1,sizeof(buf)-1,f);int extra=fgetc(f);fclose(f);if(extra!=EOF)return -1;buf[n]=0;cJSON *j=cJSON_Parse(buf);if(!j)return -1;const cJSON *h=cJSON_GetObjectItemCaseSensitive(j,"source_sha256");int valid=get(j,"version")==WT_VERSION&&get(j,"features")==WT_FEATURES&&get(j,"actions")==WT_ACTIONS&&cJSON_IsString(h)&&!strcmp(h->valuestring,WT_SOURCE_HASH);*potion=get(j,"potion");*hidden=get(j,"hidden");*layers=get(j,"layers");valid&=(*potion==0||*potion==1)&&*hidden>=8&&*hidden<=4096&&*hidden%8==0&&*layers>=1&&*layers<=16;cJSON_Delete(j);return valid?0:-1;
}
