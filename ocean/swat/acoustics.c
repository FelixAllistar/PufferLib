#include "acoustics.h"
#include <string.h>

const SwatSoundEvent* swat_sound_at(const SwatSoundLog* log, int index) {
    if(index<0 || index>=log->count) return NULL;
    return &log->events[(log->head-log->count+index+SWAT_SOUND_CAPACITY)%SWAT_SOUND_CAPACITY];
}

void swat_sound_append(SwatSoundLog* log, SwatSoundEvent event) {
    if(!event.id) return;
    for(int i=0;i<log->count;i++) if(swat_sound_at(log,i)->id==event.id) return;
    log->events[log->head]=event;
    log->head=(log->head+1)%SWAT_SOUND_CAPACITY;
    if(log->count<SWAT_SOUND_CAPACITY) log->count++;
    if(log->next_id<=event.id) log->next_id=event.id+1;
    if(!log->next_id) log->next_id=1;
}

void swat_sound_emit(SwatSoundLog* log, int tick, int source, SwatSoundKind kind,
                     b3Pos position, float strength, float range) {
    if(!log->next_id) log->next_id=1;
    swat_sound_append(log,(SwatSoundEvent){log->next_id,tick,source,kind,position,strength,range});
}

// Intersect a segment with the same oriented boxes used by physics. Actors
// do not act as acoustic walls. Destroyed cover is excluded immediately.
static float swat_acoustic_thickness(const SwatObject* o, b3Pos from, b3Pos to) {
    b3Vec3 p=b3SubPos(from,o->center),d=b3SubPos(to,from);
    float c=cosf(o->yaw),s=sinf(o->yaw);
    float origin[3]={c*p.x-s*p.z,p.y,s*p.x+c*p.z};
    float delta[3]={c*d.x-s*d.z,d.y,s*d.x+c*d.z};
    float half[3]={o->half.x,o->half.y,o->half.z};
    float enter=0,exit=1;
    for(int axis=0;axis<3;axis++) {
        if(fabsf(delta[axis])<1e-7f) {
            if(fabsf(origin[axis])>half[axis]) return 0;
        } else {
            float a=(-half[axis]-origin[axis])/delta[axis];
            float b=(half[axis]-origin[axis])/delta[axis];
            enter=fmaxf(enter,fminf(a,b)); exit=fminf(exit,fmaxf(a,b));
            if(exit<=enter) return 0;
        }
    }
    return (exit-enter)*b3Length(d);
}

static void swat_acoustic_transmit(const SwatWorld* world, b3Pos from, b3Pos to, float bands[3]) {
    // Game-tuned amplitude absorption per metre; not laboratory material data.
    static const float absorption[5][3]={
        {18,32,50}, {1.4f,5,10}, {2.2f,7,15}, {1,4,9}, {10,25,45}
    };
    for(int i=0;i<world->count;i++) {
        const SwatObject* o=&world->objects[i];
        if(!o->active) continue;
        float thickness=swat_acoustic_thickness(o,from,to);
        for(int b=0;b<3;b++) bands[b]*=expf(-absorption[o->material][b]*thickness);
    }
}

static SwatAcousticPath swat_acoustic_route(const SwatWorld* world, const SwatSoundEvent* event,
                                           b3Pos listener, const b3Pos* portal) {
    SwatAcousticPath path={0};
    float length=portal ? b3Distance(event->position,*portal)+b3Distance(*portal,listener) :
                         b3Distance(event->position,listener);
    if(length>event->range) return path;
    float attenuation=event->strength/(1.0f+length);
    for(int b=0;b<3;b++) path.bands[b]=attenuation;
    if(portal) {
        swat_acoustic_transmit(world,event->position,*portal,path.bands);
        swat_acoustic_transmit(world,*portal,listener,path.bands);
        // One bend loses some energy, especially in the high band.
        path.bands[0]*=0.75f; path.bands[1]*=0.55f; path.bands[2]*=0.35f;
    } else swat_acoustic_transmit(world,event->position,listener,path.bands);
    path.gain=path.bands[0]*0.3f+path.bands[1]*0.5f+path.bands[2]*0.2f;
    path.direction=swat_normalize(b3SubPos(portal ? *portal : event->position,listener));
    path.delay_ticks=(int)ceilf(length/(343.0f*SWAT_DT));
    path.via_doorway=portal!=NULL;
    return path;
}

SwatAcousticPath swat_acoustic_path(const SwatWorld* world, const SwatSoundEvent* event,
                                   b3Pos listener) {
    SwatAcousticPath best=swat_acoustic_route(world,event,listener,NULL);
    for(int i=0;i<world->count;i++) {
        const SwatObject* door=&world->objects[i];
        if(!door->door || (door->active && door->door_angle<0.15f)) continue;
        // Authored openings retain their location when the door swings/breaks.
        b3Pos portal=b3OffsetPos(door->hinge,swat_v(0,0,door->half.z));
        SwatAcousticPath candidate=swat_acoustic_route(world,event,listener,&portal);
        if(candidate.gain>best.gain) best=candidate;
    }
    return best;
}

bool swat_hearing_consumed(const SwatHearingMemory* memory, uint32_t id) {
    if(id<=memory->minimum_id) return true;
    for(int i=0;i<SWAT_SOUND_CAPACITY;i++) if(memory->consumed[i]==id) return true;
    return false;
}

void swat_hearing_consume(SwatHearingMemory* memory, uint32_t id) {
    memory->consumed[memory->head]=id;
    memory->head=(memory->head+1)%SWAT_SOUND_CAPACITY;
}

bool swat_hearing_next(const SwatWorld* world, const SwatSoundLog* log, int tick,
                       b3Pos listener, int actor, SwatHearingMemory* memory,
                       SwatHeardSound* heard) {
    for(int i=0;i<log->count;i++) {
        const SwatSoundEvent* event=swat_sound_at(log,i);
        if(event->source_actor==actor || swat_hearing_consumed(memory,event->id)) continue;
        if(tick-event->tick>SWAT_SOUND_LIFETIME) { swat_hearing_consume(memory,event->id); continue; }
        SwatAcousticPath path=swat_acoustic_path(world,event,listener);
        if(tick<event->tick+path.delay_ticks || path.gain<0.008f) continue;
        swat_hearing_consume(memory,event->id);
        float angle=atan2f(path.direction.z,path.direction.x);
        // Sixteen bearing sectors represent a hearing cue, not exact world truth.
        float sector=SWAT_PI/8;
        *heard=(SwatHeardSound){event->id,event->kind,path.gain,roundf(angle/sector)*sector};
        return true;
    }
    return false;
}
