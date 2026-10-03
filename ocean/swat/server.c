// Standalone dedicated authority: no Raylib, display, audio device or GPU.
#include "net.h"
#include "enet/enet.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t stopped;
static void stop_server(int signal_number) { (void)signal_number; stopped=1; }
static bool number(const char* value,long lo,long hi,long* result) {
    char* end; errno=0; long n=strtol(value,&end,10);
    if(errno || end==value || *end || n<lo || n>hi) return false;
    *result=n; return true;
}
int main(int argc,char** argv) {
    int port=SWAT_DEFAULT_PORT,run_ticks=0; uint32_t seed=42;
    SwatConfig config=swat_default_config(); config.max_ticks=18000; config.mission=SWAT_HOUSE;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help")) {
            puts("SWAT dedicated server: --port 27474 --seed 42 --max-ticks 18000\n"
                 "  --mission house|annex  --hostile-fire 0|1  --randomize 0|1  --run-ticks N\n"
                 "Four player slots; first connected officer leads/restarts. Ctrl+C stops the server.");
            return 0;
        }
        if(!strcmp(argv[i],"--mission")) {
            if(++i>=argc || (strcmp(argv[i],"house") && strcmp(argv[i],"annex"))) { fprintf(stderr,"Invalid mission\n"); return 2; }
            config.mission=!strcmp(argv[i],"house") ? SWAT_HOUSE : SWAT_ANNEX; continue;
        }
        const char* key=argv[i]; long n;
        if(++i>=argc || !number(argv[i],0,3600000,&n)) {
            fprintf(stderr,"Invalid value for %s\n",key); return 2;
        }
        if(!strcmp(key,"--port") && n>=1 && n<=65535) port=(int)n;
        else if(!strcmp(key,"--seed") && n>0) seed=(uint32_t)n;
        else if(!strcmp(key,"--max-ticks") && n>0) config.max_ticks=(int)n;
        else if(!strcmp(key,"--hostile-fire") && n<=1) config.hostile_fire=n!=0;
        else if(!strcmp(key,"--randomize") && n<=1) config.randomize=n!=0;
        else if(!strcmp(key,"--run-ticks")) run_ticks=(int)n;
        else { fprintf(stderr,"Unknown or invalid option %s\n",key); return 2; }
    }
    SwatSim* sim=calloc(1,sizeof(*sim));
    SwatNetServer* server=calloc(1,sizeof(*server));
    if(!sim || !server) { free(sim); free(server); return 1; }
    swat_sim_init(sim,config,seed);
    if(!swat_server_open(server,sim,port,false)) {
        fprintf(stderr,"%s\n",server->error); swat_sim_close(sim); free(sim); free(server); return 1;
    }
    signal(SIGINT,stop_server); signal(SIGTERM,stop_server);
    printf("SWAT server ready UDP :%d players=4 tick_rate=60 snapshot_rate=30 seed=%u\n",port,seed); fflush(stdout);
    uint32_t previous=enet_time_get(); float accumulator=0;
    while(!stopped && (!run_ticks || server->pulses<(uint32_t)run_ticks)) {
        swat_server_poll(server);
        uint32_t now=enet_time_get();
        accumulator+=fminf((float)(now-previous)*0.001f,0.1f); previous=now;
        while(accumulator>=SWAT_DT) {
            swat_server_tick(server,NULL); accumulator-=SWAT_DT;
        }
        enet_uint32 condition=ENET_SOCKET_WAIT_RECEIVE;
        enet_socket_wait(((ENetHost*)server->transport)->socket,&condition,2);
    }
    printf("SWAT server stopped epoch=%u tick=%d players=%u\n",server->epoch,sim->tick,server->player_mask);
    swat_server_close(server); swat_sim_close(sim); free(server); free(sim); return 0;
}
