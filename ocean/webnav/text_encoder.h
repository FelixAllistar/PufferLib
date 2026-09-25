#ifndef WEBNAV_TEXT_ENCODER_H
#define WEBNAV_TEXT_ENCODER_H
#include <stddef.h>
#include <stdint.h>
#define WEB_TEXT_DIM 256
#define WEB_TEXT_TOKENS 512
#define WEB_TEXT_MAX_BYTES 32768
#define WEB_TEXT_VERSION 1
#define WEB_TEXT_TOKENIZER_FINGERPRINT UINT64_C(0x4a8e209db908ec36)
#define WEB_TEXT_MODEL_FINGERPRINT UINT64_C(0x30192f937c6e10f3)
/* Pinned Potion WordPiece/BertNormalizer profile. CPU, immutable after load. */
typedef struct WebTextEncoder WebTextEncoder;
typedef struct { uint32_t count,total,unknown,truncated; int32_t ids[WEB_TEXT_TOKENS]; } WebTextTokens;
WebTextEncoder *web_text_load(const char *tokenizer_path,const char *weights_path);
void web_text_free(WebTextEncoder *encoder);
int web_text_tokenize(const WebTextEncoder *encoder,const char *text,size_t bytes,WebTextTokens *out);
int web_text_encode(const WebTextEncoder *encoder,const char *text,size_t bytes,float out[WEB_TEXT_DIM],WebTextTokens *tokens);
#endif
