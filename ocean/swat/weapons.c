#include "weapons.h"
#include "rifle_geometry.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Fictional game profiles. Reload events and inventory are authority-owned.
static const SwatWeaponDef swat_weapon_defs[SWAT_WEAPON_PROFILES] = {
    {"GE CARBINE",30,6,120,156,24,34.0f,60.0f,1.0f,2.0f,0.08f,1.2f,true,SWAT_CARBINE_REACH,SWAT_CARBINE_SIGHT_HEIGHT},
    {"P9 SIDEARM",15,10,90,120,18,26.0f,40.0f,0.45f,2.5f,0.15f,1.7f,false,.36f,.03f},
    {"LL IMPACT LAUNCHER",5,48,150,180,30,6.0f,18.0f,0.14f,3.0f,0.5f,1.8f,false,.62f,.055f},
    {"GE PRECISION",4,90,180,210,36,82.0f,150.0f,2.6f,3.0f,.025f,2.5f,false,.84f,.06f},
    {"GE MARKSMAN",10,24,150,180,30,48.0f,100.0f,1.6f,2.5f,.065f,1.5f,false,.72f,.06f},
    {"PEPPER LAUNCHER",20,12,138,168,28,1.0f,22.0f,.08f,2.0f,.25f,.25f,false,.57f,.05f},
    {"BREACH SHOTGUN",6,48,180,210,32,42.0f,24.0f,.65f,3.5f,.8f,2.2f,false,.65f,.04f},
    {"GE COMPACT",25,5,108,138,20,24.0f,40.0f,.65f,2.5f,.12f,1.0f,true,.45f,.045f},
    {"LL CS LAUNCHER",5,48,150,180,30,0,18,.01f,3,.5f,1.8f,false,.62f,.055f},
    {"LL FLASH LAUNCHER",5,48,150,180,30,0,18,.01f,3,.5f,1.8f,false,.62f,.055f},
    {"MULTI-PROBE CEW",9,30,150,180,24,0,10,.01f,2,.2f,.1f,false,.36f,.03f},
    {"TETHER RESTRAINT",1,60,120,150,28,0,6,.01f,2,.4f,.2f,false,.40f,.035f},
};

// The compiled table stays active unless a data file that differs from it is
// loaded. swat_weapons_reset() restores it.
static SwatWeaponDef loaded_defs[SWAT_WEAPON_PROFILES];
static char loaded_names[SWAT_WEAPON_PROFILES][SWAT_WEAPON_NAME_SIZE];
static const SwatWeaponDef* active_defs=swat_weapon_defs;
static uint32_t active_id;

const SwatWeaponDef* swat_weapon_def(int slot) {
    return &active_defs[slot>=0 && slot<SWAT_WEAPON_PROFILES ? slot : 0];
}
const SwatWeaponDef* swat_weapon_builtin_def(int profile) {
    return &swat_weapon_defs[profile>=0 && profile<SWAT_WEAPON_PROFILES ? profile : 0];
}
uint32_t swat_weapons_table_id(void) { return active_id; }
void swat_weapons_reset(void) { active_defs=swat_weapon_defs; active_id=0; }

typedef enum WeaponFieldKind { FIELD_NAME, FIELD_INT, FIELD_FLOAT, FIELD_BOOL } WeaponFieldKind;
typedef struct WeaponField { const char* key; WeaponFieldKind kind; size_t offset; double lo,hi; } WeaponField;
// Every SwatWeaponDef field, in declaration order. Ranges keep the existing
// rules well defined: tick counts are positive divisors in the observation and
// stay inside the snapshot decoder's 10000-tick bounds, and energy divides
// penetration. Gameplay tuning is free inside them.
static const WeaponField weapon_fields[]={
    {"name",FIELD_NAME,offsetof(SwatWeaponDef,name),1,SWAT_WEAPON_NAME_SIZE-1},
    {"capacity",FIELD_INT,offsetof(SwatWeaponDef,capacity),1,100},
    {"shot_ticks",FIELD_INT,offsetof(SwatWeaponDef,shot_ticks),1,600},
    {"reload_ticks",FIELD_INT,offsetof(SwatWeaponDef,reload_ticks),1,3600},
    {"empty_reload_ticks",FIELD_INT,offsetof(SwatWeaponDef,empty_reload_ticks),1,3600},
    {"equip_ticks",FIELD_INT,offsetof(SwatWeaponDef,equip_ticks),1,600},
    {"damage",FIELD_FLOAT,offsetof(SwatWeaponDef,damage),0,1000},
    {"range",FIELD_FLOAT,offsetof(SwatWeaponDef,range),.1,500},
    {"energy",FIELD_FLOAT,offsetof(SwatWeaponDef,energy),.001,10},
    {"hip_spread",FIELD_FLOAT,offsetof(SwatWeaponDef,hip_spread),0,30},
    {"ads_spread",FIELD_FLOAT,offsetof(SwatWeaponDef,ads_spread),0,30},
    {"recoil",FIELD_FLOAT,offsetof(SwatWeaponDef,recoil),0,20},
    {"automatic",FIELD_BOOL,offsetof(SwatWeaponDef,automatic),0,1},
    {"barrel",FIELD_FLOAT,offsetof(SwatWeaponDef,barrel),.1,1.5},
    {"sight_height",FIELD_FLOAT,offsetof(SwatWeaponDef,sight_height),0,.3},
};
#define WEAPON_FIELDS ((int)(sizeof(weapon_fields)/sizeof(weapon_fields[0])))

static const void* field_at(const SwatWeaponDef* d,const WeaponField* f) { return (const char*)d+f->offset; }
static void* field_mut(SwatWeaponDef* d,const WeaponField* f) { return (char*)d+f->offset; }
static uint32_t field_word(const SwatWeaponDef* d,const WeaponField* f) {
    const void* p=field_at(d,f); uint32_t word=0;
    if(f->kind==FIELD_INT) { int32_t v=*(const int*)p; memcpy(&word,&v,4); }
    else if(f->kind==FIELD_FLOAT) memcpy(&word,p,4);
    else if(f->kind==FIELD_BOOL) word=*(const bool*)p ? 1 : 0;
    return word;
}
static bool tables_equal(const SwatWeaponDef* a,const SwatWeaponDef* b) {
    for(int i=0;i<SWAT_WEAPON_PROFILES;i++) for(int k=0;k<WEAPON_FIELDS;k++) {
        const WeaponField* f=&weapon_fields[k];
        if(f->kind==FIELD_NAME ? strcmp(a[i].name,b[i].name)!=0 : field_word(&a[i],f)!=field_word(&b[i],f)) return false;
    }
    return true;
}
static uint32_t digest_byte(uint32_t hash,unsigned char byte) { return (hash^byte)*16777619u; }
static uint32_t digest_word(uint32_t hash,uint32_t word) {
    for(int i=3;i>=0;i--) hash=digest_byte(hash,(unsigned char)(word>>(8*i)));
    return hash;
}
// FNV-1a over big-endian words and length-prefixed names: independent of
// struct layout, endianness and padding.
static uint32_t table_digest(const SwatWeaponDef* defs) {
    uint32_t hash=digest_word(2166136261u,SWAT_WEAPON_DATA_VERSION);
    for(int i=0;i<SWAT_WEAPON_PROFILES;i++) for(int k=0;k<WEAPON_FIELDS;k++) {
        const WeaponField* f=&weapon_fields[k];
        if(f->kind!=FIELD_NAME) { hash=digest_word(hash,field_word(&defs[i],f)); continue; }
        size_t n=strlen(defs[i].name); hash=digest_word(hash,(uint32_t)n);
        for(size_t c=0;c<n;c++) hash=digest_byte(hash,(unsigned char)defs[i].name[c]);
    }
    return hash;
}

#if defined(__GNUC__)
__attribute__((format(printf,5,6)))
#endif
static bool reject(char* error,size_t capacity,const char* path,int line,const char* format,...) {
    if(error && capacity) {
        int n=line ? snprintf(error,capacity,"%s:%d: ",path,line) : snprintf(error,capacity,"%s: ",path);
        if(n>=0 && (size_t)n<capacity) {
            va_list args; va_start(args,format);
            vsnprintf(error+n,capacity-(size_t)n,format,args);
            va_end(args);
        }
    }
    return false;
}
static char* weapon_trim(char* s) {
    while(isspace((unsigned char)*s)) s++;
    char* end=s+strlen(s);
    while(end>s && isspace((unsigned char)end[-1])) *--end='\0';
    return s;
}
static bool parse_long(const char* text,long* out) {
    char* end=NULL; errno=0; long v=strtol(text,&end,10);
    if(end==text || *end || errno) return false;
    *out=v; return true;
}

bool swat_weapons_load(const char* path,char* error,size_t capacity) {
    if(error && capacity) error[0]='\0';
    if(!path || !path[0]) return reject(error,capacity,"(weapon data)",0,"no file given");
    FILE* file=fopen(path,"r");
    if(!file) return reject(error,capacity,path,0,"cannot open the file");
    // A file is an overlay: omitted profiles and keys keep the compiled values.
    SwatWeaponDef defs[SWAT_WEAPON_PROFILES];
    char names[SWAT_WEAPON_PROFILES][SWAT_WEAPON_NAME_SIZE];
    unsigned int seen[SWAT_WEAPON_PROFILES]={0};
    bool sections[SWAT_WEAPON_PROFILES]={0};
    for(int i=0;i<SWAT_WEAPON_PROFILES;i++) {
        defs[i]=swat_weapon_defs[i];
        snprintf(names[i],sizeof(names[i]),"%s",swat_weapon_defs[i].name);
    }
    char line[256];
    int number=0,section=-1;
    bool version=false,ok=true;
    while(ok && fgets(line,sizeof(line),file)) {
        number++;
        size_t length=strlen(line);
        if(length==sizeof(line)-1 && line[length-1]!='\n' && !feof(file)) { ok=reject(error,capacity,path,number,"line is longer than %d characters",(int)sizeof(line)-2); break; }
        char* comment=strpbrk(line,"#;"); if(comment) *comment='\0';
        char* text=weapon_trim(line);
        if(!text[0]) continue;
        if(text[0]=='[') {
            char* close=strchr(text,']');
            if(!close || weapon_trim(close+1)[0]) { ok=reject(error,capacity,path,number,"malformed section header"); break; }
            *close='\0';
            char* inner=weapon_trim(text+1); long index;
            if(strncmp(inner,"profile",7) || !isspace((unsigned char)inner[7])) { ok=reject(error,capacity,path,number,"unknown section [%s]; expected [profile N]",inner); break; }
            if(!version) { ok=reject(error,capacity,path,number,"\"version = %d\" must come before the first section",SWAT_WEAPON_DATA_VERSION); break; }
            char* digits=weapon_trim(inner+7);
            if(!parse_long(digits,&index) || index<0 || index>=SWAT_WEAPON_PROFILES) { ok=reject(error,capacity,path,number,"profile index \"%s\" is not 0..%d",digits,SWAT_WEAPON_PROFILES-1); break; }
            if(sections[index]) { ok=reject(error,capacity,path,number,"[profile %ld] appears twice",index); break; }
            sections[index]=true; section=(int)index; continue;
        }
        char* equals=strchr(text,'=');
        if(!equals) { ok=reject(error,capacity,path,number,"expected \"key = value\""); break; }
        *equals='\0';
        char* key=weapon_trim(text); char* value=weapon_trim(equals+1);
        if(!strcmp(key,"version")) {
            long v;
            if(version || section>=0) { ok=reject(error,capacity,path,number,"version must appear once, before any section"); break; }
            if(!parse_long(value,&v) || v!=SWAT_WEAPON_DATA_VERSION) { ok=reject(error,capacity,path,number,"unsupported version \"%s\"; this build reads version %d",value,SWAT_WEAPON_DATA_VERSION); break; }
            version=true; continue;
        }
        if(section<0) { ok=reject(error,capacity,path,number,"key \"%s\" is outside a [profile N] section",key); break; }
        int k=0; while(k<WEAPON_FIELDS && strcmp(weapon_fields[k].key,key)) k++;
        if(k==WEAPON_FIELDS) { ok=reject(error,capacity,path,number,"unknown key \"%s\"",key); break; }
        if(seen[section]&(1u<<k)) { ok=reject(error,capacity,path,number,"key \"%s\" appears twice in [profile %d]",key,section); break; }
        seen[section]|=1u<<k;
        const WeaponField* f=&weapon_fields[k];
        SwatWeaponDef* d=&defs[section];
        if(f->kind==FIELD_NAME) {
            size_t n=strlen(value); bool printable=n>=(size_t)f->lo && n<=(size_t)f->hi;
            for(size_t c=0;printable && c<n;c++) printable=value[c]>=0x20 && value[c]<=0x7e;
            if(!printable) { ok=reject(error,capacity,path,number,"name must be 1..%d printable ASCII characters",(int)f->hi); break; }
            memcpy(names[section],value,n+1);
        } else if(f->kind==FIELD_FLOAT) {
            char* end=NULL; errno=0; float v=strtof(value,&end);
            if(end==value || *end || errno || !isfinite(v)) { ok=reject(error,capacity,path,number,"%s needs a finite number, got \"%s\"",key,value); break; }
            if((double)v<f->lo || (double)v>f->hi) { ok=reject(error,capacity,path,number,"%s = %s is outside %g..%g",key,value,f->lo,f->hi); break; }
            v+=0.0f; // -0 and 0 behave alike; keep one representation for the digest
            memcpy(field_mut(d,f),&v,sizeof(v));
        } else {
            long v;
            if(!parse_long(value,&v)) { ok=reject(error,capacity,path,number,"%s needs an integer, got \"%s\"",key,value); break; }
            if(v<(long)f->lo || v>(long)f->hi) { ok=reject(error,capacity,path,number,"%s = %ld is outside %ld..%ld",key,v,(long)f->lo,(long)f->hi); break; }
            if(f->kind==FIELD_BOOL) *(bool*)field_mut(d,f)=v!=0;
            else *(int*)field_mut(d,f)=(int)v;
        }
    }
    bool failed=ferror(file)!=0;
    fclose(file);
    if(!ok) return false;
    if(failed) return reject(error,capacity,path,number,"read error");
    if(!version) return reject(error,capacity,path,0,"missing \"version = %d\"",SWAT_WEAPON_DATA_VERSION);
    // The carbine's reach and sight come from the measured rigid rifle, which
    // also drives authoritative clearance and ADS; see WEAPON_ART.md.
    const SwatWeaponDef* carbine=&swat_weapon_defs[0];
    if(memcmp(&defs[0].barrel,&carbine->barrel,sizeof(float)) || memcmp(&defs[0].sight_height,&carbine->sight_height,sizeof(float)))
        return reject(error,capacity,path,0,"profile 0 barrel and sight_height are fixed by the measured carbine (rifle_geometry.h)");
    for(int i=0;i<SWAT_WEAPON_PROFILES;i++) {
        memcpy(loaded_names[i],names[i],sizeof(names[i]));
        loaded_defs[i]=defs[i]; loaded_defs[i].name=loaded_names[i];
    }
    if(tables_equal(loaded_defs,swat_weapon_defs)) swat_weapons_reset();
    else { active_defs=loaded_defs; active_id=table_digest(loaded_defs)|0x80000000u; }
    return true;
}

bool swat_weapons_save(const char* path) {
    if(!path || !path[0]) return false;
    FILE* file=fopen(path,"w");
    if(!file) return false;
    bool ok=fprintf(file,"# SWAT: Gold Element weapon profiles. Omitted keys keep the compiled values.\nversion = %d\n",
        SWAT_WEAPON_DATA_VERSION)>0;
    for(int i=0;ok && i<SWAT_WEAPON_PROFILES;i++) {
        const SwatWeaponDef* d=&active_defs[i];
        ok=fprintf(file,"\n[profile %d]\n",i)>0;
        for(int k=0;ok && k<WEAPON_FIELDS;k++) {
            const WeaponField* f=&weapon_fields[k]; const void* p=field_at(d,f);
            if(f->kind==FIELD_NAME) ok=fprintf(file,"%s = %s\n",f->key,d->name)>0;
            else if(f->kind==FIELD_INT) ok=fprintf(file,"%s = %d\n",f->key,*(const int*)p)>0;
            else if(f->kind==FIELD_BOOL) ok=fprintf(file,"%s = %d\n",f->key,*(const bool*)p ? 1 : 0)>0;
            else ok=fprintf(file,"%s = %.9g\n",f->key,(double)*(const float*)p)>0; // %.9g round-trips float32
        }
    }
    if(fflush(file)!=0) ok=false;
    if(fclose(file)!=0) ok=false;
    return ok;
}

const SwatWeaponDef* swat_arsenal_def(const SwatArsenal* a,int slot) {
    return swat_weapon_def(slot==1 ? 1 : a->primary);
}
void swat_weapons_primary(SwatArsenal* a,int definition) {
    a->primary=definition>=0 && definition<SWAT_WEAPON_PROFILES && definition!=1 ? definition : 0;
    const SwatWeaponDef* d=swat_arsenal_def(a,0);
    a->slots[0]=(SwatWeapon){.magazine=d->capacity,.reserve=d->capacity*3,.chambered=true,.mode=SWAT_SEMI,.magazine_seated=true};
    if(a->primary==10) a->slots[0].reserve=0; // Ten independently fired probes, no automatic cartridge refill.
    a->equip_remaining=d->equip_ticks;
}

void swat_weapons_init(SwatArsenal* a, uint32_t seed) {
    memset(a,0,sizeof(*a));
    a->rng = seed ? seed : 1;
    for (int i=0;i<2;i++) {
        a->slots[i].magazine = active_defs[i].capacity;
        a->slots[i].reserve = active_defs[i].capacity*3;
        a->slots[i].chambered = true;
        a->slots[i].magazine_seated=true;
        a->slots[i].mode = SWAT_SEMI;
    }
}

bool swat_weapons_busy(const SwatArsenal* a) {
    return a->equip_remaining > 0 || a->slots[a->active].reload_remaining > 0;
}

int swat_weapon_rounds(const SwatWeapon* w) {
    int rounds=w->magazine+w->reserve+(w->chambered ? 1 : 0);
    for(int i=0;i<w->magazine_count;i++) rounds+=w->magazines[i];
    return rounds;
}

int swat_reload_stage_end(int duration,SwatReloadStage stage) {
    if(stage==SWAT_RELOAD_REMOVE) return duration/4;
    if(stage==SWAT_RELOAD_INSERT) return duration*2/3;
    return stage==SWAT_RELOAD_CHAMBER ? duration : 0;
}

void swat_weapons_cancel_reload(SwatWeapon* w) {
    w->reload_remaining=w->reload_duration=0;
    w->reload_stage=SWAT_RELOAD_IDLE;
}

void swat_weapons_magazines(SwatWeapon* w,bool enabled,int capacity) {
    if(w->use_magazines==enabled || w->reload_remaining) return;
    if(enabled) {
        w->magazine_count=0;
        while(w->reserve && w->magazine_count<SWAT_MAGAZINES) {
            int rounds=w->reserve<capacity ? w->reserve : capacity;
            w->magazines[w->magazine_count++]=rounds; w->reserve-=rounds;
        }
    } else {
        for(int i=0;i<w->magazine_count;i++) w->reserve+=w->magazines[i];
        memset(w->magazines,0,sizeof(w->magazines)); w->magazine_count=0;
    }
    w->use_magazines=enabled;
}

static void reload_remove(SwatWeapon* w) {
    if(!w->magazine_seated) return;
    if(w->use_magazines && w->magazine_count<SWAT_MAGAZINES) w->magazines[w->magazine_count++]=w->magazine;
    else w->reserve+=w->magazine;
    w->magazine=0; w->magazine_seated=false;
}

static void reload_insert(SwatWeapon* w,int capacity) {
    if(w->magazine_seated) return;
    if(w->use_magazines && w->magazine_count) {
        int best=0;
        for(int i=1;i<w->magazine_count;i++) if(w->magazines[i]>w->magazines[best]) best=i;
        w->magazine=w->magazines[best];
        w->magazines[best]=w->magazines[--w->magazine_count]; w->magazines[w->magazine_count]=0;
    } else {
        int rounds=w->reserve<capacity ? w->reserve : capacity;
        w->magazine=rounds; w->reserve-=rounds;
    }
    w->magazine_seated=true;
}

SwatShot swat_weapons_step(SwatArsenal* a, const SwatInput* in,
                           float ads, float speed, bool grounded, bool blocked) {
    SwatShot shot = {0};
    bool fire_edge = in->fire && !a->last_fire;
    bool reload_edge = in->reload && !a->last_reload;
    bool selector_edge = in->selector && !a->last_selector;
    a->last_fire = in->fire;
    a->last_reload = in->reload;
    a->last_selector = in->selector;
    for (int i=0;i<2;i++) {
        a->slots[i].events=0;
        if (a->slots[i].cooldown > 0) a->slots[i].cooldown--;
    }
    if (a->equip_remaining > 0) a->equip_remaining--;

    int requested = in->weapon == 1 ? 0 : (in->weapon == 2 ? 1 : a->active);
    if (requested != a->active) {
        if(a->slots[a->active].reload_remaining) a->slots[a->active].events|=SWAT_WEAPON_EVENT_RELOAD_SWITCHED;
        swat_weapons_cancel_reload(&a->slots[a->active]);
        a->fire_buffer_ticks=a->reload_buffer_ticks=0;
        a->active = requested;
        a->equip_remaining = swat_arsenal_def(a,requested)->equip_ticks;
    }
    SwatWeapon* w = &a->slots[a->active];
    const SwatWeaponDef* def = swat_arsenal_def(a,a->active);
    if (selector_edge) {
        w->mode = (SwatFireMode)(((int)w->mode + 1) % (def->automatic ? 3 : 2));
    }
    if(in->cancel_reload) {
        if(w->reload_remaining) w->events|=SWAT_WEAPON_EVENT_RELOAD_CANCELLED;
        swat_weapons_cancel_reload(w);
    }
    bool was_reloading=w->reload_remaining>0;
    if(was_reloading) {
        w->reload_remaining--;
        int elapsed=w->reload_duration-w->reload_remaining;
        if(elapsed>=swat_reload_stage_end(w->reload_duration,SWAT_RELOAD_REMOVE) && w->reload_stage==SWAT_RELOAD_REMOVE) {
            reload_remove(w); w->reload_stage=SWAT_RELOAD_INSERT;
            w->events|=SWAT_WEAPON_EVENT_MAGAZINE_OUT;
        }
        if(elapsed>=swat_reload_stage_end(w->reload_duration,SWAT_RELOAD_INSERT) && w->reload_stage==SWAT_RELOAD_INSERT) {
            reload_insert(w,def->capacity); w->reload_stage=SWAT_RELOAD_CHAMBER;
            w->events|=SWAT_WEAPON_EVENT_MAGAZINE_IN;
        }
        if(!w->reload_remaining) {
            if(!w->chambered && w->magazine>0) {
                w->magazine--; w->chambered=true;
                w->events|=SWAT_WEAPON_EVENT_ROUND_CHAMBERED;
            }
            swat_weapons_cancel_reload(w);
            w->events|=SWAT_WEAPON_EVENT_RELOAD_DONE;
        }
    }
    if(reload_edge && !was_reloading) a->reload_buffer_ticks=12;
    bool spare=w->reserve>0 || w->magazine_count>0;
    if(a->reload_buffer_ticks>0 && !was_reloading && !a->equip_remaining &&
       ((spare && w->magazine<def->capacity) || (!w->chambered && w->magazine>0) || !w->magazine_seated)) {
        w->reload_duration=w->chambered ? def->reload_ticks : def->empty_reload_ticks;
        w->reload_remaining=w->reload_duration;
        w->reload_stage=w->magazine_seated ? SWAT_RELOAD_REMOVE : SWAT_RELOAD_INSERT;
        a->reload_buffer_ticks=0;
        w->events|=SWAT_WEAPON_EVENT_RELOAD_START;
    }
    if(a->reload_buffer_ticks>0) a->reload_buffer_ticks--;
    if(fire_edge && !blocked && !was_reloading && !swat_weapons_busy(a) && w->mode!=SWAT_SAFE) a->fire_buffer_ticks=8;
    bool fire_request=def->automatic && w->mode==SWAT_AUTO ? in->fire : a->fire_buffer_ticks>0;
    if(a->fire_buffer_ticks>0) a->fire_buffer_ticks--;
    if(blocked || w->mode==SWAT_SAFE || in->cancel_reload) { a->fire_buffer_ticks=0; return shot; }
    if(was_reloading || swat_weapons_busy(a) || w->cooldown>0 || !w->chambered || !fire_request) return shot;
    a->fire_buffer_ticks=0;

    w->chambered = false;
    if (w->magazine > 0) { w->magazine--; w->chambered = true; }
    w->cooldown = def->shot_ticks;
    a->shots++;
    shot.fired = true;
    shot.damage = def->damage;
    shot.range = def->range;
    shot.energy = def->energy;
    float spread = def->hip_spread + (def->ads_spread-def->hip_spread)*swat_clamp(ads,0,1);
    if(a->sight==SWAT_RED_DOT) spread*=1-.12f*ads;
    else if(a->sight==SWAT_OPTIC) spread*=1-.2f*ads;
    spread += fminf(speed,5.0f)*0.18f + (grounded ? 0.0f : 3.0f);
    // Uniform disk around the aim direction, independent RNG for each actor.
    float angle = swat_rand01(&a->rng)*2.0f*SWAT_PI;
    float radius = sqrtf(swat_rand01(&a->rng))*spread*SWAT_RAD;
    b3CosSin spread_angle=swat_sincos(angle);
    shot.pitch_offset = spread_angle.sine*radius;
    shot.yaw_offset = spread_angle.cosine*radius;
    shot.recoil_yaw = (swat_rand01(&a->rng)-0.5f)*def->recoil*0.5f*SWAT_RAD;
    return shot;
}
