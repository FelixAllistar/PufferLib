#include "text_cache.h"
#include <unicode/uvernum.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {char magic[8];uint32_t version,dimensions,token_cap,icu_major,count,text_bytes;uint64_t tokenizer,model,content;} Header;
typedef struct {uint64_t hash;uint32_t offset,length;float vector[WEB_TEXT_DIM];} Entry;
struct WebTextCache {Header header;Entry *entries;char *text;uint32_t *table,slots;};
_Static_assert(sizeof(Header)==56&&sizeof(Entry)==1040,"Cache wire layout changed");
static uint64_t bytes_hash(uint64_t h,const void *data,size_t n){const unsigned char *s=data;for(size_t i=0;i<n;i++)h=(h^s[i])*UINT64_C(1099511628211);return h;}
static uint64_t text_hash(const char *s,size_t n){return bytes_hash(UINT64_C(14695981039346656037),s,n);}
static int little_endian(void){uint32_t v=1;return *(unsigned char*)&v==1;}
int web_text_cache_write(const char *path,WebTextEncoder *e,const char *const *texts,uint32_t count){
 if(!little_endian()||!count||count>1000000)return -1;size_t size=0;for(uint32_t i=0;i<count;i++){size_t n=strlen(texts[i]);if(n>WEB_TEXT_MAX_BYTES)return -1;size+=n+1;if(size>UINT32_MAX)return -1;}
 Entry *entries=calloc(count,sizeof *entries);char *text=malloc(size);if(!entries||!text){free(entries);free(text);return -1;}uint32_t at=0;int result=-1;
 for(uint32_t i=0;i<count;i++){Entry *r=entries+i;r->length=(uint32_t)strlen(texts[i]);r->offset=at;r->hash=text_hash(texts[i],r->length);memcpy(text+at,texts[i],r->length+1);at+=r->length+1;WebTextTokens tokens;if(web_text_encode(e,texts[i],r->length,r->vector,&tokens)||tokens.truncated)goto done;}
 Header h={"WEBTXT1",WEB_TEXT_VERSION,WEB_TEXT_DIM,WEB_TEXT_TOKENS,U_ICU_VERSION_MAJOR_NUM,count,at,WEB_TEXT_TOKENIZER_FINGERPRINT,WEB_TEXT_MODEL_FINGERPRINT,0};h.content=bytes_hash(text_hash((const char*)entries,(size_t)count*sizeof *entries),text,at);
 FILE *f=fopen(path,"wb");if(!f)goto done;int good=fwrite(&h,sizeof h,1,f)==1&&fwrite(entries,sizeof *entries,count,f)==count&&fwrite(text,1,at,f)==at;int closed=fclose(f);if(good&&!closed)result=0;
 done:free(entries);free(text);return result;
}
void web_text_cache_free(WebTextCache *c){if(c){free(c->entries);free(c->text);free(c->table);free(c);}}
WebTextCache *web_text_cache_load(const char *path){
 if(!little_endian())return NULL;FILE *f=fopen(path,"rb");if(!f)return NULL;WebTextCache *c=calloc(1,sizeof *c);if(!c){fclose(f);return NULL;}Header *h=&c->header;
 if(fread(h,sizeof *h,1,f)!=1||memcmp(h->magic,"WEBTXT1",8)||h->version!=WEB_TEXT_VERSION||h->dimensions!=WEB_TEXT_DIM||h->token_cap!=WEB_TEXT_TOKENS||h->icu_major!=U_ICU_VERSION_MAJOR_NUM||h->tokenizer!=WEB_TEXT_TOKENIZER_FINGERPRINT||h->model!=WEB_TEXT_MODEL_FINGERPRINT||!h->count||h->count>1000000||h->text_bytes>100000000)goto fail;
 if(fseek(f,0,SEEK_END))goto fail;
 long file_size=ftell(f);
 if(file_size<0||(uint64_t)file_size!=sizeof *h+(uint64_t)h->count*sizeof *c->entries+h->text_bytes||fseek(f,sizeof *h,SEEK_SET))goto fail;
 c->entries=malloc((size_t)h->count*sizeof *c->entries);c->text=malloc(h->text_bytes?h->text_bytes:1);c->slots=1;while(c->slots<h->count*2)c->slots*=2;c->table=calloc(c->slots,sizeof *c->table);if(!c->entries||!c->text||!c->table)goto fail;
 if(fread(c->entries,sizeof *c->entries,h->count,f)!=h->count||fread(c->text,1,h->text_bytes,f)!=h->text_bytes||fgetc(f)!=EOF)goto fail;
 if(h->content!=bytes_hash(text_hash((const char*)c->entries,(size_t)h->count*sizeof *c->entries),c->text,h->text_bytes))goto fail;
 for(uint32_t i=0;i<h->count;i++){Entry *r=c->entries+i;if(r->offset>=h->text_bytes||r->length>=h->text_bytes-r->offset||r->length>WEB_TEXT_MAX_BYTES||c->text[r->offset+r->length]||memchr(c->text+r->offset,0,r->length)||r->hash!=text_hash(c->text+r->offset,r->length))goto fail;for(int d=0;d<WEB_TEXT_DIM;d++)if(!isfinite(r->vector[d]))goto fail;uint32_t at=(uint32_t)r->hash&(c->slots-1);while(c->table[at]){Entry *prev=c->entries+c->table[at]-1;if(prev->length==r->length&&!memcmp(c->text+prev->offset,c->text+r->offset,r->length))break;at=(at+1)&(c->slots-1);}if(!c->table[at])c->table[at]=i+1;}
 fclose(f);return c;
 fail:fclose(f);web_text_cache_free(c);return NULL;
}
const float *web_text_cache_find(const WebTextCache *c,const char *text,size_t length,uint32_t *id){if(!c||!text||length>WEB_TEXT_MAX_BYTES)return NULL;uint64_t hash=text_hash(text,length);uint32_t at=(uint32_t)hash&(c->slots-1);while(c->table[at]){uint32_t i=c->table[at]-1;const Entry *r=c->entries+i;if(r->hash==hash&&r->length==length&&!memcmp(c->text+r->offset,text,length)){if(id)*id=i;return r->vector;}at=(at+1)&(c->slots-1);}return NULL;}
