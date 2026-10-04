#include "sim.h"
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
int main(int argc,char** argv) {
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
    fprintf(stderr,"usage: layout_tool corpus COUNT SEED | sample SEED DIFFICULTY [uniform|neural] | evaluate TOKENS DIFFICULTY | logits DIFFICULTY STEP PREFIX | check COUNT [uniform|neural] [--model PATH]\n");
    return 2;
}
