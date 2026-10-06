// Headless connection probe used for native Windows/Linux interoperability.
#include "net.h"
#include "enet/enet.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char** argv) {
    if(argc!=3) { fprintf(stderr,"usage: net_probe ADDRESS PORT\n"); return 2; }
    char* end; long port=strtol(argv[2],&end,10);
    if(*end || port<1 || port>65535) return 2;
    SwatSim* replica=calloc(1,sizeof(*replica)); SwatNetClient* client=calloc(1,sizeof(*client));
    if(!replica || !client) { free(replica); free(client); return 1; }
    swat_sim_init(replica,swat_default_config(),42);
    if(!swat_client_open(client,replica,argv[1],(int)port)) {
        fprintf(stderr,"%s\n",client->error); swat_sim_close(replica); free(replica); free(client); return 1;
    }
    uint32_t start=enet_time_get();
    while(client->status==SWAT_NET_CONNECTING && enet_time_get()-start<7000) {
        swat_client_poll(client); enet_uint32 condition=ENET_SOCKET_WAIT_RECEIVE;
        enet_socket_wait(((ENetHost*)client->transport)->socket,&condition,1);
    }
    if(client->status!=SWAT_NET_ACTIVE) {
        fprintf(stderr,"FAIL connection: %s\n",client->error); swat_client_close(client); swat_sim_close(replica); free(client); free(replica); return 1;
    }
    int slot=client->slot; uint32_t previous=enet_time_get(); float accumulator=0; int commands=0;
    while(client->status==SWAT_NET_ACTIVE && commands<120 && enet_time_get()-start<12000) {
        swat_client_poll(client); uint32_t now=enet_time_get();
        accumulator+=fminf((float)(now-previous)*.001f,.1f); previous=now;
        while(accumulator>=SWAT_DT && commands<120) {
            SwatInput input=swat_neutral_input(); input.forward=commands<60; input.fire=commands==10;
            if(!swat_client_input(client,&input)) break;
            accumulator-=SWAT_DT; commands++;
        }
        enet_uint32 condition=ENET_SOCKET_WAIT_RECEIVE;
        enet_socket_wait(((ENetHost*)client->transport)->socket,&condition,1);
    }
    uint32_t wait=enet_time_get();
    while(client->status==SWAT_NET_ACTIVE && client->ack<120 && enet_time_get()-wait<2000) swat_client_poll(client);
    bool valid=client->status==SWAT_NET_ACTIVE && client->ack>=120;
    if(valid) {
        float observation[SWAT_OBS_SIZE]; swat_sim_observe(replica,client->actor,observation);
        for(int i=0;i<SWAT_OBS_SIZE;i++) valid=valid && isfinite(observation[i]);
        b3Pos feet=swat_body_feet_position(&replica->actors[client->actor].controller.body);
        valid=valid && feet.x>1.5f && replica->actors[client->actor].arsenal.shots==1;
        printf("%s net probe slot=%d actor=%d epoch=%u ack=%u tick=%d position=%.3f,%.3f,%.3f shots=%d sounds=%d ping=%d ms\n",
            valid ? "PASS" : "FAIL",slot,client->actor,client->epoch,client->ack,replica->tick,
            (double)feet.x,(double)feet.y,(double)feet.z,replica->actors[client->actor].arsenal.shots,replica->sounds.count,client->ping_ms);
    } else fprintf(stderr,"FAIL probe: %s ack=%u commands=%d\n",client->error,client->ack,commands);
    swat_client_close(client); swat_sim_close(replica); free(client); free(replica); return valid ? 0 : 1;
}
