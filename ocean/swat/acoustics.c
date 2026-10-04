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
    swat_sound_surface(log,tick,source,kind,position,strength,range,SWAT_CONCRETE);
}
void swat_sound_surface(SwatSoundLog* log,int tick,int source,SwatSoundKind kind,
                       b3Pos position,float strength,float range,SwatMaterial material) {
    if(!log->next_id) log->next_id=1;
    swat_sound_append(log,(SwatSoundEvent){log->next_id,tick,source,kind,position,strength,range,material});
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
    float loss[3]={0};
    for(int i=0;i<world->count;i++) {
        const SwatObject* o=&world->objects[i];
        if(!o->active) continue;
        float thickness=swat_acoustic_thickness(o,from,to);
        if(thickness<=0) continue;
        const SwatMaterialDef* material=swat_material(o->material);
        // A thicker homogeneous slab is not many independent partitions.
        // Use a mass-law-style logarithmic thickness correction; separate
        // board faces still contribute separate losses (no cavity resonance).
        float correction=20*log10f(fmaxf(thickness/material->reference_thickness,.001f));
        for(int b=0;b<3;b++) loss[b]+=fmaxf(0,material->transmission_db[b]+correction);
    }
    for(int b=0;b<3;b++) bands[b]*=powf(10,-fminf(loss[b],180)*.05f);
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
        // Sample across the opening: a swung leaf can obstruct the centre
        // route even when sound can pass its far edge.
        for(int sample=0;sample<3;sample++) {
            float offset=2*door->half.z*(.15f+.35f*sample);
            b3Pos portal=b3OffsetPos(door->hinge,swat_v(sinf(door->closed_yaw)*offset,0,cosf(door->closed_yaw)*offset));
            portal.y=swat_clamp((float)(event->position.y+listener.y)*.5f,
                               (float)door->center.y-door->half.y+.12f,(float)door->center.y+door->half.y-.12f);
            SwatAcousticPath candidate=swat_acoustic_route(world,event,listener,&portal);
            if(candidate.gain>best.gain) best=candidate;
        }
    }
    return best;
}

SwatRoomAcoustics swat_acoustic_room(const SwatWorld* w,b3Pos listener) {
    SwatRoomAcoustics result={{.12f,.10f,.08f},0,{0},{0}};
    int index=swat_world_room(w,listener); if(index<0) return result;
    const SwatRoom* r=&w->rooms[index];
    float x=2*r->half.x,y=2*r->half.y,z=2*r->half.z;
    float volume=x*y*z,floor=x*z,walls=2*y*(x+z),open_area=0;
    for(int i=0;i<w->count;i++) {
        const SwatObject* o=&w->objects[i];
        if(o->active && (!o->door || o->door_angle<.2f)) continue;
        if(!o->door && o->part!=SWAT_PART_SKIN && o->material!=SWAT_GLASS) continue;
        b3Vec3 d=b3SubPos(o->door ? o->hinge : o->center,r->center);
        if(fabsf(d.x)>r->half.x+.3f || fabsf(d.z)>r->half.z+.3f) continue;
        if(o->part==SWAT_PART_SKIN) {
            b3Vec3 normal=swat_v(cosf(o->yaw),0,-sinf(o->yaw));
            SwatHit through=swat_world_ray(w,b3OffsetPos(o->center,swat_mul(normal,-.18f)),normal,.36f,b3_nullBodyId);
            if(through.hit && through.kind==SWAT_HIT_WORLD) continue;
        }
        // Both board faces must be open; count each half once. Partly open
        // doors leak in proportion to their projected opening.
        float fraction=o->part==SWAT_PART_SKIN ? .5f : (o->door && o->active ? sinf(o->door_angle) : 1);
        open_area+=4*o->half.y*o->half.z*fraction;
    }
    const SwatMaterialDef* wall=swat_material(r->surface),*ground=swat_material(r->floor);
    float ceiling[3],furnishings[3]={0},ceiling_area=0;
    for(int band=0;band<3;band++) ceiling[band]=0;
    for(int i=0;i<w->count;i++) {
        const SwatObject* o=&w->objects[i]; if(!o->active) continue;
        b3Vec3 d=b3SubPos(o->center,r->center);
        // Actual roof material, plus exposed furniture faces inside this room.
        // The architectural room dimensions remain an approximation to volume.
        if(fabsf(d.y-o->half.y-r->half.y)<.25f && o->half.y<.25f) {
            float ax=fmaxf(0,fminf((float)o->center.x+o->half.x,(float)r->center.x+r->half.x)-
                fmaxf((float)o->center.x-o->half.x,(float)r->center.x-r->half.x));
            float az=fmaxf(0,fminf((float)o->center.z+o->half.z,(float)r->center.z+r->half.z)-
                fmaxf((float)o->center.z-o->half.z,(float)r->center.z-r->half.z));
            float area=ax*az; ceiling_area+=area;
            for(int band=0;band<3;band++) ceiling[band]+=area*swat_material(o->material)->absorption[band];
        }
        if(o->part==SWAT_PART_SOLID && !o->door && o->half.y>.1f &&
           fabsf(d.x)+o->half.x<r->half.x && fabsf(d.z)+o->half.z<r->half.z &&
           o->center.y>r->center.y-r->half.y+.1f && o->center.y+o->half.y<r->center.y+r->half.y) {
            float area=4*o->half.x*o->half.z+8*o->half.y*(o->half.x+o->half.z);
            for(int band=0;band<3;band++) furnishings[band]+=area*swat_material(o->material)->absorption[band];
        }
    }
    for(int band=0;band<3;band++) {
        float roof=ceiling_area>0 ? ceiling[band]*floor/ceiling_area : floor*wall->absorption[band];
        float area=walls*wall->absorption[band]+floor*ground->absorption[band]+roof+furnishings[band]+open_area;
        result.rt60[band]=swat_clamp(.161f*volume/fmaxf(area,1),.15f,2.5f);
    }
    result.wet=.22f/(1+open_area*.08f);
    float distances[4]={(float)(listener.x-r->center.x+r->half.x),
        (float)(r->center.x+r->half.x-listener.x),
        (float)(listener.z-r->center.z+r->half.z),
        (float)(r->center.z+r->half.z-listener.z)};
    for(int i=0;i<4;i++) {
        result.early_seconds[i]=swat_clamp(2*distances[i]/343,.003f,.07f);
        result.early_gain[i]=.10f*(1-wall->absorption[1])/(1+.3f*distances[i]);
    }
    return result;
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
