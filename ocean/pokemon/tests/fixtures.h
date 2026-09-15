#pragma once
#include "../pokemon_core.h"
static inline PKMon pk_test_catalog_mon(int set) {
    PKMon mon={0};mon.species=(uint8_t)pk_species(set);
    for(int m=0;m<4;m++)mon.moves[m]=(uint8_t)pk_set_move(set,m);
    return mon;
}
static inline int pk_test_matches(PKMon mon,int set) {
    PKMon reference=pk_test_catalog_mon(set);
    return !memcmp(&mon,&reference,sizeof(mon));
}
// Metadata-only checkpoint fixtures: binding tests must not depend on a user's
// old training artifacts. The simulator callback never reads weight bytes.
static char pk_test_native_root[128];
static void pk_test_native_cleanup(void) {
    char path[256];
    for(int i=0;i<3;i++) {
        snprintf(path,sizeof(path),"%s/member%d/config.ini",pk_test_native_root,i);unlink(path);
        snprintf(path,sizeof(path),"%s/member%d",pk_test_native_root,i);rmdir(path);
    }
    for(int n=2;n<=3;n++) {snprintf(path,sizeof(path),"%s/native%d.ini",pk_test_native_root,n);unlink(path);}
    rmdir(pk_test_native_root);
}
static const char* pk_test_native_manifest(int count) {
    static char manifests[2][256];
    if(!*pk_test_native_root) {
        snprintf(pk_test_native_root,sizeof(pk_test_native_root),"/tmp/pk-native-fixture-XXXXXX");assert(mkdtemp(pk_test_native_root));
        const char* teams[]={"species:65,128,143,113,103,145","species:124,121,128,143,113,103","species:124,91,65,128,143,113"};
        const char* leads[]={"65","124","124"};
        char path[256];
        for(int i=0;i<3;i++) {
            snprintf(path,sizeof(path),"%s/member%d",pk_test_native_root,i);assert(!mkdir(path,0700));
            snprintf(path,sizeof(path),"%s/member%d/config.ini",pk_test_native_root,i);
            FILE* f=fopen(path,"w");assert(f);
            fprintf(f,"[env]\nabi_version=3\npolicy_version=3\nrules_sha=%s\nlearner_team=%s\nlearner_lead=%s\n[policy]\nhidden_size=128\nnum_layers=2\n",PK_RULES_SHA,teams[i],leads[i]);
            assert(!fclose(f));
        }
        for(int n=2;n<=3;n++) {
            snprintf(manifests[n-2],sizeof(manifests[0]),"%s/native%d.ini",pk_test_native_root,n);
            FILE* f=fopen(manifests[n-2],"w");assert(f);
            fprintf(f,"[native]\nbanks=%d\nhidden_size=128\nnum_layers=2\nrules_sha=%s\n",n,PK_RULES_SHA);
            for(int i=0;i<n;i++)fprintf(f,"[bank.%d]\npath=%s/member%d/weights.bin\nteam=%s\nlead=%s\n",i,pk_test_native_root,i,teams[i],leads[i]);
            assert(!fclose(f));
        }
        atexit(pk_test_native_cleanup);
    }
    assert(count==2 || count==3);return manifests[count-2];
}
