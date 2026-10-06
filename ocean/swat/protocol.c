#include "protocol.h"
#include "motel.h"
#include "storefront.h"
#include <string.h>

typedef struct Writer { unsigned char* p; size_t left; bool ok; } Writer;
typedef struct Reader { const unsigned char* p; size_t left; bool ok; } Reader;
static void put8(Writer* w,unsigned int v) {
    if(!w->left) { w->ok=false; return; } *w->p++=(unsigned char)v; w->left--;
}
static void put32(Writer* w,uint32_t v) { for(int i=3;i>=0;i--) put8(w,v>>(8*i)); }
static void putf(Writer* w,float v) { uint32_t bits; memcpy(&bits,&v,4); put32(w,bits); }
static unsigned int get8(Reader* r) {
    if(!r->left) { r->ok=false; return 0; } r->left--; return *r->p++;
}
static uint32_t get32(Reader* r) { uint32_t v=0; for(int i=0;i<4;i++) v=(v<<8)|get8(r); return v; }
static float getf(Reader* r,float lo,float hi) {
    uint32_t bits=get32(r); float v; memcpy(&v,&bits,4);
    if(!isfinite(v) || v<lo || v>hi) r->ok=false;
    return v;
}
static int geti(Reader* r,int lo,int hi) {
    uint32_t bits=get32(r); int32_t v; memcpy(&v,&bits,4);
    if(v<lo || v>hi) r->ok=false;
    return v;
}
static void putpos(Writer* w,b3Pos p) { putf(w,(float)p.x); putf(w,(float)p.y); putf(w,(float)p.z); }
static b3Pos getpos(Reader* r) { b3Pos p; p.x=getf(r,-1000,1000); p.y=getf(r,-1000,1000); p.z=getf(r,-1000,1000); return p; }
static void putvec(Writer* w,b3Vec3 p) { putf(w,p.x); putf(w,p.y); putf(w,p.z); }
static b3Vec3 getvec(Reader* r,float limit) {
    b3Vec3 v; v.x=getf(r,-limit,limit); v.y=getf(r,-limit,limit); v.z=getf(r,-limit,limit); return v;
}
static void header(Writer* w,SwatMessage type,uint32_t epoch) {
    put32(w,SWAT_NET_MAGIC); put8(w,SWAT_NET_VERSION); put8(w,type); put8(w,0); put8(w,0); put32(w,epoch);
}
static bool read_header(Reader* r,SwatMessage type,uint32_t* epoch) {
    uint32_t magic=get32(r); unsigned int version=get8(r),kind=get8(r),a=get8(r),b=get8(r);
    *epoch=get32(r);
    if(magic!=SWAT_NET_MAGIC || version!=SWAT_NET_VERSION || kind!=(unsigned int)type || a || b || !*epoch) r->ok=false;
    return r->ok;
}
SwatMessage swat_message_type(const void* bytes,size_t size) {
    if(size<12 || size>SWAT_NET_PACKET_MAX) return 0;
    Reader r={bytes,size,true};
    if(get32(&r)!=SWAT_NET_MAGIC || get8(&r)!=SWAT_NET_VERSION) return 0;
    unsigned int kind=get8(&r);
    return kind>=SWAT_MSG_INPUT && kind<=SWAT_MSG_SCENARIO ? (SwatMessage)kind : 0;
}

size_t swat_encode_command(void* bytes,size_t size,const SwatCommand* c) {
    Writer w={bytes,size,true}; header(&w,SWAT_MSG_INPUT,c->epoch); put32(&w,c->sequence);
    const SwatInput* in=&c->input;
    putf(&w,in->forward); putf(&w,in->strafe); putf(&w,in->yaw_delta); putf(&w,in->pitch_delta); putf(&w,in->lean);
    put8(&w,in->gait); put8(&w,in->weapon);
    unsigned int flags=in->crouch | (in->jump<<1) | (in->aim<<2) | (in->fire<<3) |
                       (in->reload<<4) | (in->interact<<5) | (in->selector<<6);
    put8(&w,flags);
    put8(&w,in->inspect | (in->command<<1) | (in->melee<<2) | (in->taser<<3)); put8(&w,in->loadout);
    put8(&w,in->throwable);
    put8(&w,in->sniper_order); put8(&w,in->sniper_unit); put8(&w,in->sniper_post); put8(&w,in->sniper_rifle); put8(&w,in->sniper_control);
    put8(&w,in->door_tool);
    put8(&w,in->ready); put8(&w,in->cancel_reload); put8(&w,in->primary_profile);
    put8(&w,in->sight_profile); put8(&w,in->magazine_inventory); put8(&w,in->pepper_spray | (in->peek<<1));
    put8(&w,in->squad_order); put8(&w,in->squad_team); put8(&w,in->squad_queue | (in->squad_execute<<1));
    put8(&w,in->device_deploy); put8(&w,in->device_unit); put8(&w,in->device_control);
    return w.ok ? size-w.left : 0;
}
bool swat_decode_command(SwatCommand* c,const void* bytes,size_t size) {
    Reader r={bytes,size,true}; SwatCommand tmp={0};
    read_header(&r,SWAT_MSG_INPUT,&tmp.epoch); tmp.sequence=get32(&r);
    SwatInput* in=&tmp.input;
    in->forward=getf(&r,-1,1); in->strafe=getf(&r,-1,1);
    in->yaw_delta=getf(&r,-0.4f,0.4f); in->pitch_delta=getf(&r,-0.3f,0.3f); in->lean=getf(&r,-1,1);
    unsigned int gait=get8(&r),weapon=get8(&r),flags=get8(&r);
    if(gait>SWAT_SPRINT || weapon>2 || flags>127 || !tmp.sequence) r.ok=false;
    in->gait=(SwatGait)gait; in->weapon=(int)weapon;
    in->crouch=flags&1; in->jump=flags&2; in->aim=flags&4; in->fire=flags&8;
    in->reload=flags&16; in->interact=flags&32; in->selector=flags&64;
    unsigned int tools=get8(&r),loadout=get8(&r);
    if(tools>15 || loadout>SWAT_KIT_COUNT) r.ok=false;
    in->inspect=tools&1; in->command=tools&2; in->melee=tools&4; in->loadout=(int)loadout;
    in->taser=tools&8; in->throwable=(int)get8(&r); if(in->throwable>2) r.ok=false;
    in->sniper_order=(int)get8(&r); in->sniper_unit=(int)get8(&r); in->sniper_post=(int)get8(&r); in->sniper_rifle=(int)get8(&r);
    unsigned int control=get8(&r); in->sniper_control=control!=0;
    if(in->sniper_order>=SWAT_SNIPER_ORDERS || in->sniper_unit>=SWAT_SNIPERS || in->sniper_post>=3 || in->sniper_rifle>1 || control>1) r.ok=false;
    in->door_tool=(int)get8(&r); if(in->door_tool>=SWAT_DOOR_TOOLS) r.ok=false;
    in->ready=(int)get8(&r); unsigned int cancel=get8(&r); in->cancel_reload=cancel!=0;
    in->primary_profile=(int)get8(&r); in->sight_profile=(int)get8(&r);
    unsigned int magazines=get8(&r); in->magazine_inventory=magazines!=0;
    if(in->ready>=SWAT_READY_STATES || cancel>1 || magazines>1 || in->primary_profile>SWAT_WEAPON_PROFILES ||
       in->primary_profile==2 || in->sight_profile>SWAT_SIGHTS) r.ok=false;
    unsigned int interaction=get8(&r); if(interaction>3) r.ok=false;
    in->pepper_spray=interaction&1; in->peek=interaction&2;
    in->squad_order=(int)get8(&r); in->squad_team=(int)get8(&r); unsigned int orders=get8(&r);
    in->squad_queue=orders&1; in->squad_execute=orders&2;
    in->device_deploy=(int)get8(&r); in->device_unit=(int)get8(&r); unsigned int remote=get8(&r); in->device_control=remote!=0;
    if(in->device_deploy>SWAT_DEVICE_KINDS || in->device_unit>=SWAT_MAX_DEVICES || remote>1) r.ok=false;
    if(in->squad_order>=SWAT_SQUAD_ORDERS || in->squad_team>2 || orders>3) r.ok=false;
    if(!r.ok || r.left) return false;
    *c=tmp; return true;
}

size_t swat_encode_map(void* bytes,size_t size,const SwatMap* map) {
    Writer w={bytes,size,true}; header(&w,SWAT_MSG_MAP,map->epoch);
    put32(&w,map->sound_floor); put32(&w,(uint32_t)map->config.max_ticks);
    put8(&w,map->config.randomize); put8(&w,map->config.hostile_fire); put8(&w,map->config.tactical_rules); put8(&w,map->config.squad_bots);
    put8(&w,map->config.mission);
    put32(&w,map->config.layout_seed); put8(&w,map->config.generator); put8(&w,map->config.difficulty);
    putpos(&w,map->mission.staging); put8(&w,map->mission.overwatch_count);
    for(int i=0;i<map->mission.overwatch_count;i++) { putpos(&w,map->mission.overwatch[i].position); putpos(&w,map->mission.overwatch[i].target); }
    if(map->config.mission==SWAT_GENERATED) {
        put32(&w,map->layout_policy_id);
        for(int i=0;i<SWAT_LAYOUT_TOKENS;i++) put8(&w,map->layout_tokens[i]);
    }
    put32(&w,(uint32_t)map->count); putpos(&w,map->extraction);
    put8(&w,map->room_count);
    for(int i=0;i<map->room_count;i++) {
        putpos(&w,map->rooms[i].center); putvec(&w,map->rooms[i].half);
        put8(&w,map->rooms[i].surface); put8(&w,map->rooms[i].floor);
    }
    for(int i=0;i<map->count;i++) {
        const SwatMapObject* o=&map->objects[i];
        putpos(&w,o->center); putpos(&w,o->hinge); putvec(&w,o->half);
        putf(&w,o->yaw); putf(&w,o->max_health); put8(&w,o->material); put8(&w,o->door);
        putf(&w,o->closed_yaw); put8(&w,o->part); putf(&w,o->pitch);
        put32(&w,o->wall_group); put8(&w,o->fractured);
        if(o->fractured) for(int j=0;j<4;j++) for(int k=0;k<2;k++) putf(&w,o->corners[j][k]);
    }
    return w.ok ? size-w.left : 0;
}
bool swat_decode_map(SwatMap* map,const void* bytes,size_t size) {
    Reader r={bytes,size,true}; SwatMap tmp={0};
    read_header(&r,SWAT_MSG_MAP,&tmp.epoch); tmp.sound_floor=get32(&r);
    tmp.config.max_ticks=geti(&r,1,3600000);
    unsigned int randomize=get8(&r),fire=get8(&r);
    if(randomize>1 || fire>1) r.ok=false;
    tmp.config.randomize=randomize!=0; tmp.config.hostile_fire=fire!=0;
    unsigned int tactical=get8(&r); if(tactical>1) r.ok=false; tmp.config.tactical_rules=tactical!=0; tmp.config.squad_bots=(int)get8(&r); if(tmp.config.squad_bots>3) r.ok=false;
    tmp.config.mission=(int)get8(&r); if(tmp.config.mission>=SWAT_MISSION_COUNT) r.ok=false;
    tmp.config.layout_seed=get32(&r); tmp.config.generator=(int)get8(&r); tmp.config.difficulty=(int)get8(&r);
    if(tmp.config.generator>=SWAT_GENERATORS || tmp.config.difficulty>2 || !r.ok) return false;
    tmp.mission=*swat_mission(tmp.config.mission); tmp.mission.staging=getpos(&r);
    tmp.mission.overwatch_count=(int)get8(&r); if(tmp.mission.overwatch_count>3) return false;
    for(int i=0;i<tmp.mission.overwatch_count;i++) { tmp.mission.overwatch[i].position=getpos(&r); tmp.mission.overwatch[i].target=getpos(&r); }
    if(tmp.config.mission==SWAT_GENERATED) {
        tmp.layout_policy_id=get32(&r);
        for(int i=0;i<SWAT_LAYOUT_TOKENS;i++) tmp.layout_tokens[i]=(int)get8(&r);
        SwatLayout layout;
        if(!r.ok || !swat_layout_plan(&layout,tmp.layout_tokens,tmp.config.difficulty)) return false;
    }
    tmp.count=geti(&r,1,SWAT_MAX_OBJECTS); tmp.extraction=getpos(&r);
    tmp.mission.extraction=tmp.extraction;
    tmp.room_count=(int)get8(&r); if(tmp.room_count>SWAT_MAX_ROOMS) return false;
    for(int i=0;i<tmp.room_count;i++) {
        SwatRoom* room=&tmp.rooms[i]; room->center=getpos(&r); room->half=getvec(&r,100);
        unsigned int surface=get8(&r),floor=get8(&r);
        if(surface>=SWAT_MATERIAL_COUNT || floor>=SWAT_MATERIAL_COUNT || room->half.x<=0 || room->half.y<=0 || room->half.z<=0) r.ok=false;
        room->surface=(SwatMaterial)surface; room->floor=(SwatMaterial)floor;
    }
    if(!r.ok) return false;
    for(int i=0;i<tmp.count;i++) {
        SwatMapObject* o=&tmp.objects[i];
        o->center=getpos(&r); o->hinge=getpos(&r); o->half=getvec(&r,100);
        o->yaw=getf(&r,-SWAT_PI,SWAT_PI); o->max_health=getf(&r,0,1000000);
        unsigned int material=get8(&r),door=get8(&r);
        if(material>=SWAT_MATERIAL_COUNT || door>1 || o->half.x<=0 || o->half.y<=0 || o->half.z<=0) r.ok=false;
        o->material=(SwatMaterial)material; o->door=door!=0;
        o->closed_yaw=getf(&r,-SWAT_PI,SWAT_PI); unsigned int part=get8(&r);
        if(part>SWAT_PART_LIGHT) r.ok=false;
        o->part=(SwatPart)part; o->pitch=getf(&r,-SWAT_PI,SWAT_PI);
        o->wall_group=geti(&r,0,SWAT_MAX_OBJECTS); unsigned int fractured=get8(&r);
        if(fractured>1 || (fractured && (o->door || o->part!=SWAT_PART_SKIN))) r.ok=false;
        o->fractured=fractured!=0;
        if(o->fractured) {
            for(int j=0;j<4;j++) { o->corners[j][0]=getf(&r,-o->half.y-.001f,o->half.y+.001f); o->corners[j][1]=getf(&r,-o->half.z-.001f,o->half.z+.001f); }
            for(int j=0;j<4;j++) {
                int k=(j+1)%4,l=(j+2)%4;
                float ay=o->corners[k][0]-o->corners[j][0],az=o->corners[k][1]-o->corners[j][1];
                float by=o->corners[l][0]-o->corners[k][0],bz=o->corners[l][1]-o->corners[k][1];
                if(ay*bz-az*by<.001f) r.ok=false;
            }
        }
    }
    if(!r.ok || r.left) return false;
    *map=tmp; return true;
}

static void putweapon(Writer* w,const SwatWeapon* weapon) {
    put32(w,(uint32_t)weapon->magazine); put32(w,(uint32_t)weapon->reserve);
    put32(w,(uint32_t)weapon->cooldown); put32(w,(uint32_t)weapon->reload_remaining);
    put32(w,(uint32_t)weapon->reload_duration); put8(w,weapon->chambered); put8(w,weapon->mode);
    put8(w,weapon->reload_stage); put8(w,weapon->magazine_seated); put8(w,weapon->use_magazines);
    put8(w,weapon->magazine_count);
    for(int i=0;i<SWAT_MAGAZINES;i++) put32(w,weapon->magazines[i]);
}
static SwatWeapon getweapon(Reader* r,int slot) {
    SwatWeapon w={0};
    w.magazine=geti(r,0,swat_weapon_def(slot)->capacity); w.reserve=geti(r,0,1000000);
    w.cooldown=geti(r,0,10000); w.reload_remaining=geti(r,0,10000); w.reload_duration=geti(r,0,10000);
    unsigned int chamber=get8(r),mode=get8(r);
    if(chamber>1 || mode>SWAT_AUTO || (!swat_weapon_def(slot)->automatic && mode==SWAT_AUTO)) r->ok=false;
    w.chambered=chamber!=0; w.mode=(SwatFireMode)mode;
    w.reload_stage=(SwatReloadStage)get8(r); unsigned int seated=get8(r),inventory=get8(r);
    w.magazine_seated=seated!=0; w.use_magazines=inventory!=0; w.magazine_count=(int)get8(r);
    if(w.reload_stage>=SWAT_RELOAD_STAGES || seated>1 || inventory>1 || w.magazine_count>SWAT_MAGAZINES ||
       w.reload_remaining>w.reload_duration || (!w.reload_remaining && w.reload_stage!=SWAT_RELOAD_IDLE)) r->ok=false;
    for(int i=0;i<SWAT_MAGAZINES;i++) {
        w.magazines[i]=geti(r,0,swat_weapon_def(slot)->capacity);
        if(i>=w.magazine_count && w.magazines[i]) r->ok=false;
    }
    return w;
}
static void putactor(Writer* w,const SwatActorState* a) {
    unsigned int flags=a->present | (a->alive<<1) | (a->crouched<<2) | (a->grounded<<3) |
                       (a->sprinting<<4) | (a->muzzle_blocked<<5);
    put8(w,flags); if(!a->present) return;
    put8(w,a->mind.bot | (a->rescued<<1)); put8(w,a->mind.state); put8(w,a->mind.team);
    put8(w,a->mind.order); put8(w,a->mind.pending_order); put8(w,a->mind.queued); put8(w,a->mind.escort_owner+1);
    put8(w,a->role); putpos(w,a->position); putvec(w,a->velocity); putvec(w,a->upper_offset);
    putf(w,a->health); putf(w,a->yaw); putf(w,a->pitch); putf(w,a->ads); putf(w,a->stamina);
    putf(w,a->eye_height); putf(w,a->recoil_pitch); putf(w,a->recoil_yaw); putf(w,a->lean); putf(w,a->ready_blend); put8(w,a->ready);
    put32(w,(uint32_t)a->last_shot_tick); putpos(w,a->tracer_start); putpos(w,a->tracer_end);
    put8(w,a->arsenal.active); put32(w,(uint32_t)a->arsenal.equip_remaining); put32(w,(uint32_t)a->arsenal.shots);
    put8(w,a->arsenal.last_fire | (a->arsenal.last_reload<<1) | (a->arsenal.last_selector<<2));
    put8(w,a->arsenal.primary); put8(w,a->arsenal.sight); put8(w,a->arsenal.fire_buffer_ticks); put8(w,a->arsenal.reload_buffer_ticks);
    for(int i=0;i<2;i++) putweapon(w,&a->arsenal.slots[i]);
    const SwatEquipment* g=&a->gear;
    put8(w,g->kit); put32(w,(uint32_t)g->stunned_ticks); put32(w,(uint32_t)g->melee_cooldown);
    put32(w,(uint32_t)g->cuff_ticks); put32(w,(uint32_t)g->cuff_target);
    put8(w,g->surrendered | (g->restrained<<1) | (g->inspecting<<2) | (g->last_command<<3) | (g->last_melee<<4) |
        (g->last_throw<<5) | (g->last_taser<<6) | (g->used_tools<<7));
    for(int i=0;i<SWAT_HIT_REGIONS;i++) putf(w,g->wounds[i]);
    put32(w,g->flash_ticks); put32(w,g->gas_ticks); put32(w,g->taser_cooldown); put32(w,g->throw_cooldown);
    put8(w,g->flashbangs); put8(w,g->gas_grenades); put8(w,g->taser_charges);
    put8(w,g->wand_mode); putf(w,g->wand_yaw); putf(w,g->wand_pitch);
    put8(w,g->breaching_charges); put32(w,g->door_ticks); put32(w,(uint32_t)g->door_target);
    put8(w,g->door_mode); put8(w,g->last_door_tool);
    put8(w,g->wedges); put32(w,g->spray_ticks); put8(w,g->door_completed);
    for(int i=0;i<SWAT_DEVICE_KINDS;i++) put8(w,g->devices[i]);
    put8(w,g->last_device); put32(w,g->tether_ticks);
}
static SwatActorState getactor(Reader* r) {
    SwatActorState a={0}; unsigned int flags=get8(r);
    if(flags>63 || (!(flags&1) && flags)) r->ok=false;
    a.present=flags&1; if(!a.present) return a;
    a.alive=flags&2; a.crouched=flags&4; a.grounded=flags&8; a.sprinting=flags&16; a.muzzle_blocked=flags&32;
    unsigned int behavior_flags=get8(r); a.mind.bot=behavior_flags&1; a.rescued=behavior_flags&2;
    a.mind.state=(SwatBehavior)get8(r); a.mind.team=(int)get8(r); a.mind.order=(int)get8(r); a.mind.pending_order=(int)get8(r);
    unsigned int queued=get8(r); a.mind.queued=queued!=0; a.mind.escort_owner=(int)get8(r)-1;
    if(behavior_flags>3 || a.mind.state>SWAT_DETAINED || a.mind.team>2 || a.mind.order>=SWAT_SQUAD_ORDERS || a.mind.pending_order>=SWAT_SQUAD_ORDERS || queued>1 || a.mind.escort_owner>=SWAT_MAX_ACTORS) r->ok=false;
    unsigned int role=get8(r); if(role>SWAT_SNIPER) r->ok=false; a.role=(SwatRole)role;
    a.position=getpos(r); a.velocity=getvec(r,100); a.upper_offset=getvec(r,SWAT_MAX_LEAN+0.01f);
    a.health=getf(r,0,100); a.yaw=getf(r,-SWAT_PI,SWAT_PI); a.pitch=getf(r,-85*SWAT_RAD,85*SWAT_RAD);
    a.ads=getf(r,0,1); a.stamina=getf(r,0,1); a.eye_height=getf(r,0,2);
    a.recoil_pitch=getf(r,0,12*SWAT_RAD); a.recoil_yaw=getf(r,-6*SWAT_RAD,6*SWAT_RAD); a.lean=getf(r,-1,1); a.ready_blend=getf(r,-1,1); a.ready=(SwatReady)get8(r);
    if(a.ready>=SWAT_READY_STATES) r->ok=false;
    a.last_shot_tick=geti(r,-100,3600000); a.tracer_start=getpos(r); a.tracer_end=getpos(r);
    unsigned int active=get8(r); if(active>1) r->ok=false; a.arsenal.active=(int)active;
    a.arsenal.equip_remaining=geti(r,0,10000); a.arsenal.shots=geti(r,0,1000000);
    unsigned int arsenal_flags=get8(r); if(arsenal_flags>7) r->ok=false;
    a.arsenal.last_fire=arsenal_flags&1; a.arsenal.last_reload=arsenal_flags&2; a.arsenal.last_selector=arsenal_flags&4;
    a.arsenal.primary=(int)get8(r); if(a.arsenal.primary==1 || a.arsenal.primary>=SWAT_WEAPON_PROFILES) r->ok=false;
    a.arsenal.sight=(int)get8(r); a.arsenal.fire_buffer_ticks=(int)get8(r); a.arsenal.reload_buffer_ticks=(int)get8(r);
    if(a.arsenal.sight>=SWAT_SIGHTS || a.arsenal.fire_buffer_ticks>12 || a.arsenal.reload_buffer_ticks>12) r->ok=false;
    for(int i=0;i<2;i++) a.arsenal.slots[i]=getweapon(r,i==0 ? a.arsenal.primary : 1);
    SwatEquipment* g=&a.gear; g->kit=(int)get8(r); if(g->kit>=SWAT_KIT_COUNT) r->ok=false;
    g->stunned_ticks=geti(r,0,10000); g->melee_cooldown=geti(r,0,1000);
    g->cuff_ticks=geti(r,0,72); g->cuff_target=geti(r,-1,SWAT_MAX_ACTORS-1);
    unsigned int equipment_flags=get8(r);
    g->surrendered=equipment_flags&1; g->restrained=equipment_flags&2; g->inspecting=equipment_flags&4;
    g->last_command=equipment_flags&8; g->last_melee=equipment_flags&16;
    g->last_throw=equipment_flags&32; g->last_taser=equipment_flags&64; g->used_tools=equipment_flags&128;
    for(int i=0;i<SWAT_HIT_REGIONS;i++) g->wounds[i]=getf(r,0,100);
    g->flash_ticks=geti(r,0,240); g->gas_ticks=geti(r,0,180);
    g->taser_cooldown=geti(r,0,180); g->throw_cooldown=geti(r,0,45);
    g->flashbangs=(int)get8(r); g->gas_grenades=(int)get8(r); g->taser_charges=(int)get8(r);
    if(g->flashbangs>2 || g->gas_grenades>2 || g->taser_charges>3) r->ok=false;
    g->wand_mode=(SwatWandMode)get8(r); if(g->wand_mode>=SWAT_WAND_MODES) r->ok=false;
    g->wand_yaw=getf(r,-110*SWAT_RAD,110*SWAT_RAD); g->wand_pitch=getf(r,-80*SWAT_RAD,80*SWAT_RAD);
    g->breaching_charges=(int)get8(r); g->door_ticks=geti(r,0,180); g->door_target=geti(r,-1,SWAT_MAX_OBJECTS-1);
    g->door_mode=(int)get8(r); g->last_door_tool=(int)get8(r);
    if(g->breaching_charges>2 || g->door_mode>=SWAT_DOOR_TOOLS || g->last_door_tool>=SWAT_DOOR_TOOLS) r->ok=false;
    g->wedges=(int)get8(r); g->spray_ticks=geti(r,0,360); unsigned int completed=get8(r);
    if(g->wedges>2 || completed>1) r->ok=false;
    g->door_completed=completed!=0;
    for(int i=0;i<SWAT_DEVICE_KINDS;i++) { g->devices[i]=(int)get8(r); if(g->devices[i]>1) r->ok=false; }
    unsigned int device=get8(r); if(device>1) r->ok=false; g->last_device=device!=0;
    g->tether_ticks=geti(r,0,180);
    if(a.alive!=(a.health>0)) r->ok=false;
    return a;
}
static void putevents(Writer* w,const SwatEvents* e) {
    putf(w,e->hostile_damage); putf(w,e->civilian_damage); putf(w,e->officer_damage);
    put32(w,(uint32_t)e->shots); put32(w,(uint32_t)e->destroyed); put32(w,(uint32_t)e->hostile_down);
}
static SwatEvents getevents(Reader* r) {
    SwatEvents e={0}; e.hostile_damage=getf(r,0,1000000); e.civilian_damage=getf(r,0,1000000); e.officer_damage=getf(r,0,1000000);
    e.shots=geti(r,0,1000000); e.destroyed=geti(r,0,SWAT_MAX_OBJECTS); e.hostile_down=geti(r,0,SWAT_MAX_ACTORS); return e;
}
size_t swat_encode_snapshot(void* bytes,size_t size,const SwatSnapshot* state) {
    Writer w={bytes,size,true}; header(&w,SWAT_MSG_SNAPSHOT,state->epoch);
    put32(&w,state->revision); put32(&w,(uint32_t)state->tick); put32(&w,(uint32_t)state->actor_count); put32(&w,(uint32_t)state->object_count);
    put32(&w,(uint32_t)state->generation); put8(&w,state->end); put8(&w,state->leader_slot); put8(&w,state->player_mask);
    for(int i=0;i<SWAT_MAX_PLAYERS;i++) put32(&w,state->ack[i]);
    putevents(&w,&state->totals);
    put32(&w,state->debrief.roe_violations); put32(&w,state->debrief.arrests); put32(&w,state->debrief.rescued); put32(&w,state->debrief.evidence); putf(&w,state->debrief.unlawful_damage);
    for(int i=0;i<SWAT_MAX_ACTORS;i++) {
        const SwatEvidence* e=&state->evidence[i]; put8(&w,e->dropped | (e->collected<<1)); if(e->dropped) { put8(&w,e->actor); putpos(&w,e->position); }
    }
    for(int i=0;i<SWAT_MAX_ACTORS;i++) putactor(&w,&state->actors[i]);
    for(int i=0;i<state->object_count;i++) {
        const SwatObjectState* o=&state->objects[i];
        put8(&w,o->active | (o->door_open<<1) | (o->locked<<2)); putf(&w,o->health); putf(&w,o->door_angle);
        put8(&w,o->breach_owner+1); put8(&w,o->breach_ticks);
        put8(&w,o->wedge_owner+1); put8(&w,o->peek | (o->trapped<<1)); put32(&w,o->trap_known); putpos(&w,o->breach_position);
    }
    put8(&w,state->sound_count);
    for(int i=0;i<state->sound_count;i++) {
        const SwatSoundEvent* e=&state->sounds[i];
        put32(&w,e->id); put32(&w,(uint32_t)e->tick); put32(&w,(uint32_t)e->source_actor);
        put8(&w,e->kind); putpos(&w,e->position); putf(&w,e->strength); putf(&w,e->range);
        put8(&w,e->material);
    }
    for(int i=0;i<SWAT_MAX_PROJECTILES;i++) {
        const SwatProjectile* p=&state->projectiles[i]; put8(&w,p->active);
        if(!p->active) continue;
        put8(&w,p->kind); put8(&w,p->owner); put8(&w,p->detonated);
        put32(&w,p->age); put32(&w,p->remaining_ticks); putpos(&w,p->position); putvec(&w,p->velocity);
        put32(&w,(uint32_t)p->target); putf(&w,p->damage); putvec(&w,p->attachment);
    }
    for(int i=0;i<SWAT_MAX_DEVICES;i++) {
        const SwatDevice* d=&state->devices[i]; put8(&w,d->active); if(!d->active) continue;
        put8(&w,d->kind); put8(&w,d->owner); putpos(&w,d->position); putvec(&w,d->velocity);
        putf(&w,d->yaw); putf(&w,d->pitch); putf(&w,d->health); put32(&w,d->age); put32(&w,d->battery_ticks);
    }
    put8(&w,state->commander_actor);
    for(int i=0;i<SWAT_SNIPERS;i++) {
        const SwatSniper* sniper=&state->snipers[i]; put8(&w,sniper->deployed);
        if(!sniper->deployed) continue;
        put8(&w,sniper->post); put8(&w,sniper->rifle); put32(&w,(uint32_t)sniper->target);
        put32(&w,sniper->travel_ticks); putf(&w,sniper->target_height); put8(&w,sniper->status);
    }
    return w.ok ? size-w.left : 0;
}
bool swat_decode_snapshot(SwatSnapshot* state,const void* bytes,size_t size) {
    Reader r={bytes,size,true}; SwatSnapshot tmp={0}; read_header(&r,SWAT_MSG_SNAPSHOT,&tmp.epoch);
    tmp.revision=get32(&r); if(!tmp.revision) r.ok=false;
    tmp.tick=geti(&r,0,3600000); tmp.actor_count=geti(&r,1,SWAT_MAX_ACTORS); tmp.object_count=geti(&r,1,SWAT_MAX_OBJECTS);
    tmp.generation=geti(&r,0,3600000);
    unsigned int end=get8(&r),leader=get8(&r),mask=get8(&r);
    if(end>SWAT_FALL || leader>=SWAT_MAX_PLAYERS || mask>15) r.ok=false;
    tmp.end=(SwatEnd)end; tmp.leader_slot=(int)leader; tmp.player_mask=mask;
    for(int i=0;i<SWAT_MAX_PLAYERS;i++) tmp.ack[i]=get32(&r);
    tmp.totals=getevents(&r); if(!r.ok) return false;
    tmp.debrief.roe_violations=geti(&r,0,1000000); tmp.debrief.arrests=geti(&r,0,SWAT_MAX_ACTORS); tmp.debrief.rescued=geti(&r,0,SWAT_MAX_ACTORS);
    tmp.debrief.evidence=geti(&r,0,SWAT_MAX_ACTORS); tmp.debrief.unlawful_damage=getf(&r,0,1000000);
    for(int i=0;i<SWAT_MAX_ACTORS;i++) {
        unsigned int flags=get8(&r); SwatEvidence* e=&tmp.evidence[i]; e->dropped=flags&1; e->collected=flags&2;
        if(flags>3 || (e->collected && !e->dropped)) r.ok=false;
        if(e->dropped) { e->actor=(int)get8(&r); e->position=getpos(&r); if(e->actor!=i) r.ok=false; }
    }
    for(int i=0;i<SWAT_MAX_ACTORS;i++) {
        tmp.actors[i]=getactor(&r);
        if(i>=tmp.actor_count && tmp.actors[i].present) r.ok=false;
    }
    for(int i=0;i<tmp.object_count;i++) {
        unsigned int flags=get8(&r); if(flags>7) r.ok=false;
        SwatObjectState* o=&tmp.objects[i];
        o->active=(flags&1)!=0; o->door_open=(flags&2)!=0; o->locked=(flags&4)!=0;
        o->health=getf(&r,0,1000000); o->door_angle=getf(&r,0,SWAT_PI*0.5f);
        o->breach_owner=(int)get8(&r)-1; o->breach_ticks=(int)get8(&r);
        if(o->breach_owner>=SWAT_MAX_ACTORS || o->breach_ticks>48) r.ok=false;
        o->wedge_owner=(int)get8(&r)-1; unsigned int tools=get8(&r); o->trap_known=get32(&r);
        o->peek=tools&1; o->trapped=tools&2; o->breach_position=getpos(&r);
        if(o->wedge_owner>=SWAT_MAX_ACTORS || tools>3 || o->trap_known>=(1u<<SWAT_MAX_ACTORS)) r.ok=false;
    }
    tmp.sound_count=(int)get8(&r); if(tmp.sound_count>SWAT_NET_SOUNDS || !r.ok) return false;
    uint32_t previous=0;
    for(int i=0;i<tmp.sound_count;i++) {
        SwatSoundEvent* e=&tmp.sounds[i]; e->id=get32(&r); e->tick=geti(&r,0,tmp.tick);
        e->source_actor=geti(&r,-1,SWAT_MAX_ACTORS-1);
        unsigned int kind=get8(&r); if(kind>=SWAT_SOUND_KINDS || !e->id || e->id<=previous) r.ok=false;
        previous=e->id; e->kind=(SwatSoundKind)kind; e->position=getpos(&r);
        e->strength=getf(&r,0,10); e->range=getf(&r,0,1000);
        e->material=(SwatMaterial)get8(&r); if(e->material>=SWAT_MATERIAL_COUNT) r.ok=false;
    }
    for(int i=0;i<SWAT_MAX_PROJECTILES;i++) {
        SwatProjectile* p=&tmp.projectiles[i]; unsigned active=get8(&r); if(active>1) r.ok=false;
        p->active=active!=0; if(!p->active) continue;
        p->kind=(SwatProjectileKind)get8(&r); p->owner=(int)get8(&r); unsigned detonated=get8(&r);
        if(p->kind>=SWAT_PROJECTILE_KINDS || p->owner>=SWAT_MAX_ACTORS || detonated>1) r.ok=false;
        p->detonated=detonated!=0; p->age=geti(&r,0,810); p->remaining_ticks=geti(&r,1,720);
        p->position=getpos(&r); p->velocity=getvec(&r,200);
        p->target=geti(&r,-1,SWAT_MAX_ACTORS-1); p->damage=getf(&r,0,100); p->attachment=getvec(&r,4);
    }
    for(int i=0;i<SWAT_MAX_DEVICES;i++) {
        SwatDevice* d=&tmp.devices[i]; unsigned int active=get8(&r); if(active>1) r.ok=false; d->active=active!=0; if(!d->active) continue;
        d->kind=(SwatDeviceKind)get8(&r); d->owner=(int)get8(&r); if(d->kind>=SWAT_DEVICE_KINDS || d->owner>=SWAT_MAX_ACTORS) r.ok=false;
        d->position=getpos(&r); d->velocity=getvec(&r,100); d->yaw=getf(&r,-SWAT_PI,SWAT_PI); d->pitch=getf(&r,-80*SWAT_RAD,80*SWAT_RAD);
        d->health=getf(&r,0,40); d->age=geti(&r,0,3600000); d->battery_ticks=geti(&r,0,18000);
    }
    tmp.commander_actor=(int)get8(&r); if(tmp.commander_actor>=SWAT_MAX_ACTORS) r.ok=false;
    for(int i=0;i<SWAT_SNIPERS;i++) {
        SwatSniper* sniper=&tmp.snipers[i]; unsigned int deployed=get8(&r); if(deployed>1) r.ok=false;
        sniper->deployed=deployed!=0; if(!sniper->deployed) continue;
        sniper->post=(int)get8(&r); sniper->rifle=(int)get8(&r); sniper->target=geti(&r,-1,SWAT_MAX_ACTORS-1);
        sniper->travel_ticks=geti(&r,0,180); sniper->target_height=getf(&r,0,1); sniper->status=(SwatSniperStatus)get8(&r);
        if(sniper->post>=3 || sniper->rifle>1 || sniper->status>=SWAT_SNIPER_STATUSES ||
            !tmp.actors[swat_sniper_actor(i)].present || tmp.actors[swat_sniper_actor(i)].role!=SWAT_SNIPER) r.ok=false;
    }
    if(!r.ok || r.left) return false;
    *state=tmp; return true;
}

size_t swat_encode_control(void* bytes,size_t size,SwatMessage type,uint32_t epoch,int slot) {
    Writer w={bytes,size,true}; header(&w,type,epoch); put8(&w,slot); return w.ok ? size-w.left : 0;
}
bool swat_decode_control(const void* bytes,size_t size,SwatMessage type,uint32_t* epoch,int* slot) {
    Reader r={bytes,size,true}; uint32_t e; read_header(&r,type,&e);
    unsigned int s=get8(&r); if(s>=SWAT_MAX_PLAYERS) r.ok=false;
    if(!r.ok || r.left) return false;
    *epoch=e; *slot=(int)s; return true;
}

size_t swat_encode_scenario(void* bytes,size_t size,uint32_t epoch,const SwatConfig* config) {
    Writer w={bytes,size,true}; header(&w,SWAT_MSG_SCENARIO,epoch);
    put8(&w,config->mission); put32(&w,config->layout_seed);
    put8(&w,config->generator); put8(&w,config->difficulty); put8(&w,config->tactical_rules); put8(&w,config->squad_bots);
    return w.ok ? size-w.left : 0;
}
bool swat_decode_scenario(const void* bytes,size_t size,uint32_t* epoch,SwatConfig* config) {
    Reader r={bytes,size,true}; uint32_t e; read_header(&r,SWAT_MSG_SCENARIO,&e);
    SwatConfig tmp=swat_default_config();
    tmp.mission=(int)get8(&r); tmp.layout_seed=get32(&r);
    tmp.generator=(int)get8(&r); tmp.difficulty=(int)get8(&r);
    unsigned int tactical=get8(&r); if(tactical>1) r.ok=false; tmp.tactical_rules=tactical!=0; tmp.squad_bots=(int)get8(&r); if(tmp.squad_bots>3) r.ok=false;
    if(!r.ok || r.left || tmp.mission>=SWAT_MISSION_COUNT || tmp.generator>=SWAT_GENERATORS || tmp.difficulty>2) return false;
    *epoch=e; *config=tmp; return true;
}

void swat_capture_map(const SwatSim* sim,uint32_t epoch,SwatMap* map) {
    memset(map,0,sizeof(*map)); map->epoch=epoch; map->config=sim->config;
    map->sound_floor=sim->sounds.next_id ? sim->sounds.next_id-1 : 0;
    map->count=sim->world.count; map->extraction=sim->extraction;
    map->mission=sim->mission; memcpy(map->layout_tokens,sim->layout.tokens,sizeof(map->layout_tokens)); map->layout_policy_id=sim->layout.policy_id;
    map->room_count=sim->world.room_count; memcpy(map->rooms,sim->world.rooms,sizeof(map->rooms));
    for(int i=0;i<map->count;i++) {
        const SwatObject* o=&sim->world.objects[i];
        map->objects[i]=(SwatMapObject){.center=o->center,.hinge=o->hinge,.half=o->half,.yaw=swat_angle(o->yaw),.max_health=o->max_health,.closed_yaw=swat_angle(o->closed_yaw),.material=o->material,.part=o->part,.door=o->door,.pitch=o->pitch};
        map->objects[i].fractured=o->fractured; map->objects[i].wall_group=o->wall_group;
        memcpy(map->objects[i].corners,o->corners,sizeof(o->corners));
    }
}
void swat_capture_snapshot(const SwatSim* sim,uint32_t epoch,SwatSnapshot* state) {
    memset(state,0,sizeof(*state)); state->epoch=epoch; state->revision=1; state->tick=sim->tick;
    state->actor_count=sim->actor_count; state->object_count=sim->world.count;
    state->generation=sim->world.generation; state->end=sim->end; state->totals=sim->totals;
    state->debrief=sim->debrief; memcpy(state->evidence,sim->evidence,sizeof(state->evidence));
    memcpy(state->snipers,sim->snipers,sizeof(state->snipers)); state->commander_actor=sim->commander_actor;
    memcpy(state->devices,sim->devices,sizeof(state->devices));
    for(int i=0;i<SWAT_MAX_DEVICES;i++) { state->devices[i].body=b3_nullBodyId; state->devices[i].shape=b3_nullShapeId; }
    memcpy(state->projectiles,sim->projectiles,sizeof(state->projectiles));
    for(int i=0;i<SWAT_MAX_PROJECTILES;i++) {
        state->projectiles[i].body=b3_nullBodyId; state->projectiles[i].shape=b3_nullShapeId;
    }
    for(int i=0;i<SWAT_MAX_ACTORS;i++) {
        const SwatActor* a=&sim->actors[i]; if(!a->present) continue;
        const SwatController* c=&a->controller; SwatActorState* out=&state->actors[i];
        out->present=true; out->alive=a->alive; out->role=a->role; out->health=a->health;
        out->crouched=c->body.crouched; out->grounded=c->body.onGround; out->sprinting=c->sprinting; out->muzzle_blocked=c->muzzle_blocked;
        out->position=b3Body_GetPosition(c->body.body); out->velocity=b3Body_GetLinearVelocity(c->body.body); out->upper_offset=c->body.upperOffset;
        out->yaw=c->yaw; out->pitch=c->pitch; out->ads=c->ads; out->stamina=c->stamina; out->eye_height=c->eye_height;
        out->recoil_pitch=c->recoil_pitch; out->recoil_yaw=c->recoil_yaw; out->lean=c->lean; out->ready_blend=c->ready_blend; out->ready=c->ready;
        out->last_shot_tick=a->last_shot_tick; out->tracer_start=a->tracer_start; out->tracer_end=a->tracer_end; out->arsenal=a->arsenal;
        out->gear=a->gear; out->mind=a->mind; out->rescued=a->rescued;
    }
    for(int i=0;i<state->object_count;i++) {
        const SwatObject* o=&sim->world.objects[i];
        state->objects[i]=(SwatObjectState){.active=o->active,.door_open=o->door_open,.locked=o->locked,
            .health=o->health,.door_angle=o->door_angle,.breach_owner=o->breach_owner,.breach_ticks=o->breach_ticks,.wedge_owner=o->wedge_owner,.peek=o->peek,.trapped=o->trapped,.trap_known=o->trap_known,.breach_position=o->breach_position};
    }
    int start=sim->sounds.count>SWAT_NET_SOUNDS ? sim->sounds.count-SWAT_NET_SOUNDS : 0;
    for(int i=start;i<sim->sounds.count;i++) {
        const SwatSoundEvent* e=swat_sound_at(&sim->sounds,i);
        if(sim->tick-e->tick<=SWAT_SOUND_LIFETIME) state->sounds[state->sound_count++]=*e;
    }
}
void swat_apply_map(SwatSim* sim,const SwatMap* map) {
#pragma omp critical(swat_world_lifecycle)
    {
        swat_world_close(&sim->world); memset(sim,0,sizeof(*sim));
        sim->config=map->config; sim->extraction=map->extraction; sim->episode=(int)map->epoch;
        sim->mission=map->mission;
        if(map->config.mission==SWAT_GENERATED) {
            swat_layout_plan(&sim->layout,map->layout_tokens,map->config.difficulty);
            sim->layout.seed=map->config.layout_seed; sim->layout.policy_id=map->layout_policy_id;
        }
        sim->sounds.next_id=map->sound_floor+1; swat_world_init(&sim->world);
        if(map->config.mission==SWAT_BUILDING) swat_building_plan(&sim->layout,map->config.layout_seed,map->config.difficulty);
        sim->world.room_count=map->room_count; memcpy(sim->world.rooms,map->rooms,sizeof(map->rooms));
        for(int i=0;i<map->count;i++) {
            const SwatMapObject* source=&map->objects[i];
            int id=swat_world_box(&sim->world,source->center,source->half,source->material,source->max_health);
            SwatObject* o=&sim->world.objects[id]; o->hinge=source->hinge; o->yaw=source->yaw; o->door=source->door;
            o->closed_yaw=source->closed_yaw; o->part=source->part; o->pitch=source->pitch; o->wall_group=source->wall_group;
            if(source->fractured) swat_world_fragment(o,source->corners);
            swat_world_tilt(o,o->pitch);
        }
        if(map->config.mission==SWAT_MOTEL) {
            bool bound=swat_motel_bind_collision(&sim->world);
            (void)bound; // Modified/noncanonical maps retain their explicit boxes.
        }
        if(map->config.mission==SWAT_STOREFRONT) {
            bool bound=swat_storefront_bind_collision(&sim->world);
            (void)bound;
        }
    }
}
bool swat_apply_snapshot(SwatSim* sim,const SwatSnapshot* state) {
    if(state->object_count!=sim->world.count) return false;
    for(int i=0;i<state->object_count;i++) {
        const SwatObject* o=&sim->world.objects[i]; const SwatObjectState* in=&state->objects[i];
        if(in->breach_owner>=0) {
            b3Quat rotation=b3MulQuat(b3MakeQuatFromAxisAngle(swat_v(0,1,0),o->yaw),b3MakeQuatFromAxisAngle(swat_v(0,0,1),o->pitch));
            b3Vec3 local=b3InvRotateVector(rotation,b3SubPos(in->breach_position,o->center));
            if(!isfinite(local.x) || !isfinite(local.y) || !isfinite(local.z) ||
               fabsf(local.x)>o->half.x+.05f || fabsf(local.y)>o->half.y+.05f || fabsf(local.z)>o->half.z+.05f) return false;
        }
        if(in->health>o->max_health || (!o->active && in->active) ||
           ((!o->door || !in->active) && in->locked) || (in->breach_owner>=0 && (!in->active || !swat_world_breachable(o))) ||
           (in->locked && in->door_open) || (in->breach_owner>=0 && in->door_open) ||
           (!o->door && (in->wedge_owner>=0 || in->peek || in->trapped || in->trap_known)) ||
           (in->wedge_owner>=0 && (!in->active || in->door_open)) || (in->trap_known && !in->trapped)) return false;
    }
    for(int i=0;i<state->object_count;i++) {
        SwatObject* o=&sim->world.objects[i]; const SwatObjectState* in=&state->objects[i];
        o->health=in->health; o->door_open=in->door_open; o->door_angle=in->door_angle;
        o->wedge_owner=in->wedge_owner; o->peek=in->peek; o->trapped=in->trapped; o->trap_known=in->trap_known;
        o->locked=in->locked; o->breach_owner=in->breach_owner; o->breach_ticks=in->breach_ticks; o->breach_position=in->breach_position;
        if(!in->active && o->active) { b3DestroyBody(o->body); o->body=b3_nullBodyId; o->shape=b3_nullShapeId; o->active=false; }
        if(o->active && o->door) {
            o->yaw=o->closed_yaw+o->door_angle; o->center=b3OffsetPos(o->hinge,swat_v(sinf(o->yaw)*o->half.z,0,cosf(o->yaw)*o->half.z));
            swat_world_tilt(o,o->pitch);
        }
    }
    for(int i=0;i<SWAT_MAX_ACTORS;i++) {
        const SwatActorState* in=&state->actors[i]; SwatActor* a=&sim->actors[i];
        if(!in->present) {
            if(a->present) { b3DestroyBody(a->controller.body.body); memset(a,0,sizeof(*a)); }
            continue;
        }
        if(!a->present || a->role!=in->role) swat_sim_spawn_actor(sim,i,in->role,in->position,0);
        SwatController* c=&a->controller;
        a->alive=in->alive; a->health=in->health; a->arsenal=in->arsenal;
        a->mind=in->mind; a->rescued=in->rescued;
        a->gear=in->gear; c->mobility=swat_equipment_mobility(&a->gear);
        a->last_shot_tick=in->last_shot_tick; a->tracer_start=in->tracer_start; a->tracer_end=in->tracer_end;
        c->yaw=in->yaw; c->pitch=in->pitch; c->ads=in->ads; c->stamina=in->stamina;
        c->eye_height=in->eye_height; c->recoil_pitch=in->recoil_pitch; c->recoil_yaw=in->recoil_yaw; c->lean=in->lean; c->ready_blend=in->ready_blend; c->ready=in->ready;
        c->sprinting=in->sprinting; c->muzzle_blocked=in->muzzle_blocked;
        swat_body_replica_pose(&c->body,in->position,in->velocity,in->crouched,in->grounded,in->upper_offset);
        if(in->alive) b3Body_Enable(c->body.body); else b3Body_Disable(c->body.body);
    }
    sim->actor_count=state->actor_count; sim->tick=state->tick; sim->end=state->end;
    sim->debrief=state->debrief; memcpy(sim->evidence,state->evidence,sizeof(sim->evidence));
    sim->totals=state->totals; sim->world.generation=state->generation;
    memcpy(sim->snipers,state->snipers,sizeof(sim->snipers)); sim->commander_actor=state->commander_actor;
    for(int i=0;i<SWAT_MAX_PROJECTILES;i++) {
        if(B3_IS_NON_NULL(sim->projectiles[i].body)) b3DestroyBody(sim->projectiles[i].body);
        sim->projectiles[i]=state->projectiles[i];
        sim->projectiles[i].body=b3_nullBodyId; sim->projectiles[i].shape=b3_nullShapeId;
    }
    for(int i=0;i<SWAT_MAX_DEVICES;i++) {
        if(B3_IS_NON_NULL(sim->devices[i].body)) b3DestroyBody(sim->devices[i].body);
        sim->devices[i]=state->devices[i]; sim->devices[i].body=b3_nullBodyId; sim->devices[i].shape=b3_nullShapeId;
    }
    for(int i=0;i<state->sound_count;i++) swat_sound_append(&sim->sounds,state->sounds[i]);
    return true;
}
