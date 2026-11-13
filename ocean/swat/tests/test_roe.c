// Rules of engagement, suspect surrender and civilian compliance. Every case
// runs the authoritative simulation; fixtures place people and set personality
// (resolve), never perceived ROE states or decisions.
#include "protocol.h"
#include "replay.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SwatSim sim;
static SwatInput controlled[SWAT_MAX_ACTORS];
static bool driven[SWAT_MAX_ACTORS];

// Officer 0 at the origin facing +x, suspect 1 at 6 m facing back, civilian 2
// off the line of fire. Flat open floor: perception is limited only by bodies.
static void fixture(uint32_t seed) {
    memset(&sim,0,sizeof(sim)); memset(driven,0,sizeof(driven));
    sim.config=swat_default_config(); sim.config.tactical_rules=true; sim.config.hostile_fire=false; sim.config.max_ticks=100000;
    sim.reset_seed=seed;
    sim.extraction=(b3Pos){-7,0,0}; sim.mission.staging=(b3Pos){0,0,0}; swat_world_init(&sim.world);
    swat_world_box(&sim.world,(b3Pos){0,-.5f,0},swat_v(14,.5f,14),SWAT_CONCRETE,0);
    swat_sim_spawn_actor(&sim,0,SWAT_OFFICER,(b3Pos){0,0,0},0);
    swat_sim_spawn_actor(&sim,1,SWAT_SUSPECT,(b3Pos){6,0,0},SWAT_PI);
    swat_sim_spawn_actor(&sim,2,SWAT_CIVILIAN,(b3Pos){3,0,4},SWAT_PI);
    sim.actor_count=3;
}
// The player's input, plus outside inputs for any actor marked driven.
static void step(SwatInput player) {
    SwatInput inputs[SWAT_MAX_ACTORS]; swat_sim_bot_inputs(&sim,inputs);
    inputs[0]=player;
    for(int i=1;i<sim.actor_count;i++) if(driven[i]) inputs[i]=controlled[i];
    SwatRoeState before[SWAT_MAX_ACTORS]; for(int i=0;i<sim.actor_count;i++) before[i]=sim.roe[i].state;
    swat_sim_step_inputs(&sim,inputs);
    for(int i=0;i<sim.actor_count;i++) assert(swat_roe_transition_legal(before[i],sim.roe[i].state));
}
static void idle(int ticks) { for(int t=0;t<ticks;t++) step(swat_neutral_input()); }
static void face(int actor,b3Pos point) {
    b3Vec3 d=b3SubPos(point,swat_controller_eye(&sim.actors[actor].controller));
    sim.actors[actor].controller.yaw=b3Atan2(d.z,d.x);
    sim.actors[actor].controller.pitch=b3Atan2(d.y,hypotf(d.x,d.z));
}
static b3Pos chest(int actor) { return b3OffsetPos(swat_controller_eye(&sim.actors[actor].controller),swat_v(0,-.25f,0)); }
static b3Pos feet(int actor) { return swat_body_feet_position(&sim.actors[actor].controller.body); }
static void command(void) { SwatInput in=swat_neutral_input(); in.command=true; step(in); step(swat_neutral_input()); }
static SwatRoeState roe(int actor) { return sim.roe[actor].state; }

static void transition_table(void) {
    int legal=0;
    for(int from=0;from<SWAT_ROE_STATES;from++) for(int to=0;to<SWAT_ROE_STATES;to++) {
        bool ok=swat_roe_transition_legal(from,to);
        legal+=ok;
        if(from==to) assert(ok);
        if(from==SWAT_ROE_DOWN && to!=from) assert(!ok);
        if(to==SWAT_ROE_UNKNOWN && from!=to) assert(!ok);
        if(from==SWAT_ROE_RESTRAINED && to!=from) assert(ok==(to==SWAT_ROE_DOWN));
        if(from!=SWAT_ROE_DOWN && to==SWAT_ROE_RESTRAINED) assert(ok);
    }
    assert(!swat_roe_transition_legal(SWAT_ROE_SURRENDERED,SWAT_ROE_COMPLIANT));
    assert(swat_roe_transition_legal(SWAT_ROE_SURRENDERED,SWAT_ROE_HOSTILE_ARMED));
    assert(!swat_roe_transition_legal(SWAT_ROE_COMPLIANT,SWAT_ROE_HOSTILE_ARMED));
    assert(!swat_roe_transition_legal(SWAT_ROE_COMPLIANT,SWAT_ROE_HOSTILE_FIRING));
    assert(swat_roe_transition_legal(SWAT_ROE_HOSTILE_FIRING,SWAT_ROE_HOSTILE_ARMED));
    assert(swat_roe_transition_legal(SWAT_ROE_HOSTILE_ARMED,SWAT_ROE_COMPLIANT));
    assert(!swat_roe_transition_legal(SWAT_ROE_STATES,SWAT_ROE_DOWN));
    for(int s=0;s<SWAT_ROE_STATES;s++)
        assert(swat_roe_lethal_threat(s)==(s==SWAT_ROE_HOSTILE_ARMED || s==SWAT_ROE_HOSTILE_FIRING));
    printf("PASS roe table: %d legal of %d pairs; death final, never back to unknown, cuffs only to death, feints leave surrender, unarmed stays unarmed\n",
        legal,SWAT_ROE_STATES*SWAT_ROE_STATES);
}

// The suspect is held still by an outside neutral input (weapon lowered) so
// each perceived change has one cause.
static void perception(void) {
    fixture(11); driven[1]=true; controlled[1]=swat_neutral_input();
    sim.actors[1].mind.external=true; sim.actors[1].mind.resolve=.9f;
    face(0,(b3Pos){-6,1.6f,0}); idle(5);
    assert(roe(1)==SWAT_ROE_UNKNOWN && roe(2)==SWAT_ROE_UNKNOWN && !sim.roe[1].seen_tick);
    // Seen holding a lowered weapon: armed, but not yet a threat.
    face(0,chest(1)); idle(3);
    assert(sim.roe[1].seen_tick && roe(1)==SWAT_ROE_UNKNOWN);
    // Challenged and still armed after the response time: noncompliant threat.
    command(); assert(!sim.actors[1].gear.surrendered && sim.roe[1].challenged_tick);
    idle(SWAT_ROE_CHALLENGE_TICKS-4); assert(roe(1)==SWAT_ROE_UNKNOWN);
    idle(4); assert(roe(1)==SWAT_ROE_HOSTILE_ARMED);
    // Seen firing (into the ceiling): firing, decaying to armed when unseen.
    face(1,b3OffsetPos(swat_controller_eye(&sim.actors[1].controller),swat_v(-1,3,0)));
    controlled[1].fire=true; step(swat_neutral_input()); controlled[1].fire=false;
    for(int t=0;t<20 && sim.actors[1].last_shot_tick<0;t++) step(swat_neutral_input());
    assert(sim.actors[1].last_shot_tick>=0); idle(1); assert(roe(1)==SWAT_ROE_HOSTILE_FIRING);
    face(0,(b3Pos){-6,1.6f,0}); idle(SWAT_ROE_FIRING_TICKS+2); assert(roe(1)==SWAT_ROE_HOSTILE_ARMED);
    // Kneels: surrendered; cuffed: restrained, whether or not anyone watched.
    face(0,chest(1)); sim.actors[1].gear.surrendered=true; idle(2); assert(roe(1)==SWAT_ROE_SURRENDERED);
    face(0,(b3Pos){-6,1.6f,0}); sim.actors[1].gear.restrained=true; idle(2); assert(roe(1)==SWAT_ROE_RESTRAINED);
    // The civilian, seen standing: compliant; running from the officer: fleeing.
    face(0,chest(2)); driven[2]=true; controlled[2]=swat_neutral_input(); idle(3); assert(roe(2)==SWAT_ROE_COMPLIANT);
    face(2,b3OffsetPos(feet(2),swat_v(3,0,4))); controlled[2].forward=1; controlled[2].gait=SWAT_SPRINT;
    for(int t=0;t<40 && roe(2)!=SWAT_ROE_FLEEING;t++){face(0,chest(2));step(swat_neutral_input());}
    assert(roe(2)==SWAT_ROE_FLEEING);
    controlled[2]=swat_neutral_input(); for(int t=0;t<60 && roe(2)!=SWAT_ROE_COMPLIANT;t++){face(0,chest(2));step(swat_neutral_input());}
    assert(roe(2)==SWAT_ROE_COMPLIANT);
    // An impossible perceived change is refused and counted, not applied.
    assert(!sim.roe_refused); sim.roe[2].state=SWAT_ROE_RESTRAINED; face(0,chest(2)); idle(1);
    assert(sim.roe_refused==1 && roe(2)==SWAT_ROE_RESTRAINED); sim.roe[2].state=SWAT_ROE_COMPLIANT; sim.roe_refused=0;
    swat_sim_damage_region(&sim,1,-1,200,SWAT_HEAD,false); idle(1); assert(roe(1)==SWAT_ROE_DOWN);
    swat_sim_close(&sim);
    puts("PASS roe perception: unseen unknown, armed unchallenged unknown, ignored command -> hostile-armed, seen firing -> hostile-firing -> armed when unseen, surrendered, restrained, compliant <-> fleeing, refused illegal change, down");
}

// A suspect who raises a weapon at the officer is a lethal threat at once.
static void raised_weapon(void) {
    fixture(12); idle(80);
    assert(sim.actors[1].controller.ads>=.5f && roe(1)==SWAT_ROE_HOSTILE_ARMED);
    swat_sim_close(&sim);
    puts("PASS roe raised weapon: a suspect aiming at a seen officer becomes hostile-armed before any shot");
}

static void lethal_force(void) {
    fixture(13); driven[1]=true; controlled[1]=swat_neutral_input(); sim.actors[1].mind.external=true; sim.actors[1].mind.resolve=.9f;
    face(0,chest(1)); idle(3); assert(roe(1)==SWAT_ROE_UNKNOWN);
    // Less-lethal force against an unchallenged armed suspect is lawful.
    swat_sim_damage_region(&sim,1,0,5,SWAT_LEGS,true);
    assert(!sim.debrief.lethal_violations && !sim.debrief.roe_violations);
    swat_sim_close(&sim);
    // A real carbine round into an armed suspect who threatened nobody.
    fixture(13); driven[1]=true; controlled[1]=swat_neutral_input(); sim.actors[1].mind.external=true; sim.actors[1].mind.resolve=.9f;
    face(0,chest(1)); idle(3);
    SwatInput fire=swat_neutral_input(); fire.fire=true; step(fire);
    for(int t=0;t<30 && sim.actors[1].health==100;t++) step(swat_neutral_input());
    assert(sim.actors[1].health<100 && sim.debrief.lethal_violations==1 && !sim.debrief.roe_violations);
    // A follow-up shot inside the engagement window is the same violation.
    float health=sim.actors[1].health;
    idle(20); face(0,chest(1)); step(fire); for(int t=0;t<30 && sim.actors[1].health==health;t++) step(swat_neutral_input());
    assert(sim.debrief.lethal_violations==1);
    swat_sim_close(&sim);
    // The same round after the suspect raised the weapon at the officer is lawful.
    fixture(13); idle(80); assert(roe(1)==SWAT_ROE_HOSTILE_ARMED);
    face(0,chest(1)); step(fire); for(int t=0;t<30 && sim.actors[1].health==100;t++) step(swat_neutral_input());
    assert(sim.actors[1].health<100 && !sim.debrief.lethal_violations);
    swat_sim_close(&sim);
    // Lethal force on a civilian is both a protected-person and a lethal violation.
    fixture(13); face(0,chest(2)); idle(3);
    swat_sim_damage_region(&sim,2,0,10,SWAT_ARMS,false);
    assert(sim.debrief.lethal_violations==1 && sim.debrief.roe_violations==1);
    // Unattributed damage (a fall, a suspect's round) is no officer violation.
    swat_sim_damage_region(&sim,2,-1,10,SWAT_ARMS,false); swat_sim_damage_region(&sim,2,1,10,SWAT_ARMS,false);
    assert(sim.debrief.lethal_violations==1);
    swat_sim_close(&sim);
    puts("PASS roe lethal force: less-lethal lawful, round into unchallenged armed suspect is one lethal violation per engagement, lawful once the weapon is raised, civilian hit counts both ledgers, suspects' and unattributed harm excluded");
}

static void pressure(void) {
    fixture(21); face(0,chest(1)); idle(2);
    // Rest pose: one seen officer, not aimed at, full ammunition, no wounds.
    face(0,(b3Pos){6,1,-1.2f});
    SwatPressure base=swat_suspect_pressure(&sim,1,-1);
    assert(base.officers==1 && !base.aimed && !base.commands && !base.allies && base.ammo==1 && base.wounds==0);
    face(0,chest(1)); SwatPressure aimed=swat_suspect_pressure(&sim,1,-1);
    assert(aimed.aimed==1 && aimed.pressure>base.pressure);
    sim.actors[1].health=50; SwatPressure hurt=swat_suspect_pressure(&sim,1,-1);
    assert(hurt.wounds==.5f && hurt.pressure>aimed.pressure);
    sim.actors[1].arsenal.slots[0].magazine=sim.actors[1].arsenal.slots[0].reserve=0; sim.actors[1].arsenal.slots[0].chambered=false;
    sim.actors[1].arsenal.slots[1].magazine=sim.actors[1].arsenal.slots[1].reserve=0; sim.actors[1].arsenal.slots[1].chambered=false;
    SwatPressure dry=swat_suspect_pressure(&sim,1,-1);
    assert(dry.ammo==0 && dry.pressure>hurt.pressure);
    // An armed ally in sight steadies the suspect.
    swat_sim_spawn_actor(&sim,3,SWAT_SUSPECT,(b3Pos){7,0,1.5f},SWAT_PI); sim.actor_count=4; idle(1);
    face(1,chest(3)); SwatPressure backed=swat_suspect_pressure(&sim,1,-1);
    assert(backed.allies==1);
    swat_sim_close(&sim);
    puts("PASS surrender pressure: guns aimed, wounds and an empty weapon raise it; an armed ally in sight lowers it");
}

// Command one suspect who is not covered by the officer's muzzle (the order
// lands, but no gun is on them). Returns the decision taken.
static SwatYield commanded_choice(uint32_t seed,float resolve) {
    fixture(seed); sim.actors[1].mind.resolve=resolve;
    face(1,(b3Pos){6,1.6f,-6}); face(0,(b3Pos){6,1.45f,.75f});
    SwatInput in=swat_neutral_input(); in.command=true; step(in);
    return (SwatYield)sim.actors[1].mind.yield;
}

static void decisions(void) {
    int counts[SWAT_YIELDS]={0};
    for(uint32_t seed=1;seed<=40;seed++) {
        SwatYield first=commanded_choice(seed,.2f);swat_sim_close(&sim);
        SwatYield again=commanded_choice(seed,.2f);swat_sim_close(&sim);
        assert(first==again && first!=SWAT_YIELD_NONE);
        counts[first]++;
        SwatYield waver=commanded_choice(seed,.4f);swat_sim_close(&sim);
        counts[waver]++;
    }
    printf("SURRENDER_CHOICES comply%d drop%d feint%d flee%d fight%d\n",counts[SWAT_YIELD_COMPLY],counts[SWAT_YIELD_DROP],
        counts[SWAT_YIELD_FEINT],counts[SWAT_YIELD_FLEE],counts[SWAT_YIELD_FIGHT]);
    for(int c=SWAT_YIELD_COMPLY;c<SWAT_YIELDS;c++) assert(counts[c]>0);
    // Overwhelming pressure (gun aimed, badly hurt) gives no room to feint.
    for(uint32_t seed=1;seed<=20;seed++) {
        fixture(seed); sim.actors[1].mind.resolve=.5f; sim.actors[1].health=45; face(0,chest(1)); idle(2);
        command(); assert(sim.actors[1].mind.yield==SWAT_YIELD_COMPLY && sim.actors[1].gear.surrendered);
        swat_sim_close(&sim);
    }
    puts("PASS surrender decisions: deterministic per seed; comply, drop, feint, flee and fight all occur; aimed and wounded suspects comply genuinely");
}

static uint32_t seed_for(SwatYield wanted,float resolve) {
    for(uint32_t seed=1;seed<200;seed++) {
        SwatYield got=commanded_choice(seed,resolve);swat_sim_close(&sim);
        if(got==wanted) return seed;
    }
    assert(false); return 0;
}

static void comply_and_drop(void) {
    uint32_t seed=seed_for(SWAT_YIELD_COMPLY,.2f);
    commanded_choice(seed,.2f); idle(2);
    assert(sim.actors[1].gear.surrendered && sim.evidence[1].dropped && !sim.actors[1].gear.disarmed);
    face(0,chest(1)); idle(3); assert(roe(1)==SWAT_ROE_SURRENDERED);
    swat_sim_close(&sim);
    seed=seed_for(SWAT_YIELD_DROP,.2f);
    commanded_choice(seed,.2f); b3Pos start=feet(1); idle(2);
    assert(sim.actors[1].gear.disarmed && !sim.actors[1].gear.surrendered && sim.evidence[1].dropped);
    SwatRoeState seen=SWAT_ROE_UNKNOWN; int shots=sim.actors[1].arsenal.shots;
    for(int t=0;t<SWAT_FLEE_TICKS;t++) { face(0,chest(1)); step(swat_neutral_input()); if(roe(1)==SWAT_ROE_FLEEING) seen=SWAT_ROE_FLEEING; }
    assert(seen==SWAT_ROE_FLEEING && b3Distance(feet(1),start)>1 && sim.actors[1].arsenal.shots==shots);
    assert(!swat_roe_lethal_threat(roe(1)));
    // Shooting the unarmed runner is a lethal violation; the next order lands.
    swat_sim_damage_region(&sim,1,0,10,SWAT_LEGS,false); assert(sim.debrief.lethal_violations==1);
    // The officer closes to voice range (fixture move), then gives the order.
    b3Pos near=b3OffsetPos(feet(1),swat_mul(swat_normalize(b3SubPos(feet(0),feet(1))),4));
    b3Body_SetTransform(sim.actors[0].controller.body.body,(b3Pos){near.x,.91f,near.z},b3Quat_identity); idle(2);
    face(0,chest(1)); command(); assert(sim.actors[1].gear.surrendered);
    swat_sim_close(&sim);
    puts("PASS comply/drop: compliance kneels and drops the weapon; a dropped weapon becomes evidence, the suspect runs unarmed, is seen fleeing, cannot fire, and complies with the next order");
}

static void place(int actor,b3Pos at) {
    b3Body_SetTransform(sim.actors[actor].controller.body.body,(b3Pos){at.x,.91f,at.z},b3Quat_identity);
}

// A feigned surrender looks genuine (kneeling, hands shown) and hides the
// weapon. It holds while a gun covers the suspect, draws when unguarded, and
// the drawn weapon is a lethal threat; cuffing a feint finds the weapon.
static void feint(void) {
    uint32_t seed=seed_for(SWAT_YIELD_FEINT,.2f);
    commanded_choice(seed,.2f); SwatActor* s=&sim.actors[1];
    // The kneeling suspect watches the officer (fixture turn of the head).
    face(1,swat_controller_eye(&sim.actors[0].controller));
    for(int t=0;t<400;t++) { face(0,chest(1)); step(swat_neutral_input()); assert(s->gear.surrendered); }
    assert(roe(1)==SWAT_ROE_SURRENDERED && !sim.evidence[1].dropped && s->mind.state==SWAT_SURRENDER);
    // The officer's muzzle drifts off the suspect, who still sees the officer.
    int drew=-1;
    for(int t=0;t<120 && drew<0;t++) { face(0,(b3Pos){6,1.45f,.75f}); step(swat_neutral_input()); if(!s->gear.surrendered) drew=t; }
    assert(drew>=0 && !sim.evidence[1].dropped);
    for(int t=0;t<90 && roe(1)!=SWAT_ROE_HOSTILE_ARMED;t++) { face(0,(b3Pos){6,1.45f,.75f}); step(swat_neutral_input()); }
    assert(roe(1)==SWAT_ROE_HOSTILE_ARMED && sim.debrief.feints==1);
    swat_sim_damage_region(&sim,1,0,10,SWAT_TORSO,false); assert(!sim.debrief.lethal_violations && !sim.debrief.roe_violations);
    swat_sim_close(&sim);
    // Cuffed while feigning under cover: the search finds the weapon.
    commanded_choice(seed,.2f); s=&sim.actors[1];
    place(0,(b3Pos){4.8f,0,0}); idle(2);
    SwatInput cuff=swat_neutral_input(); cuff.interact=true;
    for(int t=0;t<90 && !s->gear.restrained;t++) { face(0,chest(1)); step(cuff); }
    assert(s->gear.restrained && !sim.debrief.feints); idle(2);
    assert(sim.evidence[1].dropped && roe(1)==SWAT_ROE_RESTRAINED);
    swat_sim_close(&sim);
    printf("PASS feint: seed %u kneels with the weapon hidden for 400 covered ticks, draws %d ticks after the muzzle drifts, is seen hostile-armed (feints=1) and lawfully engaged; cuffed under cover it is searched and the weapon found\n",seed,drew);
}

static void flee_and_fight(void) {
    uint32_t seed=seed_for(SWAT_YIELD_FLEE,.4f);
    commanded_choice(seed,.4f); b3Pos start=feet(1); SwatActor* s=&sim.actors[1];
    bool fled=false; int shots=s->arsenal.shots;
    for(int t=0;t<SWAT_FLEE_TICKS;t++) { face(0,chest(1)); step(swat_neutral_input()); fled|=roe(1)==SWAT_ROE_FLEEING; }
    assert(fled && b3Distance(feet(1),start)>1 && !s->gear.surrendered && !s->gear.disarmed && s->arsenal.shots==shots);
    // From cover the suspect re-engages: weapon raised again at the officer.
    for(int t=0;t<SWAT_LOOK_BACK_TICKS+5 && s->mind.yield!=SWAT_YIELD_NONE;t++) { face(0,chest(1)); step(swat_neutral_input()); }
    assert(s->mind.yield==SWAT_YIELD_NONE);
    bool raised=false;
    for(int t=0;t<300 && !raised;t++) { face(0,chest(1)); step(swat_neutral_input()); raised=swat_sim_observed_weapon_threat(&sim,0,1,24); }
    assert(raised && roe(1)==SWAT_ROE_HOSTILE_ARMED);
    swat_sim_close(&sim);
    seed=seed_for(SWAT_YIELD_FIGHT,.4f);
    commanded_choice(seed,.4f); s=&sim.actors[1]; start=feet(1);
    for(int t=0;t<120;t++) { face(0,chest(1)); step(swat_neutral_input()); }
    assert(!s->gear.surrendered && b3Distance(feet(1),start)<1 && roe(1)==SWAT_ROE_HOSTILE_ARMED);
    swat_sim_close(&sim);
    puts("PASS flee/fight: a wavering suspect runs armed (seen fleeing, no shots) then re-engages from cover; a fighter holds ground and raises the weapon");
}

// No order needed: a fresh wound or an empty weapon in a fight forces a choice.
static void spontaneous(void) {
    int yielded=0;
    for(uint32_t seed=1;seed<=12;seed++) {
        fixture(seed); sim.actors[1].mind.resolve=.5f; face(0,chest(1)); idle(40);
        swat_sim_damage_region(&sim,1,0,55,SWAT_TORSO,false); step(swat_neutral_input()); step(swat_neutral_input());
        SwatYield y=sim.actors[1].mind.yield;
        assert(y==SWAT_YIELD_COMPLY || y==SWAT_YIELD_DROP);
        yielded++;
        swat_sim_close(&sim);
    }
    fixture(5); sim.actors[1].mind.resolve=.95f; face(0,(b3Pos){6,1,-1.5f}); idle(40);
    for(int slot=0;slot<2;slot++) { SwatWeapon* w=&sim.actors[1].arsenal.slots[slot]; w->magazine=w->reserve=0; w->chambered=false; w->magazine_count=0; }
    step(swat_neutral_input());
    assert(sim.actors[1].mind.yield_dry && sim.actors[1].mind.yield!=SWAT_YIELD_NONE);
    swat_sim_close(&sim);
    printf("PASS spontaneous: %d/12 hit suspects under a covering gun yield without an order (comply or drop); running dry forces one decision\n",yielded);
}

// Policy-driven suspects keep the role contract's fixed command rule.
static void external_rule(void) {
    for(int k=0;k<2;k++) {
        fixture(7); sim.actors[1].mind.external=true; sim.actors[1].mind.resolve=k ? .6f : .3f;
        driven[1]=true; controlled[1]=swat_neutral_input(); face(0,chest(1)); idle(2); command();
        assert(sim.actors[1].gear.surrendered==!k && sim.actors[1].mind.yield==SWAT_YIELD_NONE && !sim.actors[1].mind.yield_count);
        swat_sim_close(&sim);
    }
    puts("PASS external suspects: policy-driven suspects use the fixed resolve<0.48 command rule and no scripted decision");
}

// A civilian who has the police in sight waits for orders; one facing away
// hears gunfire and panics.
static void civilian_panic_now(void) {
    face(2,b3OffsetPos(swat_controller_eye(&sim.actors[2].controller),swat_v(5,0,0)));
    swat_sound_emit(&sim.sounds,sim.tick,-1,SWAT_SOUND_SHOT,b3OffsetPos(feet(2),swat_v(4,1.6f,0)),1,24);
    for(int t=0;t<30 && sim.actors[2].mind.panic==SWAT_PANIC_NONE;t++) idle(1);
    assert(sim.actors[2].mind.panic!=SWAT_PANIC_NONE);
}
static void civilians(void) {
    // Calm: the first order lands.
    fixture(3); face(0,chest(2)); idle(2); command(); assert(sim.actors[2].gear.surrendered); swat_sim_close(&sim);
    int first_ignored=0,orders[4]={0};
    for(uint32_t seed=1;seed<=20;seed++) {
        int needed[2];
        for(int run=0;run<2;run++) {
            fixture(seed); place(2,(b3Pos){4,0,0}); sim.actors[1].gear.surrendered=true; idle(5);
            civilian_panic_now(); idle(10);
            int n=0;
            while(!sim.actors[2].gear.surrendered) {
                assert(n<3); face(0,chest(2)); command(); n++;
                if(!sim.actors[2].gear.surrendered) {
                    // An order that did not land stops any flight: the civilian
                    // stays put (cowering, or standing for the police in sight).
                    assert(sim.actors[2].mind.panic==SWAT_PANIC_COWER && !sim.actors[2].mind.panic_goal_valid);
                    b3Pos held=feet(2); idle(20); assert(b3Distance(held,feet(2))<.15f);
                }
            }
            needed[run]=n;
            swat_sim_close(&sim);
        }
        assert(needed[0]==needed[1]);
        first_ignored+=needed[0]>1; orders[needed[0]]++;
    }
    printf("CIVILIAN_ORDERS first%d second%d third%d\n",orders[1],orders[2],orders[3]);
    assert(first_ignored>0 && orders[1]>0);
    // A policy-driven civilian is never overridden: the first order lands.
    fixture(4); place(2,(b3Pos){4,0,0}); sim.actors[1].gear.surrendered=true; sim.actors[2].mind.external=true; idle(5);
    civilian_panic_now(); face(0,chest(2)); command(); assert(sim.actors[2].gear.surrendered); swat_sim_close(&sim);
    puts("PASS civilians: calm civilians obey; panicked ones may need repeated orders (cowering, never running, when one does not land) and always obey by the third; deterministic per seed");
}

// A cuffed civilian led to the extraction point counts as rescued.
static void rescue(void) {
    fixture(9); SwatActor* c=&sim.actors[2]; c->gear.surrendered=c->gear.restrained=true; c->mind.escort_owner=0;
    place(0,(b3Pos){-6.5f,0,0}); idle(2);
    for(int t=0;t<1500 && !c->rescued;t++) idle(1);
    assert(c->rescued && sim.debrief.rescued==1 && roe(2)==SWAT_ROE_RESTRAINED);
    swat_sim_close(&sim);
    puts("PASS rescue: a cuffed civilian escorted to extraction counts in the debrief");
}

// A person standing in the aimed line inhibits a squad officer's shot.
static void blocked_line(void) {
    fixture(10); place(2,(b3Pos){3,0,0}); idle(3); face(0,chest(1));
    assert(!swat_sim_firing_line_clear(&sim,0,1,24) && swat_sim_firing_line_clear(&sim,0,2,24));
    place(2,(b3Pos){3,0,3}); idle(3); face(0,chest(1));
    assert(swat_sim_firing_line_clear(&sim,0,1,24));
    swat_sim_close(&sim);
    puts("PASS blocked line: a civilian between officer and suspect blocks the firing line; stepping aside clears it");
}

// Identical seeds and inputs give identical decisions, ROE states and digests.
static void determinism(void) {
    uint32_t digest[2]; SwatRoeState states[2][SWAT_MAX_ACTORS]; int feints[2];
    uint32_t seed=seed_for(SWAT_YIELD_FEINT,.2f);
    for(int run=0;run<2;run++) {
        commanded_choice(seed,.2f);
        for(int t=0;t<300;t++) { face(0,t<150?chest(1):(b3Pos){6,1.45f,.75f}); step(swat_neutral_input()); }
        digest[run]=swat_replay_digest(&sim); feints[run]=sim.debrief.feints;
        for(int i=0;i<SWAT_MAX_ACTORS;i++) states[run][i]=sim.roe[i].state;
        swat_sim_close(&sim);
    }
    assert(digest[0]==digest[1] && feints[0]==feints[1] && !memcmp(states[0],states[1],sizeof(states[0])));
    puts("PASS determinism: repeated seed and inputs reproduce decisions, ROE states, debrief and state digest");
}

// The disarmed flag and new debrief fields replicate.
static void replication(void) {
    uint32_t seed=seed_for(SWAT_YIELD_DROP,.2f);
    commanded_choice(seed,.2f); idle(2); sim.debrief.lethal_violations=2; sim.debrief.feints=1;
    static SwatSnapshot snapshot,decoded; static unsigned char bytes[SWAT_NET_PACKET_MAX];
    swat_capture_snapshot(&sim,1,&snapshot); size_t size=swat_encode_snapshot(bytes,sizeof(bytes),&snapshot);
    assert(size && swat_decode_snapshot(&decoded,bytes,size));
    assert(decoded.actors[1].gear.disarmed && decoded.debrief.lethal_violations==2 && decoded.debrief.feints==1);
    swat_sim_close(&sim);
    puts("PASS replication: disarmed suspects and the lethal/feint debrief survive snapshot encoding");
}

// --- Sight by body part, threat by hands (real geometry) -------------------

// A concrete slab in the test sim: a wall, a beam or full cover.
static void slab(float x,float y0,float y1,float z0,float z1) {
    swat_world_box(&sim.world,(b3Pos){x,(y0+y1)*.5f,(z0+z1)*.5f},swat_v(.05f,(y1-y0)*.5f,(z1-z0)*.5f),SWAT_CONCRETE,0);
}
// Whether officer 0's sight ray to a point at `fraction` of the way from the
// target's feet to their eye reaches the target first (1 head, .6 chest,
// .35 hips: the probes of swat_sim_actor_visible).
static bool part_seen(int target,float fraction) {
    b3Pos eye=swat_controller_eye(&sim.actors[0].controller),top=swat_controller_eye(&sim.actors[target].controller);
    b3Pos point=b3OffsetPos(feet(target),swat_mul(b3SubPos(top,feet(target)),fraction));
    SwatHit hit=swat_world_sight_ray(&sim.world,eye,swat_normalize(b3SubPos(point,eye)),b3Distance(eye,point)+.15f,sim.actors[0].controller.body.body);
    return hit.kind==SWAT_HIT_ACTOR && hit.index==target;
}
static SwatHands hands(int observer,int target) { return swat_sim_hands_seen(&sim,observer,target,24,60*SWAT_RAD); }
// The officer watches the suspect's chest for n ticks (fixture turn of the head).
static void watch(int target,int ticks) { for(int t=0;t<ticks;t++) { face(0,chest(target)); step(swat_neutral_input()); } }
// Suspect 1, held by an outside input, with resolve high enough that a
// command never makes them comply; civilian 2 held still too.
static void held_fixture(uint32_t seed) {
    fixture(seed); driven[1]=driven[2]=true; controlled[1]=controlled[2]=swat_neutral_input();
    sim.actors[1].mind.external=sim.actors[2].mind.external=true; sim.actors[1].mind.resolve=.9f;
}

// 1. A beam hides the suspect's head from the officer; the torso and the
// rifle, raised at a civilian, are in plain view.
static void head_hidden(void) {
    held_fixture(41); place(2,(b3Pos){6,0,-5});
    slab(5.6f,1.45f,2.6f,-.3f,.3f); idle(2);
    face(0,chest(1)); face(1,chest(2)); idle(1);
    assert(!part_seen(1,1) && part_seen(1,.6f) && part_seen(1,.35f));
    assert(swat_sim_actor_visible(&sim,0,1,24,60*SWAT_RAD) && hands(0,1)==SWAT_HANDS_WEAPON);
    watch(1,3); assert(roe(1)==SWAT_ROE_UNKNOWN && sim.roe[1].armed_seen);
    controlled[1].aim=true;
    for(int t=0;t<40 && roe(1)!=SWAT_ROE_HOSTILE_ARMED;t++) { face(1,chest(2)); watch(1,1); }
    assert(sim.actors[1].controller.ads>=.5f && swat_sim_observed_weapon_threat(&sim,0,1,24) && roe(1)==SWAT_ROE_HOSTILE_ARMED);
    swat_sim_damage_region(&sim,1,0,10,SWAT_TORSO,false);
    assert(!sim.debrief.lethal_violations && !sim.debrief.roe_violations);
    swat_sim_close(&sim);
    puts("PASS roe head hidden: a beam hides the head, the torso and the rifle raised at a civilian are seen; hands weapon, hostile-armed, a lethal hit is lawful");
}

// 2-3. A low wall leaves only the head of an armed suspect in view: seen,
// hands hidden, unidentified through a challenge and its expiry. Stepping
// out shows the weapon; stepping back keeps the assessment.
static void head_only(void) {
    held_fixture(42); slab(5,0,1.5f,-1,1); idle(2);
    face(0,chest(1)); idle(1);
    assert(part_seen(1,1) && !part_seen(1,.6f) && !part_seen(1,.35f));
    assert(swat_sim_actor_visible(&sim,0,1,24,60*SWAT_RAD) && hands(0,1)==SWAT_HANDS_HIDDEN);
    watch(1,3); assert(sim.roe[1].seen_tick && roe(1)==SWAT_ROE_UNKNOWN && !sim.roe[1].armed_seen);
    command(); assert(!sim.actors[1].gear.surrendered && sim.roe[1].challenged_tick);
    watch(1,SWAT_ROE_CHALLENGE_TICKS+10);
    assert(roe(1)==SWAT_ROE_UNKNOWN && !sim.roe[1].armed_seen && hands(0,1)==SWAT_HANDS_HIDDEN);
    assert(!swat_roe_lethal_threat(roe(1)));
    swat_sim_damage_region(&sim,1,0,10,SWAT_ARMS,false); assert(sim.debrief.lethal_violations==1);
    // Steps out beside the wall: the weapon in hand is seen, and the expired
    // challenge makes the armed suspect a threat at once.
    place(1,(b3Pos){6,0,2}); watch(1,2);
    assert(hands(0,1)==SWAT_HANDS_WEAPON && sim.roe[1].armed_seen && roe(1)==SWAT_ROE_HOSTILE_ARMED);
    place(1,(b3Pos){6,0,0}); watch(1,3);
    assert(hands(0,1)==SWAT_HANDS_HIDDEN && sim.roe[1].armed_seen && roe(1)==SWAT_ROE_HOSTILE_ARMED);
    swat_sim_close(&sim);
    puts("PASS roe head only: over a low wall an armed suspect is seen with hands hidden, stays unknown through a challenge and its expiry (a lethal hit is a violation); stepping out shows the weapon -> hostile-armed, kept when only the head shows again");
}

// 4. An unarmed civilian with only the head over the wall is unidentified;
// in the open, empty hands make them compliant.
static void civilian_hands(void) {
    held_fixture(43); slab(5,0,1.5f,-1,1); place(2,(b3Pos){6,0,0}); place(1,(b3Pos){6,0,-6}); idle(2);
    watch(2,3);
    assert(part_seen(2,1) && !part_seen(2,.6f));
    assert(swat_sim_actor_visible(&sim,0,2,24,60*SWAT_RAD) && hands(0,2)==SWAT_HANDS_HIDDEN && roe(2)==SWAT_ROE_UNKNOWN);
    place(2,(b3Pos){6,0,2}); watch(2,3);
    assert(hands(0,2)==SWAT_HANDS_EMPTY && roe(2)==SWAT_ROE_COMPLIANT);
    swat_sim_close(&sim);
    puts("PASS roe civilian hands: head only over a wall stays unknown; empty hands in the open are compliant");
}

// 5-6. Full cover hides every probe and freezes the record. A suspect seen
// armed and hostile drops the weapon out of sight and comes back with empty
// hands in view: the table allows hostile-armed -> compliant, and the
// weapon is no longer on the record.
static void dropped_out_of_sight(void) {
    held_fixture(44); slab(5,0,2.6f,-1,1); place(1,(b3Pos){6,0,2}); idle(2);
    controlled[1].aim=true; face(1,swat_controller_eye(&sim.actors[0].controller));
    for(int t=0;t<60 && roe(1)!=SWAT_ROE_HOSTILE_ARMED;t++) watch(1,1);
    assert(roe(1)==SWAT_ROE_HOSTILE_ARMED && sim.roe[1].armed_seen);
    controlled[1].aim=false; place(1,(b3Pos){6,0,0}); watch(1,3);
    assert(!part_seen(1,1) && !part_seen(1,.6f) && !part_seen(1,.35f));
    assert(!swat_sim_actor_visible(&sim,0,1,24,60*SWAT_RAD) && hands(0,1)==SWAT_HANDS_HIDDEN);
    SwatRoePerson record=sim.roe[1];
    sim.actors[1].gear.disarmed=true; watch(1,30);
    assert(!memcmp(&record,&sim.roe[1],sizeof(record)));
    assert(swat_roe_transition_legal(SWAT_ROE_HOSTILE_ARMED,SWAT_ROE_COMPLIANT));
    place(1,(b3Pos){6,0,2}); watch(1,3);
    assert(hands(0,1)==SWAT_HANDS_EMPTY && !sim.roe[1].armed_seen && roe(1)==SWAT_ROE_COMPLIANT && !sim.roe_refused);
    swat_sim_close(&sim);
    puts("PASS roe dropped out of sight: behind full cover no probe sees the suspect and the record does not move; back in view with empty hands, hostile-armed -> compliant (allowed by the table), weapon off the record");
}

// The suspect fires into the sky in the officer's sight and turns back to
// face the officer: hostile-firing, weapon on the record.
static void seen_firing(void) {
    face(1,b3OffsetPos(swat_controller_eye(&sim.actors[1].controller),swat_v(1,3,0)));
    controlled[1].fire=true; watch(1,1); controlled[1].fire=false;
    for(int t=0;t<20 && sim.actors[1].last_shot_tick<0;t++) watch(1,1);
    assert(sim.actors[1].last_shot_tick>=0);
    face(1,swat_controller_eye(&sim.actors[0].controller)); watch(1,1);
    assert(roe(1)==SWAT_ROE_HOSTILE_FIRING && sim.roe[1].armed_seen);
}

// 7. A shooter who drops the weapon in plain view, inside the firing window,
// has stopped firing: empty hands make them compliant at once, and a lethal
// hit is a violation.
static void dropped_after_firing(void) {
    held_fixture(45); idle(2); seen_firing();
    sim.actors[1].gear.disarmed=true; watch(1,1);
    assert(sim.tick-sim.actors[1].last_shot_tick<SWAT_ROE_FIRING_TICKS && hands(0,1)==SWAT_HANDS_EMPTY);
    assert(roe(1)==SWAT_ROE_COMPLIANT && !sim.roe[1].armed_seen && !sim.roe_refused);
    swat_sim_damage_region(&sim,1,0,10,SWAT_TORSO,false);
    assert(sim.debrief.lethal_violations==1);
    swat_sim_close(&sim);
    puts("PASS roe dropped after firing: a shooter seen dropping the weapon inside the firing window is compliant at once, weapon off the record; a lethal hit is a violation");
}

// 8. The same shooter behind a chin-high wall with a slit at the head: the
// head is seen, the raised empty hands are not. The shot stays on the record
// (hostile-firing, then hostile-armed) and a lethal hit is lawful.
static void dropped_hidden_after_firing(void) {
    held_fixture(46); slab(5,0,1.5f,-1,1); slab(5,1.5f,2.6f,-1,-.15f); slab(5,1.5f,2.6f,.15f,1); idle(2);
    seen_firing();
    assert(part_seen(1,1) && !part_seen(1,.6f) && !part_seen(1,.35f) && hands(0,1)==SWAT_HANDS_HIDDEN);
    sim.actors[1].gear.disarmed=true; watch(1,1);
    assert(sim.tick-sim.actors[1].last_shot_tick<SWAT_ROE_FIRING_TICKS && hands(0,1)==SWAT_HANDS_HIDDEN);
    assert(roe(1)==SWAT_ROE_HOSTILE_FIRING && sim.roe[1].armed_seen);
    // The ROE step runs after the tick advances, so it judges age tick+1.
    while(sim.tick+1-sim.actors[1].last_shot_tick<SWAT_ROE_FIRING_TICKS) { watch(1,1); assert(roe(1)==SWAT_ROE_HOSTILE_FIRING); }
    watch(1,1);
    assert(swat_sim_actor_visible(&sim,0,1,24,60*SWAT_RAD) && hands(0,1)==SWAT_HANDS_HIDDEN);
    assert(roe(1)==SWAT_ROE_HOSTILE_ARMED && sim.roe[1].armed_seen);
    swat_sim_damage_region(&sim,1,0,10,SWAT_HEAD,false);
    assert(!sim.debrief.lethal_violations && !sim.debrief.roe_violations);
    swat_sim_close(&sim);
    puts("PASS roe dropped hidden after firing: behind a slit wall only the head shows; the weapon dropped out of sight of the hands keeps hostile-firing for the window, then hostile-armed, and a lethal hit is lawful");
}

int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    transition_table();
    perception();
    raised_weapon();
    lethal_force();
    pressure();
    decisions();
    comply_and_drop();
    feint();
    flee_and_fight();
    spontaneous();
    external_rule();
    civilians();
    rescue();
    blocked_line();
    determinism();
    replication();
    head_hidden();
    head_only();
    civilian_hands();
    dropped_out_of_sight();
    dropped_after_firing();
    dropped_hidden_after_firing();
    return 0;
}
