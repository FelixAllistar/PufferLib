// Production loader/selector, actual input, and real wire encoding; no device.
#ifndef SWAT_SOUND_VIEW_SOURCE
#define SWAT_SOUND_VIEW_SOURCE "../sound_view.c"
#endif
#include SWAT_SOUND_VIEW_SOURCE
#include "protocol.h"
#include "replay.h"
#include <assert.h>
static SwatSoundView view;
static SwatSim sim,replica;
static SwatMap map,decoded_map;
static SwatSnapshot state,other,decoded,unchanged;
static unsigned char first[SWAT_NET_PACKET_MAX],second[SWAT_NET_PACKET_MAX];
static const SwatSoundEvent* latest(SwatSoundKind kind,int actor) {
    for(int i=sim.sounds.count-1;i>=0;i--){const SwatSoundEvent* e=swat_sound_at(&sim.sounds,i);if(e->kind==kind && e->source_actor==actor)return e;}return NULL;
}
static SwatSoundEvent fire(void) {
    int before=sim.actors[0].arsenal.shots;
    for(int t=0;t<180 && sim.actors[0].arsenal.shots==before;t++){SwatInput in=swat_neutral_input();in.aim=true;in.fire=t%2==0;swat_sim_step(&sim,&in);}
    assert(sim.actors[0].arsenal.shots>before);const SwatSoundEvent* e=latest(SWAT_SOUND_SHOT,0);assert(e);return *e;
}
static void switch_to(int slot) {
    SwatInput in=swat_neutral_input();in.aim=true;in.weapon=slot+1;for(int t=0;t<30;t++)swat_sim_step(&sim,&in);assert(sim.actors[0].arsenal.active==slot);
}
static void selected(SwatSoundEvent e,int profile,const SwatSim* current) {
    swat_audio_init(&view.mixer,SWAT_AUDIO_RATE);SwatAcousticPath path={.bands={1,1,1},.gain=1,.direction={0,0,1}};
    SwatAudioVoice* voice=swat_audio_start(&view.mixer,e,path,swat_v(0,0,1));assert(voice);recording_start(&view,current,voice);assert(voice->recording);
    bool found=false;for(int i=0;i<view.clip_count;i++)if(view.clips[i].pcm==voice->recording){assert(view.clips[i].kind==(int)e.kind && view.clips[i].profile==profile);found=true;}assert(found);
}
static void wire_profile(SwatSoundEvent e) {
    swat_capture_map(&sim,1,&map);size_t n=swat_encode_map(first,sizeof(first),&map);assert(n && swat_decode_map(&decoded_map,first,n));swat_apply_map(&replica,&decoded_map);
    swat_capture_snapshot(&sim,1,&state);n=swat_encode_snapshot(first,sizeof(first),&state);assert(n && swat_decode_snapshot(&decoded,first,n) && swat_apply_snapshot(&replica,&decoded));
    bool found=false;for(int i=0;i<replica.sounds.count;i++){const SwatSoundEvent* copy=swat_sound_at(&replica.sounds,i);if(copy->id==e.id){assert(copy->profile==e.profile);selected(*copy,e.profile,&replica);found=true;}}assert(found);assert(replica.actors[0].arsenal.active==sim.actors[0].arsenal.active);
    state.sound_count=1;state.sounds[0]=e;state.sounds[0].kind=SWAT_SOUND_SHOT;state.sounds[0].profile=0;
    n=swat_encode_snapshot(first,sizeof(first),&state);assert(n && swat_decode_snapshot(&decoded,first,n));other=state;other.sounds[0].profile=1;assert(swat_encode_snapshot(second,sizeof(second),&other)==n);
    size_t at=0;int differences=0;for(size_t i=0;i<n;i++)if(first[i]!=second[i]){at=i;differences++;}assert(differences==1);memcpy(&unchanged,&decoded,sizeof(decoded));first[at]=2;assert(!swat_decode_snapshot(&decoded,first,n));assert(!memcmp(&unchanged,&decoded,sizeof(decoded)));
    other.sounds[0].profile=2;assert(!swat_encode_snapshot(second,sizeof(second),&other));
    other.sounds[0].kind=SWAT_SOUND_COMMAND;other.sounds[0].profile=SWAT_COMMAND_CUES;assert(!swat_encode_snapshot(second,sizeof(second),&other));
}
int main(int argc,char** argv) {
    assert(argc==2);setvbuf(stdout,NULL,_IONBF,0);SetTraceLogLevel(LOG_WARNING);
#ifdef _WIN32
    assert(!_putenv_s("SWAT_SOUND_ASSETS",argv[1]));
#else
    assert(!setenv("SWAT_SOUND_ASSETS",argv[1],1));
#endif
    recordings_open(&view);assert(view.clip_count==92);
    SwatConfig cfg=swat_default_config();cfg.mission=SWAT_MOTEL;cfg.randomize=cfg.hostile_fire=false;cfg.squad_bots=3;cfg.max_ticks=2000;swat_config_human(&cfg);swat_sim_init(&sim,cfg,81);
    for(int i=3;i<6;i++)sim.actors[i].mind.order=SWAT_ORDER_HOLD;
    SwatSoundEvent primary=fire();assert(primary.profile==0);switch_to(1);selected(primary,0,&sim);wire_profile(primary);
    printf("PROFILE_STATE primary %d %08x\n",sim.tick,swat_replay_digest(&sim));
    puts("PASS emitted primary profile: ordinary shot then sidearm switch retains its recorded primary family through snapshot/late replica; invalid profile bytes reject atomically");
    SwatSoundEvent sidearm=fire();assert(sidearm.profile==1);switch_to(0);selected(sidearm,1,&sim);sidearm.source_actor=-1;selected(sidearm,1,&sim);
    printf("PROFILE_STATE secondary %d %08x\n",sim.tick,swat_replay_digest(&sim));
    puts("PASS emitted sidearm profile: later primary selection and unavailable source actor cannot turn a prior pistol shot into a rifle sample");
    SwatInput in=swat_neutral_input();in.reload=true;swat_sim_step(&sim,&in);
    assert(sim.actors[0].arsenal.slots[0].reload_remaining>0);
    SwatSoundEvent reload=*latest(SWAT_SOUND_RELOAD,0);assert(reload.profile==0);
    in=swat_neutral_input();in.weapon=2;swat_sim_step(&sim,&in);
    assert(sim.actors[0].arsenal.active==1 && !sim.actors[0].arsenal.slots[0].reload_remaining);
    const SwatSoundEvent* cancelled=latest(SWAT_SOUND_RELOAD,0);assert(cancelled && cancelled->id>reload.id && cancelled->profile==0);
    const SwatSoundEvent* drawn=latest(SWAT_SOUND_HANDLE,0);assert(drawn && drawn->tick==sim.tick && drawn->profile==1);
    printf("PROFILE_STATE reload %d %08x\n",sim.tick,swat_replay_digest(&sim));
    puts("PASS reload cancellation: ordinary switch cancels rifle reload using its emitted rifle profile while drawing the new sidearm profile");
    in=swat_neutral_input();in.command=true;swat_sim_step(&sim,&in);assert(latest(SWAT_SOUND_COMMAND,0)->profile==SWAT_COMMAND_COMPLIANCE);
    in=swat_neutral_input();in.squad_order=SWAT_ORDER_HOLD;swat_sim_step(&sim,&in);assert(latest(SWAT_SOUND_COMMAND,0)->profile==SWAT_COMMAND_ORDER);
    bool acknowledged=false;for(int t=0;t<60&&!acknowledged;t++){swat_sim_step(&sim,&(SwatInput){0});for(int i=3;i<6;i++){const SwatSoundEvent* e=latest(SWAT_SOUND_COMMAND,i);if(e && e->profile==SWAT_COMMAND_ACK)acknowledged=true;}}assert(acknowledged);
    printf("PROFILE_STATE command %d %08x\n",sim.tick,swat_replay_digest(&sim));
    puts("PASS command profiles: real compliance input, squad order and subsequent officer acknowledgement emit distinct explicit cues");
    // Controlled PCM tests routing only; these are not production voice assets.
    static float tones[3][4]={{.1f,.2f,.1f,0},{.2f,.1f,.2f,0},{.3f,.1f,.3f,0}};SwatSoundView cues={0};cues.clip_count=3;
    for(int i=0;i<3;i++)cues.clips[i]=(SwatSoundClip){tones[i],4,SWAT_AUDIO_RATE,SWAT_SOUND_COMMAND,-1,i,1};
    SwatAcousticPath clear={.bands={1,1,1},.gain=1,.direction={0,0,1}};
    for(int i=0;i<3;i++){swat_audio_init(&cues.mixer,SWAT_AUDIO_RATE);SwatSoundEvent e={.id=(uint32_t)(100+i),.kind=SWAT_SOUND_COMMAND,.profile=(unsigned char)i};SwatAudioVoice* voice=swat_audio_start(&cues.mixer,e,clear,swat_v(0,0,1));recording_start(&cues,&sim,voice);assert(voice->recording==tones[i]);}
    puts("PASS command sample routing: compliance lines cannot substitute for squad orders or acknowledgements; controlled PCM makes no voice-quality claim");
    swat_sim_close(&replica);swat_sim_close(&sim);
    cfg=swat_default_config();cfg.mission=SWAT_RANGE;cfg.hostile_fire=false;swat_sim_init(&sim,cfg,11);
    assert(swat_device_deploy(&sim,0,SWAT_BALL));
    in=swat_neutral_input();in.device_control=true;in.device_unit=0;in.command=true;swat_sim_step(&sim,&in);
    const SwatSoundEvent* ball=latest(SWAT_SOUND_COMMAND,0);assert(ball && ball->profile==SWAT_COMMAND_COMPLIANCE);
    assert(b3Distance(ball->position,sim.devices[0].position)<.1f);
    printf("PROFILE_STATE device %d %08x\n",sim.tick,swat_replay_digest(&sim));
    puts("PASS communication ball: actual owner-controlled command keeps compliance cue and device source instead of becoming a squad order");
    for(int kind=0;kind<SWAT_SOUND_KINDS;kind++)assert(swat_sound_profile_valid((SwatSoundKind)kind,SWAT_SOUND_PROFILE_UNSPECIFIED));
    assert(!swat_sound_profile_valid((SwatSoundKind)SWAT_SOUND_KINDS,0) && !swat_sound_profile_valid(SWAT_SOUND_STEP,1));
    assert(!swat_sound_profile_valid(SWAT_SOUND_COMMAND,256) && !swat_sound_profile_valid(SWAT_SOUND_SHOT,256));
    puts("PASS profile bounds: unspecified legacy emitters remain explicit; unknown kinds, cross-kind and unrepresentable profiles reject");
    for(int i=0;i<view.clip_count;i++){UnloadWaveSamples(view.clips[i].pcm);}
    swat_sim_close(&sim);return 0;
}
