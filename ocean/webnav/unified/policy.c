#include "policy.h"
#include "semantic.h"
#include <math.h>
#include <string.h>

typedef struct { const char *bytes; size_t length; } WUString;

static int wu_inputs(const WFView *v,const WUCapabilities *c,const WUState *s) {
    return v&&c&&s&&v->version==WF_ABI_VERSION&&v->count<=WF_MAX_NODES&&
        v->text_bytes<=WF_TEXT_BYTES&&c->version==WU_CAP_VERSION&&
        c->count<=WU_MAX_CAPABILITIES&&s->parameter0<WU_PARAMETER_BINS&&
        s->parameter1<WU_PARAMETER_BINS&&s->text_length<=WU_TEXT_LIMIT;
}
static WUString wu_string(const WFView *v,WFText t) {
    if(t.offset>=v->text_bytes||t.length>=v->text_bytes-t.offset||
       v->text[t.offset+t.length]!=0)return (WUString){NULL,0};
    return (WUString){v->text+t.offset,t.length};
}
static int wu_space(unsigned char b) {
    return b==' '||b=='\t'||b=='\n'||b=='\r'||b=='\v'||b=='\f';
}
static int wu_next_token(WUString s,size_t *offset,WUString *token) {
    size_t i=*offset;
    while(i<s.length&&wu_space((unsigned char)s.bytes[i]))i++;
    size_t start=i;
    while(i<s.length&&!wu_space((unsigned char)s.bytes[i]))i++;
    *offset=i;
    if(start==i)return 0;
    *token=(WUString){s.bytes+start,i-start};
    return 1;
}
/* Copy catalog: instruction, then name and value of every node in public order.
 * All spans, including spans longer than the text register, occupy an index. */
static unsigned wu_tokens(const WFView *v,WUString out[WU_COPY_TOKENS]) {
    unsigned count=0;
    for(unsigned source=0;source<1+2*v->count&&count<WU_COPY_TOKENS;source++) {
        WFText t=source==0?v->instruction:
            ((source&1)?v->nodes[(source-1)/2].name:v->nodes[(source-1)/2].value);
        WUString s=wu_string(v,t),token;
        size_t offset=0;
        while(count<WU_COPY_TOKENS&&wu_next_token(s,&offset,&token))out[count++]=token;
    }
    return count;
}
static WUString wu_value(const WFView *v,unsigned index) {
    WUString s=wu_string(v,v->nodes[index].value);
    return s.length?s:wu_string(v,v->nodes[index].name);
}
static float wu_bound(double value) {
    if(!isfinite(value))return 0.0f;
    return (float)(value<-1?-1:value>1?1:value);
}
static float wu_unsigned(uint32_t value) {
    return (float)((double)value/((double)value+256.0));
}
static float wu_length(size_t value) {
    return (float)((double)value/((double)value+256.0));
}
static uint32_t wu_hash(WUString token) {
    uint32_t hash=2166136261u;
    for(size_t i=0;i<token.length;i++) {
        unsigned char b=(unsigned char)token.bytes[i];
        if(b>='A'&&b<='Z')b=(unsigned char)(b-'A'+'a');
        hash=(hash^b)*16777619u;
    }
    return hash;
}
static void wu_bytes(float *out,unsigned width,WUString s) {
    for(unsigned i=0;i<width&&i<s.length;i++)out[i]=((unsigned char)s.bytes[i]+1)/256.0f;
}
/* Lexical features use ASCII-folded whitespace tokens. The bag records bounded
 * counts; ordered fingerprints retain positions and word order independently. */
static void wu_lexical(float *bag,unsigned bag_width,float *order,unsigned order_width,WUString s) {
    size_t offset=0;
    unsigned position=0;
    WUString token;
    while(wu_next_token(s,&offset,&token)) {
        uint32_t hash=wu_hash(token);
        unsigned bin=hash%bag_width;
        bag[bin]=fminf(1.0f,bag[bin]+0.125f);
        if(position<order_width)order[position]=(float)(((double)hash+1.0)/4294967296.0);
        position++;
    }
}
static uint32_t wu_map(uint32_t bin,uint32_t low,uint32_t high,uint32_t step) {
    uint64_t stride=step?step:1;
    uint64_t span=(uint64_t)high-low;
    uint64_t last=span/stride;
    /* Exact integer nearest-step rounding, ties toward the larger legal step. */
    uint64_t tick=(span*bin+128*stride)/(256*stride);
    if(tick>last)tick=last;
    return (uint32_t)(low+tick*stride);
}
static int wu_payload(const WUState *s,const WUCapability *cap) {
    if(cap->kind==WF_INSERT||(cap->flags&WU_CAP_TEXT)) {
        if(!s->text_length||s->text_length>cap->text_capacity)return 0;
        if(cap->flags&WU_CAP_ASCII)
            for(size_t i=0;i<s->text_length;i++)
                if((unsigned char)s->text[i]<32u||(unsigned char)s->text[i]>126u)return 0;
    }
    if(cap->kind==WF_SELECT_RANGE)
        if(wu_map(s->parameter0,cap->min0,cap->max0,cap->step0)>
           wu_map(s->parameter1,cap->min1,cap->max1,cap->step1))return 0;
    return 1;
}
static const WUCapability *wu_cap(const WUCapabilities *c,const WUState *s,uint32_t kind,uint32_t ref) {
    for(uint32_t i=0;i<c->count;i++) {
        const WUCapability *cap=c->items+i;
        if(cap->kind==kind&&cap->ref==ref&&cap->min0<=cap->max0&&
           cap->min1<=cap->max1&&wu_payload(s,cap))return cap;
    }
    return NULL;
}
/* The ten capability summary offsets are min0,max0,step0,unit0,min1,max1,
 * step1,unit1,flags,text_capacity. Maxima across public capabilities are used;
 * action decoding always uses the individual capability's exact bounds. */
static void wu_cap_features(float *kinds,float *summary,const WUCapabilities *c,uint32_t ref) {
    uint32_t maxima[10]={0};
    for(uint32_t i=0;i<c->count;i++) {
        const WUCapability *cap=c->items+i;
        if(cap->ref!=ref||cap->kind>WF_SELECT_OPTION)continue;
        kinds[cap->kind]=1.0f;
        uint32_t values[10]={cap->min0,cap->max0,cap->step0?cap->step0:1,cap->unit0,
            cap->min1,cap->max1,cap->step1?cap->step1:1,cap->unit1,cap->flags,cap->text_capacity};
        for(unsigned j=0;j<10;j++)if(values[j]>maxima[j])maxima[j]=values[j];
    }
    for(unsigned i=0;i<10;i++)summary[i]=wu_unsigned(maxima[i]);
    summary[3]=wu_bound(maxima[3]/6.0);
    summary[7]=wu_bound(maxima[7]/6.0);
    summary[8]=wu_bound(maxima[8]/7.0);
}
static unsigned wu_parent(const WFView *v,uint32_t ref) {
    if(!ref)return 0;
    for(unsigned i=0;i<v->count;i++)if(v->nodes[i].ref==ref)return i+1;
    return 0;
}
static unsigned wu_controls(const WFView *v,const WUCapabilities *c) {
    unsigned enabled=0;
    for(unsigned i=0;i<c->count;i++) {
        const WUCapability *cap=c->items+i;
        if(cap->kind>WF_SELECT_OPTION||cap->min0>cap->max0||cap->min1>cap->max1||
           (cap->ref&&!wu_parent(v,cap->ref)))continue;
        if(cap->max0>cap->min0)enabled|=1;
        if(cap->max1>cap->min1)enabled|=2;
        if((cap->kind==WF_INSERT||(cap->flags&WU_CAP_TEXT))&&cap->text_capacity)enabled|=4;
    }
    return enabled;
}

int wu_project(const WFView *v,const WUCapabilities *c,const WUState *s,
               float observations[WU_OBS_SIZE],unsigned char mask[WU_ACTIONS]) {
    if(!observations||!mask)return -1;
    memset(observations,0,WU_OBS_SIZE*sizeof *observations);
    memset(mask,0,WU_ACTIONS*sizeof *mask);
    if(!wu_inputs(v,c,s))return -1;
    float *g=observations;
    /* Global 0..15: count, omitted, text truncation, capability incompleteness,
     * elapsed fraction, remaining fraction, elapsed/deadline magnitudes,
     * instruction length, text pool length, capability count, reserved 11..15.
     * 16..31: registers 0/1, last kind/ref, steps, selected payload length,
     * payload present, reserved 23..31.
     * 32..111 instruction bytes; 112..143 lexical bag; 144..159 ordered tokens.
     * 160..207 payload bytes; 208..223 payload lexical bag (no ordered tokens).
     * 224..245 page capability kind bits; 246..255 page capability summary. */
    WUString instruction=wu_string(v,v->instruction),payload={s->text,s->text_length};
    g[0]=v->count/(float)WF_MAX_NODES;
    g[1]=wu_unsigned(v->omitted);g[2]=!!v->text_truncated;g[3]=!!c->incomplete;
    if(v->deadline_ms) {
        g[4]=wu_bound((double)v->elapsed_ms/v->deadline_ms);
        g[5]=wu_bound(1.0-(double)v->elapsed_ms/v->deadline_ms);
    }
    g[6]=wu_unsigned(v->elapsed_ms);g[7]=wu_unsigned(v->deadline_ms);
    g[8]=wu_length(instruction.length);g[9]=wu_unsigned(v->text_bytes);
    g[10]=c->count/(float)WU_MAX_CAPABILITIES;
    g[16]=s->parameter0/256.0f;g[17]=s->parameter1/256.0f;
    g[18]=wu_bound(s->last_kind/21.0);g[19]=wu_unsigned(s->last_ref);
    g[20]=wu_unsigned(s->steps);g[21]=s->text_length/(float)WU_TEXT_LIMIT;g[22]=!!s->text_length;
    wu_bytes(g+32,80,instruction);wu_lexical(g+112,32,g+144,16,instruction);
    /* 256..287: shared frozen semantic projection of the instruction. */
    if(instruction.bytes&&wu_semantic_encode(instruction.bytes,instruction.length,g+256))return -1;
    wu_bytes(g+160,48,payload);wu_lexical(g+208,16,NULL,0,payload);
    wu_cap_features(g+224,g+246,c,0);g[224+WF_WAIT]=1.0f;
    for(unsigned i=0;i<v->count;i++) {
        const WFNode *n=v->nodes+i;
        float *f=g+WU_GLOBAL_FEATURES+i*WU_NODE_FEATURES;
        /* Node 0: present; 1: role; 2..9 flags by bit; 10: public ref;
         * 11: parent public slot; 12: public slot; 13: parent resolves;
         * 14..17 x,y,width,height /4096; 18..19 selection offsets;
         * 20 capacity; 21..24 scroll x,y,max x,max y /4096;
         * 25..26 scroll fractions; 27..28 name/value lengths;
         * 29..30 selection fractions of value length; 31 reserved.
         * 32..47 name bytes; 48..63 value bytes; 64..71/72..79 lexical bags;
         * 80..87/88..95 ordered name/value token fingerprints;
         * 96..117 capability kind bits; 118..127 capability summary. */
        WUString name=wu_string(v,n->name),value=wu_string(v,n->value);
        unsigned parent=wu_parent(v,n->parent);
        f[0]=1.0f;f[1]=wu_bound(n->role/16.0);
        for(unsigned bit=0;bit<8;bit++)f[2+bit]=!!(n->flags&(1u<<bit));
        f[10]=wu_unsigned(n->ref);f[11]=parent/(float)WF_MAX_NODES;
        f[12]=(i+1)/(float)WF_MAX_NODES;f[13]=!!parent;
        f[14]=wu_bound(n->x/4096.0);f[15]=wu_bound(n->y/4096.0);
        f[16]=wu_bound(n->width/4096.0);f[17]=wu_bound(n->height/4096.0);
        f[18]=wu_unsigned(n->selection_start);f[19]=wu_unsigned(n->selection_end);
        f[20]=wu_unsigned(n->capacity);
        f[21]=wu_bound(n->scroll_x/4096.0);f[22]=wu_bound(n->scroll_y/4096.0);
        f[23]=wu_bound(n->scroll_max_x/4096.0);f[24]=wu_bound(n->scroll_max_y/4096.0);
        if(n->scroll_max_x>0)f[25]=wu_bound((double)n->scroll_x/n->scroll_max_x);
        if(n->scroll_max_y>0)f[26]=wu_bound((double)n->scroll_y/n->scroll_max_y);
        f[27]=wu_length(name.length);f[28]=wu_length(value.length);
        if(value.length) {f[29]=wu_bound((double)n->selection_start/value.length);
            f[30]=wu_bound((double)n->selection_end/value.length);}
        wu_bytes(f+32,16,name);wu_bytes(f+48,16,value);
        wu_lexical(f+64,8,f+80,8,name);wu_lexical(f+72,8,f+88,8,value);
        wu_cap_features(f+96,f+118,c,n->ref);
        /* 128..159 name semantics; 160..191 value semantics. */
        if(name.bytes&&wu_semantic_encode(name.bytes,name.length,f+128))return -1;
        if(value.bytes&&wu_semantic_encode(value.bytes,value.length,f+160))return -1;
    }
    for(unsigned i=0;i<c->count;i++) {
        const WUCapability *cap=c->items+i;
        if(cap->kind>WF_SELECT_OPTION||cap->min0>cap->max0||
           cap->min1>cap->max1||!wu_payload(s,cap))continue;
        unsigned slot=cap->ref?wu_parent(v,cap->ref):0;
        if(cap->ref&&!slot)continue;
        mask[cap->kind*(WF_MAX_NODES+1u)+slot]=1;
    }
    mask[WF_WAIT*(WF_MAX_NODES+1u)]=1;
    unsigned controls=wu_controls(v,c);
    if(controls&1)memset(mask+WU_SET0,1,WU_PARAMETER_BINS);
    if(controls&2)memset(mask+WU_SET1,1,WU_PARAMETER_BINS);
    if(controls&4) {
        memset(mask+WU_LITERAL,1,95);
        WUString tokens[WU_COPY_TOKENS];unsigned count=wu_tokens(v,tokens);
        for(unsigned i=0;i<count;i++)mask[WU_TOKEN+i]=tokens[i].length<=WU_TEXT_LIMIT;
        for(unsigned i=0;i<v->count;i++) {
            WUString value=wu_value(v,i);
            mask[WU_VALUE+i]=value.length>0&&value.length<=WU_TEXT_LIMIT;
        }
        mask[WU_INSTRUCTION]=instruction.length>0&&instruction.length<=WU_TEXT_LIMIT;
    }
    return 0;
}

static int wu_copy(WUState *s,WUString source) {
    if(!source.bytes||!source.length||source.length>WU_TEXT_LIMIT)return -1;
    memcpy(s->text,source.bytes,source.length);
    s->text_length=source.length;s->text[source.length]=0;
    return 0;
}
int wu_decode(const WFView *v,const WUCapabilities *c,WUState *s,uint32_t choice,
              uint32_t elapsed_ms,WFAction *action) {
    if(action)*action=(WFAction){0};
    if(!action||!wu_inputs(v,c,s)||choice>=WU_ACTIONS)return -1;
    if(choice<WU_EXEC_COUNT) {
        uint32_t kind=choice/(WF_MAX_NODES+1u),slot=choice%(WF_MAX_NODES+1u);
        if(slot>v->count)return -1;
        uint32_t ref=slot?v->nodes[slot-1].ref:0;
        if(slot&&!ref)return -1;
        const WUCapability *cap=wu_cap(c,s,kind,ref);
        if(!cap&&!(kind==WF_WAIT&&slot==0))return -1;
        action->kind=kind;action->target=cap?cap->wire_target:0;
        action->elapsed_ms=elapsed_ms;
        if(cap) {
            action->arg0=wu_map(s->parameter0,cap->min0,cap->max0,cap->step0);
            action->arg1=wu_map(s->parameter1,cap->min1,cap->max1,cap->step1);
        }
        if(kind==WF_INSERT||(cap&&(cap->flags&WU_CAP_TEXT))) {
            action->text=s->text;action->text_length=s->text_length;
        }
        s->last_kind=kind;s->last_ref=ref;
        return 1;
    }
    unsigned controls=wu_controls(v,c);
    if(choice<WU_SET1) {if(!(controls&1))return -1;s->parameter0=choice-WU_SET0;return 0;}
    if(choice<WU_LITERAL) {if(!(controls&2))return -1;s->parameter1=choice-WU_SET1;return 0;}
    if(!(controls&4))return -1;
    if(choice<WU_TOKEN) {
        s->text[0]=(char)(32+choice-WU_LITERAL);s->text[1]=0;s->text_length=1;return 0;
    }
    if(choice<WU_VALUE) {
        WUString tokens[WU_COPY_TOKENS];unsigned count=wu_tokens(v,tokens);
        unsigned index=choice-WU_TOKEN;
        return index<count?wu_copy(s,tokens[index]):-1;
    }
    if(choice<WU_INSTRUCTION) {
        unsigned index=choice-WU_VALUE;
        return index<v->count?wu_copy(s,wu_value(v,index)):-1;
    }
    return wu_copy(s,wu_string(v,v->instruction));
}
