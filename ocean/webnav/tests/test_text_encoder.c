#include "text_encoder.h"
#include "cJSON.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *read_file(const char *p){FILE *f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char *s=calloc((size_t)n+1,1);assert(s&&fread(s,1,(size_t)n,f)==(size_t)n);fclose(f);return s;}
static int digit(char x){return x<='9'?x-'0':x-'a'+10;}
int main(void){WebTextEncoder *e=web_text_load("build/webnav/reference/potion-tokenizer.json","build/webnav/reference/potion-model.safetensors");assert(e);char *raw=read_file("build/webnav/reference/tokenizer-golden.json");cJSON *j=cJSON_Parse(raw),*cases=cJSON_GetObjectItem(j,"cases");assert(cases);int count=cJSON_GetArraySize(cases);float max_error=0;
 for(int i=0;i<count;i++){cJSON *r=cJSON_GetArrayItem(cases,i),*ids=cJSON_GetObjectItem(r,"ids"),*vec=cJSON_GetObjectItem(r,"vector");const char *hex=cJSON_GetObjectItem(r,"hex")->valuestring;size_t len=strlen(hex)/2;char *s=malloc(len+1);assert(s);for(size_t k=0;k<len;k++)s[k]=(char)(digit(hex[2*k])*16+digit(hex[2*k+1]));s[len]=0;
 WebTextTokens t;float actual[WEB_TEXT_DIM];assert(!web_text_encode(e,s,len,actual,&t));int expected=cJSON_GetArraySize(ids);int bad=t.count!=(unsigned)expected||t.total!=(unsigned)cJSON_GetObjectItem(r,"total")->valueint||t.unknown!=(unsigned)cJSON_GetObjectItem(r,"unknown")->valueint||t.truncated!=(t.total>WEB_TEXT_TOKENS);
 for(int k=0;k<expected&&!bad;k++)if(t.ids[k]!=cJSON_GetArrayItem(ids,k)->valueint)bad=1;
 if(bad){fprintf(stderr,"Token mismatch case=%d text=%s expected=%d actual=%u total=%u unknown=%u\n",i,s,expected,t.count,t.total,t.unknown);for(unsigned k=0;k<t.count;k++)fprintf(stderr,"%d ",t.ids[k]);fprintf(stderr,"\n");char *expected_json=cJSON_PrintUnformatted(ids);fprintf(stderr,"expected %s\n",expected_json);free(expected_json);return 1;}
 for(int d=0;d<WEB_TEXT_DIM;d++){float error=fabsf(actual[d]-(float)cJSON_GetArrayItem(vec,d)->valuedouble);if(error>max_error)max_error=error;assert(isfinite(actual[d])&&error<1e-5f);}free(s);}
 WebTextTokens t;const char invalid[]={ (char)0xff };assert(web_text_tokenize(e,invalid,1,&t)==-1);assert(web_text_tokenize(e,"",WEB_TEXT_MAX_BYTES+1,&t)==-1);
 printf("{\"tokenizer_parity\":\"PASS\",\"reference\":\"Hugging Face Rust tokenizers 0.23.2\",\"cases\":%d,\"vector_reference\":\"independent JS normalized sum\",\"max_vector_error\":%.9g,\"unicode\":\"ICU\",\"token_cap\":%d}\n",count,max_error,WEB_TEXT_TOKENS);web_text_free(e);cJSON_Delete(j);free(raw);return 0;}
