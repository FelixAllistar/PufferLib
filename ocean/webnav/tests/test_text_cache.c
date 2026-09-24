#include "text_cache.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){const char *texts[]={"Add to cart","café résumé 東京","","[UNK]","Add to cart"};WebTextEncoder *e=web_text_load("build/webnav/reference/potion-tokenizer.json","build/webnav/reference/potion-model.safetensors");assert(e);const char *path="build/webnav/test-text-cache.bin";assert(!web_text_cache_write(path,e,texts,5));WebTextCache *c=web_text_cache_load(path);assert(c);for(unsigned i=0;i<5;i++){float expected[WEB_TEXT_DIM];assert(!web_text_encode(e,texts[i],strlen(texts[i]),expected,NULL));uint32_t id;const float *v=web_text_cache_find(c,texts[i],strlen(texts[i]),&id);assert(v&&id==(i==4?0:i)&&!memcmp(v,expected,sizeof expected));}assert(!web_text_cache_find(c,"not in cache",12,NULL));web_text_cache_free(c);
 FILE *f=fopen(path,"r+b");assert(f);assert(!fseek(f,32,SEEK_SET));int b=fgetc(f);assert(b!=EOF);assert(!fseek(f,32,SEEK_SET));fputc(b^1,f);fclose(f);assert(!web_text_cache_load(path));assert(!web_text_cache_write(path,e,texts,5));f=fopen(path,"r+b");assert(f);assert(!fseek(f,90,SEEK_SET));b=fgetc(f);assert(!fseek(f,90,SEEK_SET));fputc(b^1,f);fclose(f);assert(!web_text_cache_load(path));
 assert(!web_text_cache_write(path,e,texts,5));puts("PASS: exact Unicode/cache lookup, duplicate IDs, misses, model-profile rejection and content corruption rejection");web_text_free(e);return 0;}
