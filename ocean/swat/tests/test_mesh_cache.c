// Shared motel collision meshes: four motel sims built concurrently from a
// cold cache agree on every mesh pointer, each cached motel asset mesh is
// byte-identical to a fresh Box3D build of the same input, closing one sim
// leaves the others' meshes intact, and the concurrently built sims step to
// the same replicated state as one built alone afterwards.
#include "replay.h"
#include "motel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIMS 4

static SwatSim* motel_sim(void) {
    SwatSim* s=calloc(1,sizeof(*s)); assert(s);
    SwatConfig config=swat_default_config(); config.mission=SWAT_MOTEL; config.tactical_rules=true; config.squad_bots=0;
    swat_sim_init(s,config,2718);
    return s;
}
static uint32_t run(SwatSim* s,int ticks) {
    SwatInput inputs[SWAT_MAX_ACTORS];
    for(int t=0;t<ticks;t++) { swat_sim_bot_inputs(s,inputs); swat_sim_step_inputs(s,inputs); }
    return swat_replay_digest(s);
}

int main(void) {
    SwatSim* sims[SIMS];
#pragma omp parallel for num_threads(SIMS) schedule(static,1)
    for(int i=0;i<SIMS;i++) sims[i]=motel_sim();
    const SwatWorld* w=&sims[0]->world;
    assert(w->motel && w->motel_contact_mesh_count>0);
    int contacts=w->motel_contact_mesh_count;
    for(int i=1;i<SIMS;i++) {
        const SwatWorld* o=&sims[i]->world;
        assert(!memcmp(w->motel_meshes,o->motel_meshes,sizeof(w->motel_meshes)));
        assert(!memcmp(w->fence_meshes,o->fence_meshes,sizeof(w->fence_meshes)));
        assert(!memcmp(w->surroundings_meshes,o->surroundings_meshes,sizeof(w->surroundings_meshes)));
        assert(!memcmp(w->ground_meshes,o->ground_meshes,sizeof(w->ground_meshes)));
        assert(o->motel_contact_mesh_count==w->motel_contact_mesh_count);
        assert(!memcmp(w->motel_contact_meshes,o->motel_contact_meshes,(size_t)w->motel_contact_mesh_count*sizeof(*w->motel_contact_meshes)));
    }
    // Rebuild each asset mesh exactly as swat_motel_bind_collision describes it.
    int compared=0;
    for(int i=0;i<SWAT_MOTEL_ASSETS;i++) {
        const SwatMotelAsset* a=swat_motel_asset(i); float units=i>=44 ? 100 : 1;
        b3Vec3* scaled=malloc((size_t)a->vertex_count*sizeof(*scaled)); assert(scaled);
        for(int v=0;v<a->vertex_count;v++) scaled[v]=swat_mul(a->vertices[v],units);
        b3MeshDef def={.vertices=scaled,.indices=a->indices,.vertexCount=a->vertex_count,.triangleCount=a->triangle_count,
            .weldVertices=true,.weldTolerance=units*1e-6f,.identifyEdges=true};
        b3MeshData* fresh=b3CreateMesh(&def,NULL,0); assert(fresh);
        assert(swat_shared_mesh(&def)==w->motel_meshes[i]);
        assert(fresh->byteCount==w->motel_meshes[i]->byteCount && !memcmp(fresh,w->motel_meshes[i],(size_t)fresh->byteCount));
        b3DestroyMesh(fresh); free(scaled); compared++;
    }
    swat_sim_close(sims[0]); free(sims[0]);
    uint32_t digest[SIMS]={0};
#pragma omp parallel for num_threads(SIMS-1) schedule(static,1)
    for(int i=1;i<SIMS;i++) digest[i]=run(sims[i],240);
    SwatSim* alone=motel_sim(); uint32_t expected=run(alone,240);
    for(int i=1;i<SIMS;i++) assert(digest[i]==expected);
    swat_sim_close(alone); free(alone);
    for(int i=1;i<SIMS;i++) { swat_sim_close(sims[i]); free(sims[i]); }
    printf("PASS mesh cache: %d concurrent cold motel builds share %d contact meshes, %d asset meshes byte-identical to fresh builds, equal 240-tick digests after closing one sim\n",
           SIMS,contacts,compared);
    return 0;
}
