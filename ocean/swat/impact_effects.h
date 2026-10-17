#ifndef SWAT_IMPACT_EFFECTS_H
#define SWAT_IMPACT_EFFECTS_H
#include "sound_feedback.h"

// Cosmetic, reconstructed from replicated events. No bodies, casts, allocations,
// frame accumulator, simulation RNG, or persistent debris in this path.
#define SWAT_IMPACT_EFFECT_EVENTS 12
#define SWAT_IMPACT_EFFECT_PARTICLES 96
#define SWAT_IMPACT_EFFECT_VERTICES 1404
typedef struct SwatImpactParticle {
    b3Pos position;
    float radius, aspect, rotation, distance_squared;
    uint32_t event_id;
    uint8_t color[4];
    bool dust,shard;
} SwatImpactParticle;

static inline bool swat_impact_recent(const SwatSoundEvent* e,int tick) {
    int64_t age=(int64_t)tick-e->tick;
    return e->id && age>=0 && age<90 && e->material>=0 && e->material<SWAT_MATERIAL_COUNT &&
        e->material!=SWAT_CARPET && (e->kind==SWAT_SOUND_IMPACT || e->kind==SWAT_SOUND_BREAK) &&
        isfinite(e->position.x) && isfinite(e->position.y) && isfinite(e->position.z);
}
static inline bool swat_impact_near(const SwatSoundEvent* a,const SwatSoundEvent* b) {
    return swat_sound_same_contact(a,b);
}

static inline int swat_impact_sample(const SwatSoundLog* log,int tick,b3Pos eye,b3Vec3 forward,
                                     SwatImpactParticle out[SWAT_IMPACT_EFFECT_PARTICLES]) {
    int count=0,events=0,vertices=0;
    // Breaks take priority over ordinary hits when automatic fire fills the log.
    for(int pass=0;pass<2;pass++)for(int i=log->count-1;i>=0 && events<SWAT_IMPACT_EFFECT_EVENTS;i--) {
        const SwatSoundEvent* e=swat_sound_at(log,i);
        if(!swat_impact_recent(e,tick) || (e->kind==SWAT_SOUND_BREAK)!=(pass==0))continue;
        bool glass=e->material==SWAT_GLASS || e->material==SWAT_OPAQUE_GLASS;
        bool metal=e->material==SWAT_STEEL || e->material==SWAT_ALUMINUM;
        bool broken=e->kind==SWAT_SOUND_BREAK,blast=false,duplicate=false;
        float lifetime=broken ? (glass?.38f:.65f) : .25f;
        for(int j=log->count-1;j>=0;j--) {
            const SwatSoundEvent* other=swat_sound_at(log,j);
            if(!swat_impact_near(e,other))continue;
            if(!broken && other->kind==SWAT_SOUND_BREAK && other->material==e->material)duplicate=true;
            if(broken && other->kind==SWAT_SOUND_BREACH)blast=true;
        }
        if(blast)lifetime=1.15f;
        float t=((int64_t)tick-e->tick)*SWAT_DT;
        b3Vec3 delta=b3SubPos(e->position,eye);
        float distance=b3Length(delta);
        if(duplicate || t>=lifetime || distance>30 || b3Dot(delta,forward)<-.5f)continue;
        events++;
        uint32_t random=e->id*0x9e3779b9u+0x85ebca6bu;
        static const uint8_t colors[SWAT_MATERIAL_COUNT][3]={
            {157,151,139},{211,203,182},{140,105,68},{193,217,219},{212,209,185},
            {172,111,78},{219,209,185},{190,177,132},{206,197,182},{90,83,73},
            {128,109,81},{151,148,137},{193,217,219},{191,199,202},{62,64,58},{146,133,105}};
        int particles=blast?24:broken?8:4;
        particles=particles<SWAT_IMPACT_EFFECT_PARTICLES-count?particles:SWAT_IMPACT_EFFECT_PARTICLES-count;
        for(int p=0;p<particles;p++) {
            bool dust=blast?p<8:!glass && !metal && p<(broken?3:2);
            int cost=(dust?8:glass?3:4)*3;
            if(vertices+cost>SWAT_IMPACT_EFFECT_VERTICES)break;
            vertices+=cost;
            // Explicit order keeps the event seed stable across compilers.
            float vx=2*swat_rand01(&random)-1;
            float vy=.4f+swat_rand01(&random);
            float vz=2*swat_rand01(&random)-1;
            b3Vec3 velocity=swat_v(vx,vy,vz);
            float speed=blast?2.1f:broken?1.2f:.55f;
            b3Vec3 offset=swat_mul(velocity,speed*t);
            // A charge starts as a small volume, rather than every chip and
            // dust lobe occupying one pixel. No guessed aperture or collider.
            if(blast) {
                offset.x+=vx*.14f;offset.z+=vz*.14f;
                offset.y+=(vy-.9f)*.35f;
            }
            offset.y-=(dust?.35f:4.9f)*t*t;
            SwatImpactParticle particle={0};
            particle.position=b3OffsetPos(e->position,offset);
            particle.radius=dust ? (blast?.10f:.025f)+t*(blast?.36f:.20f) :
                (blast?.018f:.007f)+swat_rand01(&random)*(blast?.047f:glass?.018f:.010f);
            particle.event_id=e->id;particle.dust=dust;particle.shard=glass;
            particle.aspect=dust?1:e->material==SWAT_WOOD?.22f:.4f+.6f*swat_rand01(&random);
            particle.rotation=swat_rand01(&random)*2*SWAT_PI+(dust?.25f:5)*t;
            float tone=.80f+.20f*swat_rand01(&random);
            for(int c=0;c<3;c++)particle.color[c]=(uint8_t)(colors[e->material][c]*tone);
            float fade=1-t/lifetime;
            particle.color[3]=(uint8_t)((dust?(blast?95:70):glass?185:220)*fade*fade*swat_clamp((30-distance)/6,0,1));
            b3Vec3 d=b3SubPos(particle.position,eye);particle.distance_squared=b3Dot(d,d);
            // Bounded insertion sort gives transparent surfaces back-to-front.
            int at=count++;
            while(at && out[at-1].distance_squared<particle.distance_squared){out[at]=out[at-1];at--;}
            out[at]=particle;
        }
    }
    return count;
}
#endif
