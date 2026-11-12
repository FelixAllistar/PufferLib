// Rules-of-engagement perception and force judgement. Included once by sim.c
// (build.sh lists the sources it compiles, so the module lives in a header).
// See roe.h for the states and ARCHITECTURE.md for the transition table.

static const char* const roe_names[SWAT_ROE_STATES]={
    "unknown","compliant","surrendered","restrained","fleeing","hostile-armed","hostile-firing","down"};
const char* swat_roe_state_name(SwatRoeState state) {
    return state>=0 && state<SWAT_ROE_STATES ? roe_names[state] : "invalid";
}

bool swat_roe_transition_legal(SwatRoeState from,SwatRoeState to) {
    if(from<0 || from>=SWAT_ROE_STATES || to<0 || to>=SWAT_ROE_STATES) return false;
    if(from==to) return true;
    // Death is final, and an assessment once made is never forgotten.
    if(from==SWAT_ROE_DOWN || to==SWAT_ROE_UNKNOWN) return false;
    if(to==SWAT_ROE_DOWN) return true;
    // Cuffs come off only by death. Hands-on restraint overrides any earlier
    // view, because officers may not have seen the surrender that preceded it.
    if(from==SWAT_ROE_RESTRAINED) return false;
    if(to==SWAT_ROE_RESTRAINED) return true;
    // A genuine surrender leads only to cuffs. Leaving it means the surrender
    // was feigned: the weapon comes out, or the person runs.
    if(from==SWAT_ROE_SURRENDERED)
        return to==SWAT_ROE_HOSTILE_ARMED || to==SWAT_ROE_HOSTILE_FIRING || to==SWAT_ROE_FLEEING;
    // Someone seen unarmed does not become armed: civilians never carry, and a
    // suspect who dropped the weapon cannot pick it back up.
    if(from==SWAT_ROE_COMPLIANT) return to!=SWAT_ROE_HOSTILE_ARMED && to!=SWAT_ROE_HOSTILE_FIRING;
    return true;
}

bool swat_roe_lethal_threat(SwatRoeState state) {
    return state==SWAT_ROE_HOSTILE_ARMED || state==SWAT_ROE_HOSTILE_FIRING;
}

// Holding a weapon in a way an observer can see: for the suspects and
// civilians judged here, a suspect who has neither dropped it nor put their
// hands up (swat_actor_shows_weapon). A feigned surrender hides the weapon.
static bool roe_shows_weapon(const SwatActor* a) {
    return a->role==SWAT_SUSPECT && swat_actor_shows_weapon(a);
}

static SwatRoeState roe_classify(const SwatSim* s,int person,int observer,bool raised) {
    const SwatActor* a=&s->actors[person];
    const SwatRoePerson* r=&s->roe[person];
    if(a->gear.surrendered) return SWAT_ROE_SURRENDERED;
    bool armed=roe_shows_weapon(a);
    int age=s->tick-a->last_shot_tick;
    // Muzzle flash and recoil need only sight of the shooter.
    if(armed && a->last_shot_tick>=0 && age>=0 && age<SWAT_ROE_FIRING_TICKS) return SWAT_ROE_HOSTILE_FIRING;
    if(armed && raised) return SWAT_ROE_HOSTILE_ARMED;
    // An armed person who has already shown lethal intent stays a threat
    // until they surrender or are seen without the weapon.
    if(armed && (r->state==SWAT_ROE_HOSTILE_ARMED || r->state==SWAT_ROE_HOSTILE_FIRING)) return SWAT_ROE_HOSTILE_ARMED;
    b3Vec3 v=b3Body_GetLinearVelocity(a->controller.body.body);
    b3Vec3 away=b3SubPos(swat_body_feet_position(&a->controller.body),swat_body_feet_position(&s->actors[observer].controller.body));
    away.y=0; float span=hypotf(away.x,away.z);
    if(span>.05f && (v.x*away.x+v.z*away.z)/span>SWAT_ROE_FLEE_SPEED) return SWAT_ROE_FLEEING;
    if(armed && r->challenged_tick && s->tick-(r->challenged_tick-1)>=SWAT_ROE_CHALLENGE_TICKS) return SWAT_ROE_HOSTILE_ARMED;
    if(!armed) return SWAT_ROE_COMPLIANT;
    return r->state;
}

static void roe_set(SwatSim* s,int person,SwatRoeState next) {
    SwatRoePerson* r=&s->roe[person];
    if(next==r->state) return;
    if(!swat_roe_transition_legal(r->state,next)) { s->roe_refused++; return; }
    if(r->state==SWAT_ROE_SURRENDERED && next!=SWAT_ROE_RESTRAINED && next!=SWAT_ROE_DOWN) s->debrief.feints++;
    r->state=next; r->since=s->tick;
}

void swat_roe_step(SwatSim* s) {
    if(!s->config.tactical_rules) return;
    for(int p=0;p<s->actor_count;p++) {
        const SwatActor* a=&s->actors[p];
        if(!a->present || (a->role!=SWAT_SUSPECT && a->role!=SWAT_CIVILIAN)) continue;
        SwatRoePerson* r=&s->roe[p];
        if(!a->alive) { roe_set(s,p,SWAT_ROE_DOWN); continue; }
        if(a->gear.restrained) { roe_set(s,p,SWAT_ROE_RESTRAINED); continue; }
        // The nearest officer or sniper with sight of the person judges motion;
        // any of them seeing a raised weapon makes it a team observation.
        int observer=-1; float nearest=1e9f; bool raised=false;
        bool aiming=roe_shows_weapon(a) && a->controller.ads>=.5f && fabsf(a->controller.ready_blend)<=.12f;
        for(int o=0;o<s->actor_count;o++) {
            const SwatActor* police=&s->actors[o];
            if(!police->present || !police->alive || (police->role!=SWAT_OFFICER && police->role!=SWAT_SNIPER)) continue;
            float range=police->role==SWAT_SNIPER ? 120 : 24;
            if(!swat_sim_actor_visible(s,o,p,range,60*SWAT_RAD)) continue;
            float distance=b3Distance(swat_controller_eye(&police->controller),swat_controller_eye(&a->controller));
            if(distance<nearest) { nearest=distance; observer=o; }
            if(aiming && !raised) raised=swat_sim_observed_weapon_threat(s,o,p,range);
        }
        if(observer<0) {
            // Unseen: the last assessment stands, except that a shooter last
            // seen firing long ago is remembered as armed rather than firing.
            if(r->state==SWAT_ROE_HOSTILE_FIRING && r->seen_tick && s->tick-(r->seen_tick-1)>=SWAT_ROE_FIRING_TICKS)
                roe_set(s,p,SWAT_ROE_HOSTILE_ARMED);
            continue;
        }
        r->seen_tick=s->tick+1;
        roe_set(s,p,roe_classify(s,p,observer,raised));
    }
}

void swat_roe_challenge(SwatSim* s,int person) {
    if(person>=0 && person<s->actor_count) s->roe[person].challenged_tick=s->tick+1;
}

void swat_roe_force(SwatSim* s,int victim,int shooter,bool less_lethal) {
    if(!s->config.tactical_rules || less_lethal || victim<0 || victim>=s->actor_count ||
       shooter<0 || shooter>=s->actor_count || shooter==victim) return;
    const SwatActor* a=&s->actors[victim];
    SwatRole role=s->actors[shooter].role;
    if(!a->alive || (a->role!=SWAT_SUSPECT && a->role!=SWAT_CIVILIAN) || (role!=SWAT_OFFICER && role!=SWAT_SNIPER)) return;
    if(swat_roe_lethal_threat(s->roe[victim].state)) return;
    // A burst is one decision, as for roe_violations.
    int* last=&s->roe_lethal_tick[shooter][victim];
    if(!*last || s->tick-(*last-1)>=SWAT_ROE_ENGAGEMENT_TICKS) s->debrief.lethal_violations++;
    *last=s->tick+1; s->force[shooter].lethal++;
}
