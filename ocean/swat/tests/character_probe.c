// Read-only diagnostic output: exact sampled node matrices/geometry for an
// independent numerical oracle. Licensed source assets are supplied locally.
#include "character_asset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char** argv) {
    if(argc!=5) { fprintf(stderr,"usage: character_probe ASSET.glb TIMES.txt OUTPUT.bin INFLUENCE_LIMIT\n"); return 2; }
    char error[256]; SwatCharacterAsset* asset=swat_character_load(argv[1],atoi(argv[4]),error,sizeof(error));
    if(!asset) { fprintf(stderr,"Character rejected: %s\n",error); return 1; }
    SwatArtInfo info=swat_character_info(asset); const char* clip=swat_character_clip_name(asset,0);
    printf("CHARACTER nodes=%d skins=%d joints=%d meshes=%d vertices=%d triangles=%d max_influences=%d vertices_over_four=%d clips=%d duration=%.9g\n",
        info.nodes,info.skins,info.joints,info.meshes,info.vertices,info.triangles,info.max_influences,info.vertices_over_four,info.clips,swat_character_clip_duration(asset,0));
    FILE* times=fopen(argv[2],"rb"),*output=fopen(argv[3],"wb"); bool ok=times && output;
    double t; int samples=0;
    while(ok && fscanf(times,"%lf",&t)==1) {
        ok=swat_character_sample(asset,t<0 ? NULL : clip,t);
        for(int node=0;ok && node<info.nodes;node++) ok=fwrite(swat_character_node_matrix(asset,node),sizeof(float),16,output)==16;
        for(int mesh=0;ok && mesh<info.meshes;mesh++) {
            const SwatArtMesh* m=swat_character_mesh(asset,mesh); float visible=m->visible ? 1 : 0;
            ok=fwrite(&visible,sizeof(float),1,output)==1 && fwrite(m->positions,sizeof(float),(size_t)m->vertices*3,output)==(size_t)m->vertices*3 &&
                fwrite(m->normals,sizeof(float),(size_t)m->vertices*3,output)==(size_t)m->vertices*3;
        }
        samples++;
    }
    if(times) { if(ferror(times)) ok=false; if(fclose(times)) ok=false; }
    if(output && fclose(output)) ok=false;
    swat_character_free(asset); printf("samples=%d\n",samples); return ok ? 0 : 1;
}
