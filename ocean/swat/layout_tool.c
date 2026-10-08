#include "sim.h"
#include "motel.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool integer(const char* value,uint32_t* result) {
    char* end; errno=0; unsigned long parsed=strtoul(value,&end,10);
    if(errno || !value[0] || *end || value[0]=='-' || parsed>UINT32_MAX) return false;
    *result=(uint32_t)parsed; return true;
}
static int tokens_from(const char* source,int tokens[SWAT_LAYOUT_TOKENS]) {
    int count=0; const char* at=source;
    while(*at && count<SWAT_LAYOUT_TOKENS) {
        char* end; long n=strtol(at,&end,10); if(end==at || n<0 || n>=swat_layout_categories()[count]) return -1;
        tokens[count++]=(int)n; if(!*end) return count; if(*end!=',') return -1; at=end+1;
        if(!*at || count==SWAT_LAYOUT_TOKENS) return -1;
    }
    return count;
}
static void print_plan(const SwatLayout* p) {
    printf("{\"version\":%d,\"seed\":%u,\"policy_id\":%u,\"fingerprint\":%u,\"difficulty\":%d,\"tokens\":[",
        SWAT_LAYOUT_VERSION,p->seed,p->policy_id,p->fingerprint,p->difficulty);
    for(int i=0;i<SWAT_LAYOUT_TOKENS;i++) printf("%s%d",i ? "," : "",p->tokens[i]);
    printf("],\"rooms\":%d,\"width\":%.1f,\"depth\":%.1f,\"furniture\":%d,\"path_length\":%.4f,\"quality\":%.6f,\"valid\":true}\n",
        p->room_count,p->width,p->depth,p->furniture_count,p->path_length,p->quality);
}
static int motel_walls(void) {
    static SwatWorld w;swat_world_init(&w);swat_motel_build(&w);
    const SwatMotelInstance* p=swat_motel_instance(13);int group=w.objects[14].wall_group;
    SwatHit hit=swat_world_ray(&w,(b3Pos){-6.7f,1,-7},swat_v(0,0,1),2,b3_nullBodyId);
    if(!hit.hit || !swat_world_breach(&w,hit.index,hit.point))return 1;
    printf("{\"format\":1,\"parent\":14,\"units\":\"metres\",\"source_origin\":[%.6f,%.6f,%.6f],\"source_yaw\":%.6f,\"source_scale\":[%.6f,%.6f,%.6f],\"sections\":[",(double)p->origin.x,(double)p->origin.y,(double)p->origin.z,p->yaw,p->scale.x,p->scale.y,p->scale.z);
    int count=0;for(int i=group-1;i<w.count && w.objects[i].wall_group==group;i++) {
        SwatObject* o=&w.objects[i];b3Vec3 d=b3SubPos(o->center,p->origin);float x=cosf(p->yaw)*d.x-sinf(p->yaw)*d.z;
        printf("%s{\"id\":%d,\"center_world\":[%.6f,%.6f,%.6f],\"half_wall_xyz\":[%.6f,%.6f,%.6f],\"yaw\":%.6f,\"source_xy_bounds\":[%.6f,%.6f,%.6f,%.6f],\"survives\":%s}",count++?",":"",i,(double)o->center.x,(double)o->center.y,(double)o->center.z,o->half.x,o->half.y,o->half.z,o->yaw,x-o->half.z,d.y-o->half.y,x+o->half.z,d.y+o->half.y,o->active?"true":"false");
    }
    printf("],\"edges\":[");count=0;
    for(int i=group-1;i<w.count && w.objects[i].wall_group==group;i++) {
        SwatMotelEdge edges[32];int n=swat_motel_wall_edges(&w,&w.objects[i],edges,32);
        for(int k=0;k<n;k++){SwatMotelEdge* e=&edges[k];printf("%s{\"owner\":%d,\"removed_neighbor\":%d,\"origin\":[%.6f,%.6f,%.6f],\"yaw\":%.6f,\"roll\":%.6f,\"length\":%.6f,\"depth\":%.6f}",count++?",":"",e->owner,e->neighbor,(double)e->origin.x,(double)e->origin.y,(double)e->origin.z,e->yaw,e->roll,e->length,e->depth);}
    }
    puts("]}");swat_world_close(&w);return 0;
}
int main(int argc,char** argv) {
    if(argc==2 && !strcmp(argv[1],"motel-walls"))return motel_walls();
    const char* command=argc>1 ? argv[1] : "";
    for(int i=2;i<argc;i++) if(!strcmp(argv[i],"--model")) {
        if(i+1>=argc || !swat_layout_load_policy(argv[i+1])) { fprintf(stderr,"Invalid layout model.\n"); return 2; }
        for(int j=i;j+2<argc;j++) argv[j]=argv[j+2];
        argc-=2; i--;
    }
    uint32_t first=0,second=0; int tokens[SWAT_LAYOUT_TOKENS]={0}; SwatLayout layout;
    if(!strcmp(command,"corpus") && (argc==4 || argc==5) && integer(argv[2],&first) && first>0 && first<=1000000 && integer(argv[3],&second)) {
        SwatGenerator generator=argc==5 && !strcmp(argv[4],"neural") ? SWAT_LAYOUT_NEURAL : SWAT_LAYOUT_UNIFORM;
        for(uint32_t i=0;i<first;i++) {
            if(!swat_layout_generate(&layout,second+i,(int)(i%3),generator)) return 1;
            print_plan(&layout);
        }
        return 0;
    }
    if(!strcmp(command,"batch") && argc==2) {
        char line[256],csv[128],extra; unsigned difficulty;
        while(fgets(line,sizeof(line),stdin)) {
            if(sscanf(line,"%127s %u %c",csv,&difficulty,&extra)!=2 || difficulty>2 || tokens_from(csv,tokens)!=SWAT_LAYOUT_TOKENS) return 2;
            if(swat_layout_plan(&layout,tokens,(int)difficulty)) print_plan(&layout);
            else puts("{\"valid\":false}");
        }
        return ferror(stdin) ? 1 : 0;
    }
    if(!strcmp(command,"sample") && (argc==4 || argc==5) && integer(argv[2],&first) && integer(argv[3],&second) && second<3) {
        SwatGenerator generator=argc==5 && !strcmp(argv[4],"uniform") ? SWAT_LAYOUT_UNIFORM : SWAT_LAYOUT_NEURAL;
        if(!swat_layout_generate(&layout,first,(int)second,generator)) return 1;
        print_plan(&layout); return 0;
    }
    if(!strcmp(command,"evaluate") && argc==4 && integer(argv[3],&second) && second<3 && tokens_from(argv[2],tokens)==SWAT_LAYOUT_TOKENS) {
        if(swat_layout_plan(&layout,tokens,(int)second)) print_plan(&layout);
        else puts("{\"valid\":false}");
        return 0;
    }
    if(!strcmp(command,"logits") && argc==5 && integer(argv[2],&first) && first<3 && integer(argv[3],&second) && second<SWAT_LAYOUT_TOKENS && tokens_from(argv[4],tokens)>=(int)second) {
        float logits[3]; swat_layout_logits(tokens,(int)second,(int)first,logits);
        printf("[%.9g,%.9g,%.9g]\n",logits[0],logits[1],logits[2]); return 0;
    }
    if(!strcmp(command,"check") && (argc==3 || argc==4) && integer(argv[2],&first) && first>0 && first<=10000) {
        static SwatSim sim; SwatConfig config=swat_default_config(); config.mission=SWAT_GENERATED; config.hostile_fire=false;
        config.generator=argc==4 && !strcmp(argv[3],"uniform") ? SWAT_LAYOUT_UNIFORM : SWAT_LAYOUT_NEURAL;
        int largest=0,histogram[3]={0}; float quality=0;
        for(uint32_t i=0;i<first;i++) {
            config.layout_seed=i+1; config.difficulty=(int)(i%3); swat_sim_init(&sim,config,1);
            if(sim.world.count>largest) largest=sim.world.count;
            histogram[sim.layout.room_count-3]++; quality+=sim.layout.quality;
            for(int actor=0;actor<sim.actor_count;actor++) if(sim.actors[actor].present) {
                b3Pos feet=swat_body_feet_position(&sim.actors[actor].controller.body);
                if(!isfinite(feet.x) || feet.y<-.1f || feet.y>.2f) { fprintf(stderr,"Bad spawn: seed %u actor %d y %.3f\n",i+1,actor,(float)feet.y); return 1; }
            }
            swat_sim_close(&sim);
        }
        printf("{\"checked\":%u,\"maximum_objects\":%d,\"room_counts\":[%d,%d,%d],\"mean_bootstrap_quality\":%.6f}\n",first,largest,histogram[0],histogram[1],histogram[2],quality/first);
        return 0;
    }
    fprintf(stderr,"usage: layout_tool motel-walls | corpus COUNT SEED | sample SEED DIFFICULTY [uniform|neural] | evaluate TOKENS DIFFICULTY | logits DIFFICULTY STEP PREFIX | check COUNT [uniform|neural] [--model PATH]\n");
    return 2;
}
