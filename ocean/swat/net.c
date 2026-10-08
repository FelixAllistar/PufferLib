#include "net.h"
#include "enet/enet.h"
#include <stdio.h>
#include <string.h>

enum { CHANNEL_CONTROL,CHANNEL_INPUT,CHANNEL_STATE,CHANNELS };
enum { REASON_CLOSED=1,REASON_FULL,REASON_VERSION,REASON_INVALID,REASON_OVERFLOW };
static int net_references;
static bool net_acquire(void) {
    if(!net_references && enet_initialize()!=0) return false;
    net_references++; return true;
}
static void net_release(void) { if(net_references>0 && --net_references==0) enet_deinitialize(); }
const char* swat_disconnect_reason(unsigned int reason) {
    switch(reason) {
        case REASON_CLOSED: return "The host closed the session.";
        case REASON_FULL: return "This session already has four players.";
        case REASON_VERSION: return "Game network versions do not match.";
        case REASON_INVALID: return "Invalid game message received.";
        case REASON_OVERFLOW: return "Input queue exceeded its limit.";
        default: return "Connection lost.";
    }
}
static bool net_send(ENetPeer* peer,int channel,const void* bytes,size_t size,bool reliable) {
    if(!size) return false;
    ENetPacket* packet=enet_packet_create(bytes,size,reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if(!packet) return false;
    if(enet_peer_send(peer,(enet_uint8)channel,packet)!=0) { enet_packet_destroy(packet); return false; }
    return true;
}
int swat_server_leader(const SwatNetServer* server) {
    for(int i=0;i<SWAT_MAX_PLAYERS;i++) if(server->player_mask&(1u<<i)) return i;
    return 0;
}
static void server_capture(SwatNetServer* server,SwatSnapshot* state) {
    swat_capture_snapshot(server->sim,server->epoch,state);
    state->revision=++server->revision; state->player_mask=server->player_mask;
    state->leader_slot=swat_server_leader(server);
    for(int i=0;i<SWAT_MAX_PLAYERS;i++) state->ack[i]=server->slots[i].ack;
}
static void server_snapshot_to(SwatNetServer* server,ENetPeer* peer) {
    SwatSnapshot state; unsigned char bytes[SWAT_NET_PACKET_MAX];
    server_capture(server,&state);
    net_send(peer,CHANNEL_CONTROL,bytes,swat_encode_snapshot(bytes,sizeof(bytes),&state),true);
}
static void server_map_to(SwatNetServer* server,ENetPeer* peer) {
    SwatMap map; unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_map(server->sim,server->epoch,&map);
    net_send(peer,CHANNEL_CONTROL,bytes,swat_encode_map(bytes,sizeof(bytes),&map),true);
    server_snapshot_to(server,peer);
}
static void server_broadcast(SwatNetServer* server) {
    SwatSnapshot state; unsigned char bytes[SWAT_NET_PACKET_MAX];
    server_capture(server,&state);
    size_t size=swat_encode_snapshot(bytes,sizeof(bytes),&state);
    if(!size) return;
    ENetPacket* packet=enet_packet_create(bytes,size,ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);
    if(packet) enet_host_broadcast((ENetHost*)server->transport,CHANNEL_STATE,packet);
}
bool swat_server_open(SwatNetServer* server,SwatSim* sim,int port,bool local_player) {
    memset(server,0,sizeof(*server)); server->local_slot=local_player ? 0 : -1;
    if(port<1 || port>65535 || !net_acquire()) {
        snprintf(server->error,sizeof(server->error),"Could not initialize networking."); return false;
    }
    ENetAddress address={ENET_HOST_ANY,(enet_uint16)port};
    ENetHost* host=enet_host_create(&address,SWAT_MAX_PLAYERS+2,CHANNELS,0,0);
    if(!host) {
        net_release(); snprintf(server->error,sizeof(server->error),"Could not listen on UDP port %d.",port); return false;
    }
    host->maximumPacketSize=SWAT_NET_PACKET_MAX;
    host->maximumWaitingData=SWAT_NET_PACKET_MAX*8;
    server->transport=host; server->sim=sim; server->port=port; server->epoch=1;
    swat_sim_reset(sim);
    swat_sim_set_player(sim,0,local_player);
    server->player_mask=local_player ? 1 : 0;
    return true;
}
static void server_remove(SwatNetServer* server,int slot) {
    server->player_mask&=~(1u<<slot);
    swat_sim_set_player(server->sim,slot,false);
    memset(&server->slots[slot],0,sizeof(server->slots[slot]));
}
void swat_server_restart(SwatNetServer* server) {
    if(!server->transport) return;
    server->epoch++; server->revision=0;
    swat_sim_reset(server->sim);
    for(int i=0;i<SWAT_MAX_PLAYERS;i++) {
        swat_sim_set_player(server->sim,i,(server->player_mask&(1u<<i))!=0);
        server->slots[i].head=server->slots[i].count=0;
        server->slots[i].received=server->slots[i].ack=0;
        server->slots[i].held=swat_neutral_input();
    }
    for(int i=0;i<SWAT_MAX_PLAYERS;i++)
        if(server->slots[i].peer) server_map_to(server,(ENetPeer*)server->slots[i].peer);
    enet_host_flush((ENetHost*)server->transport);
}
void swat_server_scenario(SwatNetServer* server,const SwatConfig* config) {
    if(!server->transport || config->mission<0 || config->mission>=SWAT_MISSION_COUNT ||
       config->generator<0 || config->generator>=SWAT_GENERATORS || config->difficulty<0 || config->difficulty>2) return;
    server->sim->config.mission=config->mission;
    server->sim->config.layout_seed=config->layout_seed;
    server->sim->config.generator=config->generator;
    server->sim->config.difficulty=config->difficulty;
    swat_server_restart(server);
}
void swat_server_poll(SwatNetServer* server) {
    if(!server->transport) return;
    ENetHost* host=(ENetHost*)server->transport;
    ENetEvent event;
    for(int budget=0;budget<128 && enet_host_service(host,&event,0)>0;budget++) {
        if(event.type==ENET_EVENT_TYPE_CONNECT) {
            if(event.data!=SWAT_NET_MAGIC+SWAT_NET_VERSION) {
                enet_peer_disconnect(event.peer,REASON_VERSION); continue;
            }
            int slot=-1;
            for(int i=0;i<SWAT_MAX_PLAYERS;i++) if(!(server->player_mask&(1u<<i))) { slot=i; break; }
            if(slot<0) { enet_peer_disconnect(event.peer,REASON_FULL); continue; }
            SwatRemoteSlot* remote=&server->slots[slot]; memset(remote,0,sizeof(*remote));
            remote->peer=event.peer; remote->held=swat_neutral_input(); remote->last_receive_ms=enet_time_get();
            event.peer->data=remote; enet_peer_timeout(event.peer,8,2000,6000);
            server->player_mask|=1u<<slot; swat_sim_set_player(server->sim,slot,true);
            unsigned char bytes[32];
            net_send(event.peer,CHANNEL_CONTROL,bytes,swat_encode_control(bytes,sizeof(bytes),SWAT_MSG_WELCOME,server->epoch,slot),true);
            server_map_to(server,event.peer);
        } else if(event.type==ENET_EVENT_TYPE_DISCONNECT) {
            if(event.peer->data) {
                int slot=(int)((SwatRemoteSlot*)event.peer->data-server->slots);
                event.peer->data=NULL; server_remove(server,slot);
            }
        } else if(event.type==ENET_EVENT_TYPE_RECEIVE) {
            SwatRemoteSlot* remote=(SwatRemoteSlot*)event.peer->data;
            bool valid=remote!=NULL;
            SwatMessage type=swat_message_type(event.packet->data,event.packet->dataLength);
            if(valid && type==SWAT_MSG_INPUT && event.channelID==CHANNEL_INPUT) {
                SwatCommand command;
                valid=swat_decode_command(&command,event.packet->data,event.packet->dataLength);
                if(valid && command.epoch==server->epoch && command.sequence>remote->received) {
                    if(server->sim->end!=SWAT_RUNNING) {
                        remote->received=remote->ack=command.sequence; remote->count=0;
                        remote->last_receive_ms=enet_time_get();
                    } else if(remote->count==SWAT_INPUT_QUEUE) {
                        enet_peer_disconnect(event.peer,REASON_OVERFLOW);
                    } else {
                        int tail=(remote->head+remote->count)%SWAT_INPUT_QUEUE;
                        remote->queue[tail]=command; remote->count++;
                        remote->received=command.sequence; remote->last_receive_ms=enet_time_get();
                    }
                }
            } else if(valid && type==SWAT_MSG_RESTART && event.channelID==CHANNEL_CONTROL) {
                uint32_t epoch; int requested_slot;
                valid=swat_decode_control(event.packet->data,event.packet->dataLength,type,&epoch,&requested_slot);
                int actual_slot=(int)(remote-server->slots);
                if(valid && epoch==server->epoch && actual_slot==swat_server_leader(server)) swat_server_restart(server);
            } else if(valid && type==SWAT_MSG_SCENARIO && event.channelID==CHANNEL_CONTROL) {
                uint32_t epoch; SwatConfig config;
                valid=swat_decode_scenario(event.packet->data,event.packet->dataLength,&epoch,&config);
                int actual_slot=(int)(remote-server->slots);
                if(valid && epoch==server->epoch && actual_slot==swat_server_leader(server)) swat_server_scenario(server,&config);
            } else valid=false;
            enet_packet_destroy(event.packet);
            if(!valid) enet_peer_disconnect(event.peer,REASON_INVALID);
        }
    }
    enet_host_flush(host);
}
void swat_server_tick(SwatNetServer* server,const SwatInput* local) {
    if(!server->transport) return;
    if(server->player_mask && server->sim->end==SWAT_RUNNING) {
        SwatInput inputs[SWAT_MAX_ACTORS]; swat_sim_bot_inputs(server->sim,inputs);
        server->sim->commander_actor=swat_player_actor(swat_server_leader(server));
        for(int slot=0;slot<SWAT_MAX_PLAYERS;slot++) {
            if(!(server->player_mask&(1u<<slot))) continue;
            SwatInput input=swat_neutral_input();
            if(slot==server->local_slot && local) input=*local;
            else {
                SwatRemoteSlot* remote=&server->slots[slot];
                if(remote->count) {
                    SwatCommand* command=&remote->queue[remote->head]; remote->held=command->input; remote->ack=command->sequence;
                    remote->head=(remote->head+1)%SWAT_INPUT_QUEUE; remote->count--;
                } else if(enet_time_get()-remote->last_receive_ms>250) {
                    // Expire repeated holds, never an unconsumed command. A
                    // slow server frame must not acknowledge input it discards.
                    remote->held=swat_neutral_input();
                }
                input=remote->held; remote->held.yaw_delta=remote->held.pitch_delta=0;
            }
            inputs[swat_player_actor(slot)]=input;
        }
        swat_sim_step_inputs(server->sim,inputs);
    }
    if(++server->pulses%2==0) server_broadcast(server);
    enet_host_flush((ENetHost*)server->transport);
}
void swat_server_close(SwatNetServer* server) {
    if(server->transport) {
        ENetHost* host=(ENetHost*)server->transport;
        for(int i=0;i<SWAT_MAX_PLAYERS;i++) if(server->slots[i].peer)
            enet_peer_disconnect_now((ENetPeer*)server->slots[i].peer,REASON_CLOSED);
        enet_host_flush(host); enet_host_destroy(host); net_release();
    }
    memset(server,0,sizeof(*server));
}

bool swat_client_open(SwatNetClient* client,SwatSim* replica,const char* name,int port) {
    memset(client,0,sizeof(*client)); client->slot=client->actor=-1; client->replica=replica;
    if(port<1 || port>65535 || !name || !name[0] || !net_acquire()) {
        snprintf(client->error,sizeof(client->error),"Invalid server address or port."); return false;
    }
    ENetAddress address={0,(enet_uint16)port};
    if(enet_address_set_host(&address,name)!=0) {
        net_release(); snprintf(client->error,sizeof(client->error),"Could not resolve the server address."); return false;
    }
    ENetHost* host=enet_host_create(NULL,1,CHANNELS,0,0);
    if(!host) { net_release(); snprintf(client->error,sizeof(client->error),"Could not create a connection."); return false; }
    host->maximumPacketSize=SWAT_NET_PACKET_MAX; host->maximumWaitingData=SWAT_NET_PACKET_MAX*8;
    ENetPeer* peer=enet_host_connect(host,&address,CHANNELS,SWAT_NET_MAGIC+SWAT_NET_VERSION);
    if(!peer) { enet_host_destroy(host); net_release(); snprintf(client->error,sizeof(client->error),"Could not connect."); return false; }
    enet_peer_timeout(peer,8,2000,6000);
    client->transport=host; client->peer=peer; client->status=SWAT_NET_CONNECTING;
    client->started_ms=client->last_packet_ms=enet_time_get();
    enet_host_flush(host); return true;
}
static void client_fail(SwatNetClient* client,const char* reason) {
    client->status=SWAT_NET_FAILED;
    snprintf(client->error,sizeof(client->error),"%s",reason);
}
void swat_client_poll(SwatNetClient* client) {
    if(!client->transport || client->status==SWAT_NET_FAILED) return;
    ENetHost* host=(ENetHost*)client->transport; ENetEvent event;
    for(int budget=0;budget<128 && enet_host_service(host,&event,0)>0;budget++) {
        if(event.type==ENET_EVENT_TYPE_DISCONNECT) client_fail(client,swat_disconnect_reason(event.data));
        if(event.type==ENET_EVENT_TYPE_RECEIVE) {
            const void* bytes=event.packet->data; size_t size=event.packet->dataLength;
            SwatMessage type=swat_message_type(bytes,size); bool valid=true;
            client->last_packet_ms=enet_time_get();
            if(type==SWAT_MSG_WELCOME && event.channelID==CHANNEL_CONTROL) {
                uint32_t epoch; int slot;
                valid=swat_decode_control(bytes,size,type,&epoch,&slot);
                if(valid) { client->slot=slot; client->actor=swat_player_actor(slot); }
            } else if(type==SWAT_MSG_MAP && event.channelID==CHANNEL_CONTROL) {
                SwatMap map; valid=swat_decode_map(&map,bytes,size);
                if(valid) {
                    swat_apply_map(client->replica,&map); client->has_map=true;
                    client->epoch=map.epoch; client->revision=client->sequence=client->ack=0;
                    client->pending_head=client->pending_count=0;
                    client->pending_yaw=client->pending_pitch=0;
                    client->sound_floor=map.sound_floor;
                    client->started_ms=enet_time_get();
                    client->status=SWAT_NET_CONNECTING;
                }
            } else if(type==SWAT_MSG_SNAPSHOT) {
                SwatSnapshot state; valid=swat_decode_snapshot(&state,bytes,size);
                if(valid && client->has_map && state.epoch==client->epoch && state.revision>client->revision) {
                    valid=client->slot>=0 && swat_apply_snapshot(client->replica,&state);
                    if(valid) {
                        client->revision=state.revision; client->ack=state.ack[client->slot];
                        while(client->pending_count && client->pending[client->pending_head].sequence<=client->ack) {
                            client->pending_head=(client->pending_head+1)%128; client->pending_count--;
                        }
                        client->pending_yaw=client->pending_pitch=0;
                        for(int i=0;i<client->pending_count;i++) {
                            const SwatInput* in=&client->pending[(client->pending_head+i)%128].input;
                            client->pending_yaw+=in->yaw_delta; client->pending_pitch+=in->pitch_delta;
                        }
                        client->leader_slot=state.leader_slot; client->player_mask=state.player_mask;
                        client->status=SWAT_NET_ACTIVE;
                    }
                }
            } else valid=false;
            enet_packet_destroy(event.packet);
            if(!valid) client_fail(client,"The server sent incompatible game data.");
        }
    }
    client->ping_ms=(int)((ENetPeer*)client->peer)->roundTripTime;
    uint32_t now=enet_time_get();
    if(client->status==SWAT_NET_CONNECTING && now-client->started_ms>6000)
        client_fail(client,"Connection timed out. Check the address and UDP port.");
    if(client->status==SWAT_NET_ACTIVE && now-client->last_packet_ms>6000) client_fail(client,"Connection timed out.");
    enet_host_flush(host);
}
bool swat_client_input(SwatNetClient* client,const SwatInput* input) {
    if(client->status!=SWAT_NET_ACTIVE) return false;
    if(client->pending_count==128) { client_fail(client,"The server stopped acknowledging controls."); return false; }
    SwatCommand command={client->epoch,++client->sequence,*input};
    // Mouse movement can accumulate across render frames. The authority's
    // per-tick turn limits remain the same as offline controller limits.
    command.input.yaw_delta=swat_clamp(command.input.yaw_delta,-0.4f,0.4f);
    command.input.pitch_delta=swat_clamp(command.input.pitch_delta,-0.3f,0.3f);
    unsigned char bytes[64];
    bool sent=net_send((ENetPeer*)client->peer,CHANNEL_INPUT,bytes,swat_encode_command(bytes,sizeof(bytes),&command),true);
    if(sent) {
        client->pending[(client->pending_head+client->pending_count)%128]=command; client->pending_count++;
        client->pending_yaw+=command.input.yaw_delta; client->pending_pitch+=command.input.pitch_delta;
    }
    enet_host_flush((ENetHost*)client->transport); return sent;
}
void swat_client_restart(SwatNetClient* client) {
    if(client->status!=SWAT_NET_ACTIVE || client->slot!=client->leader_slot) return;
    unsigned char bytes[32];
    net_send((ENetPeer*)client->peer,CHANNEL_CONTROL,bytes,swat_encode_control(bytes,sizeof(bytes),SWAT_MSG_RESTART,client->epoch,0),true);
    enet_host_flush((ENetHost*)client->transport);
}
void swat_client_scenario(SwatNetClient* client,const SwatConfig* config) {
    if(client->status!=SWAT_NET_ACTIVE || client->slot!=client->leader_slot) return;
    unsigned char bytes[32];
    net_send((ENetPeer*)client->peer,CHANNEL_CONTROL,bytes,swat_encode_scenario(bytes,sizeof(bytes),client->epoch,config),true);
    enet_host_flush((ENetHost*)client->transport);
}
void swat_client_close(SwatNetClient* client) {
    if(client->transport) {
        ENetHost* host=(ENetHost*)client->transport;
        enet_peer_disconnect_now((ENetPeer*)client->peer,REASON_CLOSED);
        enet_host_flush(host); enet_host_destroy(host); net_release();
    }
    memset(client,0,sizeof(*client)); client->slot=client->actor=-1;
}
