#pragma once
/* PufferLib CPU environment: one persistent source-engine process per batch. */
typedef float obs_t;
#include "pufferenv.h"
#ifndef PG9_LAYOUT_HEADER
#define PG9_LAYOUT_HEADER "worker_layout.h"
#endif
#include PG9_LAYOUT_HEADER
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <assert.h>
#define OBS_SIZE PG9_OBS
#define NUM_ATNS 1
#define ACT_SIZES {PG9_ACTIONS}
#ifndef PG9_MAX_GAMES
#define PG9_MAX_GAMES 16
#endif
#ifndef PG9_WORKER_SCRIPT
#define PG9_WORKER_SCRIPT "ocean/pokemon_gen9/showdown_worker.cjs"
#endif
#define PG9_ACTOR_BYTES (4 * PG9_OBS + 16)
#define PG9_GAME_BYTES (32 + 2 * PG9_ACTOR_BYTES)
static void pg9_configure(Ini *ini,const char *mode){
    (void)mode;
    int games=(int)puf_ini_get(ini,"env","games_per_worker");
    int agents=(int)puf_ini_get(ini,"vec","total_agents"),buffers=(int)puf_ini_get(ini,"vec","num_buffers");
    if(games<1||games>PG9_MAX_GAMES||buffers<1||agents<1||agents%(2*games*buffers)||
       puf_ini_get(ini,"vec","action_mask_size")!=PG9_ACTIONS||puf_ini_get(ini,"selfplay","enabled")!=0){
        fprintf(stderr,"Showdown: each buffer needs complete worker batches, action_mask_size=15, and selfplay.enabled=0 (both seats use current policy)\n");exit(1);
    }
    puf_ini_put(ini,"env.source_revision","9fb3a5b99f1a0bea17f495c5cc1bfe04fdd19c3e");
    char abi[16];snprintf(abi,sizeof abi,"%u",PG9_WORKER_ABI);puf_ini_put(ini,"env.worker_abi",abi);
#if PG9_WORKER_ABI == 2
    puf_ini_put(ini,"env.observation_schema",PG9_COMPACT_SCHEMA_HASH);
    puf_ini_put(ini,"env.execution_backend","source_continuations_native_numeric_batches");
#endif
}
#define PUF_CONFIGURE pg9_configure
struct Log { float perf,score,episode_length,battle_turns,draw_rate,choice_retries,n; };
struct Env {
    Log log;
    Agent agents[2 * PG9_MAX_GAMES];
    int num_agents,tag,boundary_reached,games,input,output;
    unsigned int rng;
    pid_t child;
    unsigned char *packet;
};
static void pg9_fail(const char *message) {
    fprintf(stderr,"Showdown CPU worker: %s (%s)\n",message,strerror(errno));exit(1);
}
static void pg9_read(int fd,void *data,size_t count) {
    unsigned char *p=(unsigned char*)data;
    while(count){ssize_t n=read(fd,p,count);if(n<0&&errno==EINTR)continue;
        if(n<=0)pg9_fail("worker closed its output");p+=n;count-=(size_t)n;}
}
static void pg9_write(int fd,const void *data,size_t count) {
    const unsigned char *p=(const unsigned char*)data;
    while(count){ssize_t n=write(fd,p,count);if(n<0&&errno==EINTR)continue;
        if(n<=0)pg9_fail("worker closed its input");p+=n;count-=(size_t)n;}
}
static float pg9_float(const unsigned char *p){float value;memcpy(&value,p,4);return value;}
void puf_init(Env *e,Dict *kwargs) {
    e->games=(int)dict_get(kwargs,"games_per_worker");
    if(e->games<1||e->games>PG9_MAX_GAMES)pg9_fail("games_per_worker exceeds the compiled batch capacity");
    e->num_agents=2*e->games;e->tag=e->boundary_reached=0;
    memset(&e->log,0,sizeof e->log);
    e->packet=(unsigned char*)malloc((size_t)e->games*PG9_GAME_BYTES);
    if(!e->packet)pg9_fail("allocation failed");
    int to_child[2],from_child[2];
    if(pipe(to_child)||pipe(from_child))pg9_fail("pipe creation failed");
    for(int i=0;i<2;i++){
        if(fcntl(to_child[i],F_SETFD,FD_CLOEXEC)||fcntl(from_child[i],F_SETFD,FD_CLOEXEC))pg9_fail("pipe flags failed");
    }
    posix_spawn_file_actions_t fa;
    if(posix_spawn_file_actions_init(&fa)||posix_spawn_file_actions_adddup2(&fa,to_child[0],0)||
       posix_spawn_file_actions_adddup2(&fa,from_child[1],1))pg9_fail("spawn file actions failed");
    char count[24],seed[24];snprintf(count,sizeof count,"%d",e->games);
    snprintf(seed,sizeof seed,"%u",e->rng*2654435761u+(unsigned)dict_get(kwargs,"seed"));
    const char *node=getenv("PG9_NODE");if(!node||!*node)node="node";
    char *args[]={(char*)node,(char*)PG9_WORKER_SCRIPT,count,seed,NULL};
    extern char **environ;
    int error=posix_spawnp(&e->child,node,&fa,NULL,args,environ);
    posix_spawn_file_actions_destroy(&fa);close(to_child[0]);close(from_child[1]);
    if(error){errno=error;pg9_fail("could not launch Node");}
    e->input=to_child[1];e->output=from_child[0];
    uint32_t hello[4];pg9_read(e->output,hello,sizeof hello);
    if(hello[0]!=0x39504750u||hello[1]!=PG9_WORKER_ABI||hello[2]!=PG9_OBS||hello[3]!=PG9_ACTIONS)
        pg9_fail("worker ABI/observation/action mismatch");
#if PG9_WORKER_ABI == 2
    char schema[64];pg9_read(e->output,schema,sizeof schema);
    if(memcmp(schema,PG9_COMPACT_SCHEMA_HASH,64))pg9_fail("worker observation schema mismatch");
#endif
    for(int i=0;i<e->num_agents;i++)e->agents[i].policy=0;
}
static void pg9_exchange(Env *e,unsigned op) {
    uint32_t request[1+2*PG9_MAX_GAMES]={0};request[0]=op;
    for(int i=0;i<e->num_agents;i++)if(op==2){
        float action=e->agents[i].actions[0];
        if(!isfinite(action)||action<0||action>=PG9_ACTIONS||floorf(action)!=action||
           !e->agents[i].action_mask[(unsigned)action])pg9_fail("learner supplied an unmasked action");
        request[1+i]=(uint32_t)action;
    }
    pg9_write(e->input,request,(size_t)(1+e->num_agents)*4);
    pg9_read(e->output,e->packet,(size_t)e->games*PG9_GAME_BYTES);
    for(int g=0;g<e->games;g++){
        const unsigned char *row=e->packet+(size_t)g*PG9_GAME_BYTES;
        float score=pg9_float(row),ended=pg9_float(row+4);
        if(ended){e->log.n++;e->log.score+=score;e->log.perf+=(score+1)*0.5f;
            e->log.episode_length+=pg9_float(row+12);e->log.battle_turns+=pg9_float(row+8);
            e->log.draw_rate+=score==0;e->log.choice_retries+=pg9_float(row+20);}
        for(int s=0;s<2;s++){
            Agent *a=e->agents+2*g+s;const unsigned char *actor=row+32+s*PG9_ACTOR_BYTES;
            memcpy(a->observations,actor,4*PG9_OBS);memcpy(a->action_mask,actor+4*PG9_OBS,PG9_ACTIONS);
            a->rewards[0]=ended?(s?-score:score):0;a->terminals[0]=ended;
        }
    }
}
void puf_reset(Env *e){pg9_exchange(e,1);}
void puf_step(Env *e){pg9_exchange(e,2);}
void puf_close(Env *e){
    close(e->input);close(e->output);int status;
    while(waitpid(e->child,&status,0)<0&&errno==EINTR){}
    free(e->packet);e->packet=NULL;
}
void puf_render(Env *e){(void)e;}
void puf_log(Log *log,Dict *out){
    const float n=log->n;if(!n)return;
    dict_set(out,"perf",log->perf/n);dict_set(out,"score",log->score/n);
    dict_set(out,"episode_length",log->episode_length/n);dict_set(out,"battle_turns",log->battle_turns/n);
    dict_set(out,"draw_rate",log->draw_rate/n);dict_set(out,"choice_retries",log->choice_retries/n);dict_set(out,"n",n);
}
