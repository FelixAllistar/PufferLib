#ifndef SWAT_IMPACT_EFFECTS_H
#define SWAT_IMPACT_EFFECTS_H
#include "acoustics.h"

// Cosmetic, reconstructed from replicated events. No bodies, casts, allocations,
// frame accumulator, simulation RNG, or persistent debris in this path.
#define SWAT_IMPACT_EFFECT_EVENTS 12
#define SWAT_IMPACT_EFFECT_PARTICLES 96
typedef struct SwatImpactParticle {
    b3Pos position;
    float radius, aspect, rotation, distance_squared;
    uint32_t event_id;
    uint8_t color[4];
    bool dust;
} SwatImpactParticle;

static inline bool swat_impact_recent(const SwatSoundEvent* e,int tick) {
    int64_t age=(int64_t)tick-e->tick;
    return e->id && age>=0 && age<60 && e->material>=0 && e->material<SWAT_MATERIAL_COUNT &&
        e->material!=SWAT_CARPET && (e->kind==SWAT_SOUND_IMPACT || e->kind==SWAT_SOUND_BREAK) &&
        isfinite(e->position.x) && isfinite(e->position.y) && isfinite(e->position.z);
}
static inline bool swat_impact_near(const SwatSoundEvent* a,const SwatSoundEvent* b) {
    return a->tick==b->tick && a->source_actor==b->source_actor && b3Distance(a->position,b->position)<.08f;
}

static inline int swat_impact_sample(const SwatSoundLog* log,int tick,b3Pos eye,b3Vec3 forward,
                                     SwatImpactParticle out[SWAT_IMPACT_EFFECT_PARTICLES]) {
    int count=0,events=0;
    // Breaks take priority over ordinary hits when automatic fire fills the log.
    for(int pass=0;pass<2;pass++)for(int i=log->count-1;i>=0 && events<SWAT_IMPACT_EFFECT_EVENTS;i--) {
        const SwatSoundEvent* e=swat_sound_at(log,i);
        if(!swat_impact_recent(e,tick) || (e->kind==SWAT_SOUND_BREAK)!=(pass==0))continue;
        bool glass=e->material==SWAT_GLASS || e->material==SWAT_OPAQUE_GLASS;
        bool metal=e->material==SWAT_STEEL;
        bool broken=e->kind==SWAT_SOUND_BREAK,blast=false,duplicate=false;
        float lifetime=broken ? (glass?.38f:.65f) : .25f;
        for(int j=log->count-1;j>=0;j--) {
            const SwatSoundEvent* other=swat_sound_at(log,j);
            if(!swat_impact_near(e,other))continue;
            if(!broken && other->kind==SWAT_SOUND_BREAK && other->material==e->material)duplicate=true;
            if(broken && other->kind==SWAT_SOUND_FLASH && other->strength>=2.4f)blast=true;
        }
        if(blast)lifetime=.85f;
        float t=((int64_t)tick-e->tick)*SWAT_DT;
        b3Vec3 delta=b3SubPos(e->position,eye);
        float distance=b3Length(delta);
        if(duplicate || t>=lifetime || distance>30 || b3Dot(delta,forward)<-.5f)continue;
        events++;
        uint32_t random=e->id*0x9e3779b9u+0x85ebca6bu;
        static const uint8_t colors[SWAT_MATERIAL_COUNT][3]={
            {157,151,139},{211,203,182},{140,105,68},{193,217,219},{212,209,185},
            {172,111,78},{219,209,185},{190,177,132},{206,197,182},{90,83,73},
            {128,109,81},{151,148,137},{193,217,219}};
        int particles=broken?8:4;
        for(int p=0;p<particles;p++) {
            bool dust=!glass && !metal && p<(broken?3:2);
            // Explicit order keeps the event seed stable across compilers.
            float vx=2*swat_rand01(&random)-1;
            float vy=.4f+swat_rand01(&random);
            float vz=2*swat_rand01(&random)-1;
            b3Vec3 velocity=swat_v(vx,vy,vz);
            float speed=blast?1.9f:broken?1.2f:.55f;
            b3Vec3 offset=swat_mul(velocity,speed*t);
            offset.y-=(dust?.4f:3.4f)*t*t;
            SwatImpactParticle particle={0};
            particle.position=b3OffsetPos(e->position,offset);
            particle.radius=dust ? (blast?.09f:.025f)+t*(blast?.6f:.20f) : .007f+swat_rand01(&random)*(glass?.018f:.010f);
            particle.event_id=e->id;particle.dust=dust;
            particle.aspect=dust?1:e->material==SWAT_WOOD?.22f:.4f+.6f*swat_rand01(&random);
            particle.rotation=swat_rand01(&random)*2*SWAT_PI;
            for(int c=0;c<3;c++)particle.color[c]=colors[e->material][c];
            float fade=1-t/lifetime;
            particle.color[3]=(uint8_t)((dust?70:glass?185:220)*fade*fade*swat_clamp((30-distance)/6,0,1));
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
