#include "text_encoder.h"
#include "cJSON.h"
#include <unicode/uchar.h>
#include <unicode/unorm2.h>
#include <unicode/utf8.h>
#include <unicode/utf16.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TABLE 65536
struct WebTextEncoder { cJSON *tokenizer;char *model;const float *vectors;const char *keys[TABLE];int ids[TABLE];int unknown;const UNormalizer2 *nfd; };
static unsigned hash(const char *s){unsigned h=2166136261u;while(*s)h=(h^(unsigned char)*s++)*16777619u;return h;}
/* Accidental profile-mismatch detection; downloads additionally use SHA-256. */
static uint64_t fingerprint(const char *s,size_t n){uint64_t h=UINT64_C(14695981039346656037);for(size_t i=0;i<n;i++)h=(h^(unsigned char)s[i])*UINT64_C(1099511628211);return h;}
static int lookup(const WebTextEncoder *e,const char *s){unsigned h=hash(s)%TABLE;for(unsigned i=0;i<TABLE;i++){if(!e->keys[h])return -1;if(!strcmp(e->keys[h],s))return e->ids[h];h=(h+1)%TABLE;}return -1;}
static char *read_file(const char *path,size_t *size){FILE *f=fopen(path,"rb");if(!f)return NULL;if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);if(n<0||n>100000000){fclose(f);return NULL;}rewind(f);char *s=calloc((size_t)n+1,1);if(!s||fread(s,1,(size_t)n,f)!=(size_t)n){free(s);fclose(f);return NULL;}fclose(f);*size=(size_t)n;return s;}
void web_text_free(WebTextEncoder *e){if(e){cJSON_Delete(e->tokenizer);free(e->model);free(e);}}
WebTextEncoder *web_text_load(const char *tokenizer_path,const char *weights_path){
    WebTextEncoder *e=calloc(1,sizeof *e);if(!e)return NULL;size_t nt=0,nm=0;char *text=read_file(tokenizer_path,&nt);if(!text)goto fail;if(fingerprint(text,nt)!=WEB_TEXT_TOKENIZER_FINGERPRINT){free(text);goto fail;}e->tokenizer=cJSON_Parse(text);free(text);if(!e->tokenizer)goto fail;
    const cJSON *model=cJSON_GetObjectItem(e->tokenizer,"model"),*type=cJSON_GetObjectItem(model,"type"),*vocab=cJSON_GetObjectItem(model,"vocab");
    if(!cJSON_IsString(type)||strcmp(type->valuestring,"WordPiece")||!cJSON_IsObject(vocab)||cJSON_GetArraySize(vocab)!=29528)goto fail;
    const cJSON *norm=cJSON_GetObjectItem(e->tokenizer,"normalizer");type=cJSON_GetObjectItem(norm,"type");
    if(!cJSON_IsString(type)||strcmp(type->valuestring,"BertNormalizer")||!cJSON_IsTrue(cJSON_GetObjectItem(norm,"lowercase"))||!cJSON_IsTrue(cJSON_GetObjectItem(norm,"clean_text"))||!cJSON_IsTrue(cJSON_GetObjectItem(norm,"handle_chinese_chars")))goto fail;
    cJSON *item;unsigned char seen[29528]={0};cJSON_ArrayForEach(item,vocab){if(!cJSON_IsNumber(item)||item->valuedouble!=item->valueint||item->valueint<0||item->valueint>=29528||seen[item->valueint])goto fail;seen[item->valueint]=1;unsigned h=hash(item->string)%TABLE;while(e->keys[h])h=(h+1)%TABLE;e->keys[h]=item->string;e->ids[h]=item->valueint;}
    e->unknown=lookup(e,"[UNK]");if(e->unknown<0)goto fail;
    e->model=read_file(weights_path,&nm);if(!e->model||nm<8||fingerprint(e->model,nm)!=WEB_TEXT_MODEL_FINGERPRINT)goto fail;uint64_t header;memcpy(&header,e->model,8);if(header>nm-8||header>65536)goto fail;
    char *meta=calloc((size_t)header+1,1);if(!meta)goto fail;memcpy(meta,e->model+8,(size_t)header);cJSON *j=cJSON_Parse(meta);free(meta);if(!j)goto fail;
    cJSON *tensor=cJSON_GetObjectItem(j,"embeddings"),*shape=cJSON_GetObjectItem(tensor,"shape"),*offset=cJSON_GetObjectItem(tensor,"data_offsets"),*dtype=cJSON_GetObjectItem(tensor,"dtype");
    int valid=cJSON_IsString(dtype)&&!strcmp(dtype->valuestring,"F32")&&cJSON_GetArraySize(shape)==2&&cJSON_GetArrayItem(shape,0)->valueint==29528&&cJSON_GetArrayItem(shape,1)->valueint==WEB_TEXT_DIM&&cJSON_GetArraySize(offset)==2&&cJSON_GetArrayItem(offset,0)->valueint==0&&cJSON_GetArrayItem(offset,1)->valuedouble==29528.0*WEB_TEXT_DIM*4&&nm==8+header+29528u*WEB_TEXT_DIM*4&&(8+header)%4==0;
    cJSON_Delete(j);if(!valid)goto fail;e->vectors=(const float*)(e->model+8+header);
    UErrorCode status=U_ZERO_ERROR;e->nfd=unorm2_getNFDInstance(&status);if(U_FAILURE(status))goto fail;return e;
fail: web_text_free(e);return NULL;
}
static void emit(const WebTextEncoder *e,WebTextTokens *out,int id){out->total++;if(id==e->unknown)out->unknown++;if(out->count<WEB_TEXT_TOKENS)out->ids[out->count++]=id;else out->truncated=1;}
static int chinese(UChar32 c){return (c>=0x4e00&&c<=0x9fff)||(c>=0x3400&&c<=0x4dbf)||(c>=0x20000&&c<=0x2a6df)||(c>=0x2a700&&c<=0x2b73f)||(c>=0x2b740&&c<=0x2b81f)||(c>=0x2b820&&c<=0x2ceaf)||(c>=0xf900&&c<=0xfaff)||(c>=0x2f800&&c<=0x2fa1f);}
static int punctuation(UChar32 c){return (c>=33&&c<=47)||(c>=58&&c<=64)||(c>=91&&c<=96)||(c>=123&&c<=126)||u_ispunct(c);}
static void word(const WebTextEncoder *e,const char *s,int bytes,int chars,WebTextTokens *out){
    if(!chars)return;if(chars>100){emit(e,out,e->unknown);return;}int ids[100],n=0,start=0;
    while(start<bytes){int id=-1,end=bytes;char piece[408];for(;end>start;){int prefix=start?2:0;piece[0]='#';piece[1]='#';memcpy(piece+prefix,s+start,(size_t)(end-start));piece[prefix+end-start]=0;id=lookup(e,piece);if(id>=0)break;end--;while(end>start&&((unsigned char)s[end]&0xc0)==0x80)end--;}
        if(id<0){emit(e,out,e->unknown);return;}ids[n++]=id;start=end;}
    for(int i=0;i<n;i++)emit(e,out,ids[i]);
}
static int segment(const WebTextEncoder *e,const char *s,int length,WebTextTokens *out){
    if(!length)return 0;UChar *clean=malloc(((size_t)length*3+1)*sizeof *clean);if(!clean)return -1;int size=0;
    for(int i=0;i<length;){UChar32 c;U8_NEXT(s,i,length,c);if(c<0){free(clean);return -1;}int category=u_charType(c);if(c==0||c==0xfffd)continue;if(c==' '||c=='\t'||c=='\n'||c=='\r'||category==U_SPACE_SEPARATOR)c=' ';else if(category==U_CONTROL_CHAR||category==U_FORMAT_CHAR)continue;if(chinese(c))clean[size++]=' ';c=u_tolower(c);U16_APPEND_UNSAFE(clean,size,c);if(chinese(c))clean[size++]=' ';}
    UErrorCode status=U_ZERO_ERROR;int required=unorm2_normalize(e->nfd,clean,size,NULL,0,&status);if(status!=U_BUFFER_OVERFLOW_ERROR&&U_FAILURE(status)){free(clean);return -1;}status=U_ZERO_ERROR;UChar *normalized=malloc(((size_t)required+1)*sizeof *normalized);if(!normalized){free(clean);return -1;}int count=unorm2_normalize(e->nfd,clean,size,normalized,required+1,&status);free(clean);if(U_FAILURE(status)){free(normalized);return -1;}
    char buffer[404];int bytes=0,chars=0;
    for(int i=0;i<count;){UChar32 c;U16_NEXT(normalized,i,count,c);if(u_charType(c)==U_NON_SPACING_MARK)continue;int split=u_isUWhiteSpace(c),punct=punctuation(c);if(split||punct){word(e,buffer,bytes,chars,out);bytes=chars=0;if(split)continue;}chars++;if(chars<=100)U8_APPEND_UNSAFE(buffer,bytes,c);if(punct){word(e,buffer,bytes,chars,out);bytes=chars=0;}}
    word(e,buffer,bytes,chars,out);free(normalized);return 0;
}
int web_text_tokenize(const WebTextEncoder *e,const char *text,size_t bytes,WebTextTokens *out){
    if(!e||!text||!out||bytes>WEB_TEXT_MAX_BYTES)return -1;memset(out,0,sizeof *out);static const char *special[]={"[PAD]","[UNK]","[CLS]","[SEP]","[MASK]"};size_t base=0,i=0;
    while(i<bytes){int found=-1;for(int k=0;k<5;k++){size_t n=strlen(special[k]);if(n<=bytes-i&&!memcmp(text+i,special[k],n)){found=k;break;}}
        if(found<0){i++;continue;}if(segment(e,text+base,(int)(i-base),out))return -1;emit(e,out,lookup(e,special[found]));i+=strlen(special[found]);base=i;}
    return segment(e,text+base,(int)(bytes-base),out);
}
int web_text_encode(const WebTextEncoder *e,const char *text,size_t bytes,float out[WEB_TEXT_DIM],WebTextTokens *tokens){
    WebTextTokens scratch;if(!tokens)tokens=&scratch;if(web_text_tokenize(e,text,bytes,tokens))return -1;memset(out,0,WEB_TEXT_DIM*sizeof *out);
    for(unsigned i=0;i<tokens->count;i++){int id=tokens->ids[i];if(id==e->unknown)continue;const float *v=e->vectors+(size_t)id*WEB_TEXT_DIM;for(int d=0;d<WEB_TEXT_DIM;d++)out[d]+=v[d];}
    double norm=0;for(int d=0;d<WEB_TEXT_DIM;d++)norm+=(double)out[d]*out[d];float scale=norm>0?(float)(1/sqrt(norm)):0;for(int d=0;d<WEB_TEXT_DIM;d++)out[d]*=scale;return 0;
}
