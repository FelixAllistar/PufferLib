#pragma once
#include "fpg_task.h"

static FpgCase* fpg_bank;
static int fpg_bank_count;
static uint64_t fpg_bank_hash;
static int fpg_integer(Dict* d,const char* key,int fallback,int lo,int hi) {
    DictItem* item=dict_find(d,key);double value=item?item->value:fallback;
    if(!isfinite(value)||value!=floor(value)||value<lo||value>hi) {
        fprintf(stderr,"mario_fpg: invalid %s\n",key);abort();
    }
    return (int)value;
}
static FpgConfig fpg_config(Dict* d) {
    FpgConfig c;
    c.max_frames=fpg_integer(d,"max_frames",240,1,2000);
    c.seed=fpg_integer(d,"seed",73,1,2147483647);
    c.split=fpg_integer(d,"split",0,0,2);
    c.adaptive=fpg_integer(d,"adaptive",1,0,1);
    c.fixed_tier=fpg_integer(d,"fixed_tier",-1,-1,3);
    c.augment=fpg_integer(d,"augment",1,0,1);
    // Missing means the archived v1 contract; active configs opt into v2.
    c.contract_version=fpg_integer(d,"contract_version",1,1,2);
    return c;
}
static void fpg_load_bank(Dict* d) {
    if(fpg_bank) return;
    DictItem* entry=dict_find(d,"reset_bank");
    const char* path=entry&&entry->str?entry->str:"ocean/mario_fpg/data/reset_bank.bin";
    FILE* f=fopen(path,"rb");uint32_t header[4];
    if(!f||fread(header,sizeof(header),1,f)!=1||header[0]!=0x46504731||header[1]!=FPG_VERSION
        ||header[2]<3||header[2]>768||header[3]!=sizeof(FpgCase)) {
        fprintf(stderr,"mario_fpg: invalid or missing reset bank %s\n",path);abort();
    }
    fpg_bank_count=(int)header[2];fpg_bank=(FpgCase*)calloc(header[2],sizeof(FpgCase));
    if(!fpg_bank||fread(fpg_bank,sizeof(FpgCase),header[2],f)!=header[2]||fgetc(f)!=EOF) abort();
    fclose(f);int splits[3]={};fpg_bank_hash=1469598103934665603ull;
    const unsigned char* bytes=(const unsigned char*)fpg_bank;
    for(size_t i=0;i<sizeof(FpgCase)*header[2];i++) {fpg_bank_hash^=bytes[i];fpg_bank_hash*=1099511628211ull;}
    for(int i=0;i<fpg_bank_count;i++) {
        const FpgCase* sample=&fpg_bank[i];
        if(sample->course.split<0||sample->course.split>2||sample->length<1||sample->length>FPG_PATH_MAX
            ||sample->course.pole<24||sample->course.pole>48||sample->course.height<3||sample->course.height>8
            ||sample->course.gap<1||sample->course.gap>12) abort();
        splits[sample->course.split]++;
        FpgWorld world;fpg_generate(&sample->course,&world);FpgBody b=fpg_initial(&sample->course);
        for(int t=0;t<sample->length;t++) {
            if(memcmp(&b,&sample->frames[t].body,sizeof(b))||sample->frames[t].action<0||sample->frames[t].action>=12) abort();
            fpg_physics(&b,&world,fpg_buttons(sample->frames[t].action));
            if(t+1<sample->length&&fpg_outcome(&b)!=FPG_ACTIVE) abort();
        }
        if(fpg_outcome(&b)!=FPG_SUCCESS) abort();
    }
    if(!splits[0]||!splits[1]||!splits[2]) abort();
    fprintf(stderr,"[mario_fpg] bank=%s cases=%d/%d/%d fingerprint=%016llx\n",path,splits[0],splits[1],splits[2],(unsigned long long)fpg_bank_hash);
}
