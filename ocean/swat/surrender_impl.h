// Suspect surrender and civilian compliance. Included once by encounter.c,
// after the civilian fear helpers it reuses (build.sh lists the sources it
// compiles, so the module lives in a header).
//
// Every input is something the person perceives: commands they heard, the
// officers they can see, guns they can see pointed at them, their own wounds
// and ammunition, and armed allies in sight. Choices are deterministic: a hash
// of the round seed, layout seed, actor and decision count, weighed by the
// person's resolve. No wall clock and no shared RNG stream is consumed.
// Actors driven by an outside policy (mind.external) keep the role contract's
// fixed command rule; their own policy chooses whether to comply or flee.

static float yield_roll(SwatSim* s,int i) {
    SwatMind* mind=&s->actors[i].mind;
    return (civilian_hash(s,i,0x5e11+mind->yield_count++)%1000)/1000.0f;
}

static float suspect_ammo(const SwatActor* a) {
    if(a->gear.disarmed) return 0;
    int rounds=0,full=0;
    for(int slot=0;slot<2;slot++) {
        rounds+=swat_weapon_rounds(&a->arsenal.slots[slot]);
        full+=4*swat_arsenal_def(&a->arsenal,slot)->capacity+1;
    }
    return full>0 ? fminf(1,(float)rounds/(float)full) : 0;
}

// Officers this suspect can see (plus the officer whose command it just heard,
// seen or not), guns among them aimed squarely at it, and armed allies in sight.
SwatPressure swat_suspect_pressure(const SwatSim* s,int i,int commander) {
    const SwatActor* a=&s->actors[i]; const SwatMind* mind=&a->mind;
    SwatPressure p={0};
    p.commands=mind->heard_commands && s->tick-(mind->heard_command_tick-1)<SWAT_YIELD_COMMAND_WINDOW ? mind->heard_commands : 0;
    for(int j=0;j<s->actor_count;j++) {
        const SwatActor* other=&s->actors[j];
        if(j==i || !other->present || !other->alive) continue;
        if(other->role==SWAT_OFFICER || other->role==SWAT_SNIPER) {
            bool seen=swat_sim_actor_visible(s,i,j,24,60*SWAT_RAD);
            if(!seen && j!=commander) continue;
            p.officers++;
            // The suspect sees the muzzle on them only when it sees the officer.
            if(seen && swat_sim_firing_line_clear(s,j,i,24)) p.aimed++;
        } else if(other->role==SWAT_SUSPECT && !other->gear.surrendered && !other->gear.restrained && !other->gear.disarmed &&
                  b3Distance(swat_controller_eye(&a->controller),swat_controller_eye(&other->controller))<10 &&
                  swat_sim_actor_visible(s,i,j,10,60*SWAT_RAD)) p.allies++;
    }
    p.wounds=1-fmaxf(0,a->health)/100;
    p.ammo=suspect_ammo(a);
    // Each count saturates: a fourth officer or command adds nothing more.
    p.pressure=.18f*fminf((float)p.commands,3)+.12f*fminf((float)p.officers,3)+.16f*fminf((float)p.aimed,3)+
        .55f*p.wounds+.4f*(1-p.ammo)-.15f*fminf((float)p.allies,2);
    p.will=mind->resolve;
    return p;
}

static float nearest_officer(const SwatSim* s,int i) {
    float nearest=1e9f;
    for(int j=0;j<s->actor_count;j++) {
        const SwatActor* o=&s->actors[j];
        if(!o->present || !o->alive || (o->role!=SWAT_OFFICER && o->role!=SWAT_SNIPER)) continue;
        nearest=fminf(nearest,b3Distance(swat_body_feet_position(&o->controller.body),swat_body_feet_position(&s->actors[i].controller.body)));
    }
    return nearest;
}

// Pressure above the suspect's will breaks them: genuine compliance, or for a
// still-capable suspect with no gun on them, a feint or dropping the weapon to
// run. Pressure just short of it makes them waver: flight is likelier when no
// gun is aimed at them. Otherwise they fight on.
SwatYield swat_suspect_choose(SwatSim* s,int i,const SwatPressure* p) {
    if(s->actors[i].gear.disarmed) return SWAT_YIELD_COMPLY;
    float roll=yield_roll(s,i),margin=p->pressure-p->will;
    if(margin>=0) {
        float feint=!p->aimed && p->ammo>0 && p->wounds<.5f ? .1f+.35f*p->will : 0;
        float drop=nearest_officer(s,i)>4 ? .25f*(1-p->wounds) : 0;
        return roll<feint ? SWAT_YIELD_FEINT : roll<feint+drop ? SWAT_YIELD_DROP : SWAT_YIELD_COMPLY;
    }
    if(margin>=-.25f) return roll<(p->aimed ? .25f : .6f) ? SWAT_YIELD_FLEE : SWAT_YIELD_FIGHT;
    return SWAT_YIELD_FIGHT;
}

static void suspect_apply(SwatSim* s,int i,SwatYield choice,b3Pos threat) {
    SwatActor* a=&s->actors[i]; SwatMind* mind=&a->mind;
    mind->yield=choice; mind->yield_goal_valid=false; mind->yield_plan_tick=0; mind->yield_from=threat;
    if(choice==SWAT_YIELD_COMPLY) a->gear.surrendered=true;
    else if(choice==SWAT_YIELD_FEINT) {
        a->gear.surrendered=true;
        mind->yield_until=s->tick+SWAT_FEINT_MIN_TICKS+(int)(civilian_hash(s,i,0xfe1+mind->yield_count)%SWAT_FEINT_SPREAD);
    } else if(choice==SWAT_YIELD_DROP) {
        // The weapon leaves the hands now; encounter step drops it as evidence.
        a->gear.disarmed=true; a->arsenal.fire_buffer_ticks=0;
        mind->yield_until=s->tick+SWAT_FLEE_TICKS;
    } else if(choice==SWAT_YIELD_FLEE) mind->yield_until=s->tick+SWAT_FLEE_TICKS;
    if(choice==SWAT_YIELD_COMPLY || choice==SWAT_YIELD_FEINT) civilian_calm(mind);
}

// A panicked civilian may not take in the first shout. Each further command
// in the window makes compliance likelier, and the third always lands. A
// command that does not land stops a flight: the civilian drops and hides.
// One who obeys an order they only heard gets down where it is safe: they
// walk to a wall or corner away from the voice (civilian_cover_spot), then
// kneel with hands shown; with no such spot they kneel where they are.
static void civilian_commanded(SwatSim* s,int i,bool heard_only,b3Vec3 arrival) {
    SwatActor* a=&s->actors[i]; SwatMind* mind=&a->mind;
    bool obey=mind->external || mind->panic==SWAT_PANIC_NONE;
    if(!obey) {
        float chance=mind->heard_commands>=3 ? 1 : .35f+.3f*mind->resolve+.25f*(float)(mind->heard_commands-1);
        obey=yield_roll(s,i)<chance;
    }
    if(!obey) {
        mind->complying=false;
        mind->panic=SWAT_PANIC_COWER; mind->panic_ticks=SWAT_PANIC_TICKS; mind->panic_goal_valid=false;
        return;
    }
    // A policy-driven civilian keeps the role contract's rule: it surrenders.
    if(!heard_only || mind->external) { mind->complying=false; a->gear.surrendered=true; return; }
    if(mind->complying) return; // Already on the way to the chosen spot.
    civilian_calm(mind);
    b3Pos spot;
    if(!civilian_cover_spot(s,i,arrival,&spot)) { a->gear.surrendered=true; return; }
    mind->complying=true; mind->comply_goal=spot; mind->comply_until=s->tick+SWAT_COMPLY_TICKS;
}

// Any officer or sniper this suspect can see now.
static bool suspect_sees_officer(const SwatSim* s,int i) {
    for(int j=0;j<s->actor_count;j++) {
        const SwatActor* o=&s->actors[j];
        if(j!=i && o->present && o->alive && (o->role==SWAT_OFFICER || o->role==SWAT_SNIPER) &&
           swat_sim_actor_visible(s,i,j,24,60*SWAT_RAD)) return true;
    }
    return false;
}

void swat_person_commanded(SwatSim* s,int officer,int i,bool heard_only,b3Vec3 arrival) {
    SwatActor* a=&s->actors[i]; SwatMind* mind=&a->mind;
    if(!a->alive || a->gear.restrained || a->gear.surrendered) return;
    if(!mind->heard_commands || s->tick-(mind->heard_command_tick-1)>=SWAT_YIELD_COMMAND_WINDOW) mind->heard_commands=0;
    mind->heard_commands++; mind->heard_command_tick=s->tick+1;
    if(a->role==SWAT_CIVILIAN) { civilian_commanded(s,i,heard_only,arrival); return; }
    if(a->role!=SWAT_SUSPECT) return;
    if(a->gear.stunned_ticks || a->health<40 || a->gear.disarmed) { suspect_apply(s,i,SWAT_YIELD_COMPLY,swat_controller_eye(&s->actors[officer].controller)); return; }
    if(mind->external) {
        // Role contract (v1 and v2): the fixed command rule for policy-driven suspects.
        if(mind->resolve<.48f) a->gear.surrendered=true;
        return;
    }
    SwatPressure p=swat_suspect_pressure(s,i,officer);
    SwatYield choice=swat_suspect_choose(s,i,&p);
    suspect_apply(s,i,choice,swat_controller_eye(&s->actors[officer].controller));
    // A suspect who defies an order from police out of sight now knows
    // roughly where they are: investigate a coarse point 4 m along the heard
    // bearing, as for a heard shot. Shouting gives the police away.
    if((choice==SWAT_YIELD_FIGHT || choice==SWAT_YIELD_FLEE) && !suspect_sees_officer(s,i)) {
        float sector=SWAT_PI/8,bearing=roundf(atan2f(arrival.z,arrival.x)/sector)*sector;
        mind->memory=b3OffsetPos(swat_body_feet_position(&a->controller.body),swat_mul(swat_direction(bearing,0),4));
        mind->memory_ticks=180;
    }
}

// A gun the suspect can see pointed at them, or an officer close enough to
// feel and hear, keeps a feint on its knees. A muzzle behind them goes unnoticed.
static bool officer_guarding(const SwatSim* s,int i) {
    b3Pos feet=swat_body_feet_position(&s->actors[i].controller.body);
    for(int j=0;j<s->actor_count;j++) {
        const SwatActor* o=&s->actors[j];
        if(!o->present || !o->alive || (o->role!=SWAT_OFFICER && o->role!=SWAT_SNIPER)) continue;
        if(b3Distance(swat_body_feet_position(&o->controller.body),feet)<3) return true;
        if(swat_sim_firing_line_clear(s,j,i,24) && swat_sim_actor_visible(s,i,j,24,60*SWAT_RAD)) return true;
    }
    return false;
}

// Scripted suspect behaviour outside ordinary engagement. Returns true when
// this tick's input is decided here.
static bool suspect_yield_inputs(SwatSim* s,int i,SwatInput* in,int target) {
    SwatActor* a=&s->actors[i]; SwatMind* mind=&a->mind;
    if(mind->external) return false;
    if(mind->yield==SWAT_YIELD_FEINT && a->gear.surrendered) {
        // Kneel with hands shown until the time has come and no gun covers
        // them, then come up with the weapon toward the officer last seen.
        if(s->tick<mind->yield_until || officer_guarding(s,i)) return true;
        a->gear.surrendered=false; mind->yield=SWAT_YIELD_FIGHT;
        mind->memory=mind->yield_from; mind->memory_ticks=180;
        // Drawing takes the ordinary attack confirmation time before a shot.
        a->visible_ticks=0;
        return false;
    }
    // A fresh wound or an empty weapon in a fight forces a decision.
    bool wounded=s->force[i].taken>0,dry=!a->gear.disarmed && !mind->yield_dry && suspect_ammo(a)<=0;
    mind->yield_dry|=dry;
    if((wounded || dry) && target>=0 && !a->gear.surrendered) {
        SwatPressure p=swat_suspect_pressure(s,i,-1);
        suspect_apply(s,i,swat_suspect_choose(s,i,&p),swat_controller_eye(&s->actors[target].controller));
        if(a->gear.surrendered) return true;
    }
    bool running=(mind->yield==SWAT_YIELD_FLEE || mind->yield==SWAT_YIELD_DROP) && s->tick<mind->yield_until;
    if(!running && !a->gear.disarmed) {
        if(mind->yield!=SWAT_YIELD_FLEE) return false;
        // Out of breath, an armed runner turns back toward the officer last
        // seen (up to 1 s), then investigates that point and fights again.
        if(target<0 && s->tick<mind->yield_until+SWAT_LOOK_BACK_TICKS) {
            b3Vec3 d=b3SubPos(mind->yield_from,swat_controller_eye(&a->controller));
            in->yaw_delta=swat_clamp(swat_angle(b3Atan2(d.z,d.x)-a->controller.yaw),-3*SWAT_RAD,3*SWAT_RAD);
            return true;
        }
        mind->yield=SWAT_YIELD_NONE; mind->memory=mind->yield_from; mind->memory_ticks=180;
        return false;
    }
    // Flight: away from the officer last seen, to cover in the room or out of
    // a far doorway, at a run. An unarmed suspect never engages; once out of
    // time they hide where they are and wait for the next command.
    mind->state=SWAT_FLEE;
    if(target>=0) mind->yield_from=swat_controller_eye(&s->actors[target].controller);
    if(!running) { in->crouch=true; return true; }
    if(s->tick>=mind->yield_plan_tick) {
        mind->yield_goal_valid=civilian_refuge(s,i,mind->yield_from,true,&mind->yield_goal);
        mind->yield_plan_tick=s->tick+SWAT_PANIC_REPLAN;
    }
    b3Vec3 d=b3SubPos(mind->yield_goal,swat_body_feet_position(&a->controller.body));
    if(!mind->yield_goal_valid || (hypotf(d.x,d.z)<.45f && fabsf(d.y)<.45f)) { in->crouch=true; return true; }
    move_to(s,i,mind->yield_goal,in);
    in->gait=SWAT_SPRINT; in->aim=in->fire=false;
    return true;
}
