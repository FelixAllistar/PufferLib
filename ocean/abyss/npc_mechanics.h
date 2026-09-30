// Included by abyss.h after the common spawn helper. Shared mechanics for all supported tiers.
static float ab_effect_range_factor(float distance, float optimal, float falloff) {
    if(distance<=optimal)return 1;
    if(falloff<=0)return 0;
    float x=(distance-optimal)/falloff;return powf(.5f,x*x);
}
static float ab_stack_effects(float* values,int count) {
    for(int i=1;i<count;i++){float v=values[i];int j=i;
        while(j>0&&fabsf(values[j-1])<fabsf(v)){values[j]=values[j-1];j--;}values[j]=v;}
    float product=1;
    for(int i=0;i<count;i++)product*=fmaxf(.01f,1+values[i]*expf(-.140274f*i*i));
    return product;
}
// Observation estimator uses only NPC identity and observed distance. Actual
// cycle success/strength is deliberately hidden (also unavailable to the bot).
static void ab_estimate_ewar(Env* e,float* out) {
    float vals[7][ABYSS_MAX_ENTITIES*4];int counts[7]={0};float scram=0,neut=0;int incomplete=0;
    for(int i=0;i<e->entity_count;i++){
        AbyssEntity*n=&e->entities[i];if(!n->alive||n->kind!=ENTITY_HOSTILE)continue;
        if(i==e->unknown_npc_index){incomplete=1;continue;}
        const NpcMechanics*d=&AB_NPC_MECHANICS[n->type_index];
        float distance=ab_len(ab_observed_relative(e,n->pos,n->vel));
        for(int k=0;k<d->effect_count;k++){
            const NpcEffect*x=&d->effects[k];float factor=ab_effect_range_factor(distance,x->optimal,x->falloff);
            for(int c=0;c<7;c++)if(x->values[c]!=0)vals[c][counts[c]++]=x->values[c]*factor;
            scram+=x->values[7]*factor;neut+=x->values[8]*factor/fmaxf(1,x->cycle);
        }
    }
    for(int c=0;c<7;c++)out[c]=ab_stack_effects(vals[c],counts[c]);
    out[7]=fminf(1,scram);out[8]=neut*(1-e->cap_neut_resistance)/100;
    out[9]=incomplete?-1:0; // -1 incomplete, 0 estimated, never verified
    if(incomplete){for(int c=0;c<7;c++)out[c]=c==4?fmaxf(1.5f,out[c]):fminf(.7f,out[c]);out[7]=1;out[8]=fmaxf(.2f,out[8]);}
}
static void ab_step_ewar(Env*e) {
    float vals[7][ABYSS_MAX_ENTITIES*4];int counts[7]={0};float scram=0;
    for(int i=0;i<e->entity_count;i++){
        AbyssEntity*n=&e->entities[i];if(!n->alive||n->kind!=ENTITY_HOSTILE)continue;
        const NpcMechanics*d=&AB_NPC_MECHANICS[n->type_index];float distance=ab_len(ab_sub(n->pos,e->ship_pos));
        for(int k=0;k<d->effect_count;k++){
            const NpcEffect*x=&d->effects[k];n->ewar_cd[k]-=1;
            if(n->ewar_cd[k]<=0){
                n->ewar_cd[k]+=fmaxf(1,x->cycle);
                float factor=ab_effect_range_factor(distance,x->optimal,x->falloff);
                n->ewar_active[k]=(ab_rand(e)<factor*e->ewar_activation_probability)?n->ewar_strength:0;
                // Neuts have graded falloff, unlike chance-based TD/damp cycles.
                float drain=x->values[8]*factor*n->ewar_strength*(1-e->cap_neut_resistance);
                float before=e->capacitor;e->capacitor=fmaxf(0,before-drain);e->neut_cap_drained+=before-e->capacitor;
            }
            for(int c=0;c<7;c++)if(x->values[c]!=0)vals[c][counts[c]++]=x->values[c]*n->ewar_active[k];
            scram+=x->values[7]*n->ewar_active[k];
        }
    }
    for(int c=0;c<7;c++)e->ewar[c]=ab_stack_effects(vals[c],counts[c]);
    e->ewar[7]=scram;
    if(e->prop_is_mwd&&scram>0){e->prop_on=0;e->prop_cooldown=0;}
    // Damps can break established locks and slow acquisition, not merely mask
    // new lock requests. Other transactions observe these authoritative states.
    for(int i=0;i<e->entity_count;i++){
        AbyssEntity*n=&e->entities[i];
        if(ab_len(ab_sub(n->pos,e->ship_pos))>e->lock_range*e->ewar[5]){
            n->locked=n->locking=n->focused=0;n->lock_progress=0;
            if(e->focus_index==i)e->focus_index=-1;
        }
    }
}
static void ab_spawn_calm(Env* e,const GeneratedRoom* layout) {
    int a=e->encounter_archetype;
    if(a<0){int total=0;for(unsigned i=0;i<CALM_ARCHETYPE_COUNT;i++)total+=CALM_ARCHETYPES[i].samples;
        int pick=ab_rand_u32(e)%total;a=0;while(pick>=CALM_ARCHETYPES[a].samples)pick-=CALM_ARCHETYPES[a++].samples;}
    e->active_archetype=a;const CalmArchetype*s=&CALM_ARCHETYPES[a];
    int counts[20]={0},total=0,valid=0;
    // Rejection samples marginals conditioned on observed total-count bounds.
    // This is explicitly synthetic: source has no joint composition records.
    for(int attempt=0;attempt<4096&&!valid;attempt++){
        total=0;
        for(int j=0;j<s->count;j++){
            const CalmSpawnChoice*c=&s->choices[j];counts[j]=0;
            if(ab_rand(e)<c->chance){
                float pick=ab_rand(e);counts[j]=c->max;
                for(int k=c->min;k<=c->max;k++){pick-=c->weights[k];if(pick<=0){counts[j]=k;break;}}
            }
            total+=counts[j];
        }
        valid=total>=s->min&&total<=s->max;
    }
    // Valid data should never exhaust retries; fail loudly instead of silently
    // manufacturing a composition outside the source constraints.
    if(!valid){fprintf(stderr,"T1 sampler failed for %s\n",s->name);abort();}
    int parents_start=e->entity_count;
    for(int j=0;j<s->count;j++)for(int c=0;c<counts[j];c++){
        GeneratedSpawn spawn={0};spawn.npc=s->choices[j].npc;
        // Recorded T0 anchor with a synthetic spread. No T1 layout captures yet.
        int anchor=ab_rand_u32(e)%layout->hostile_count;
        for(int axis=0;axis<3;axis++)spawn.position[axis]=layout->hostiles[anchor].position[axis]+(ab_rand(e)-.5f)*8000;
        ab_add_generated_hostile(e,spawn);
    }
    int parents_end=e->entity_count;
    e->unknown_npc_index=ab_rand(e)<e->ewar_sensor_dropout_probability?
        parents_start+(int)(ab_rand_u32(e)%(parents_end-parents_start)):-1;
    for(int parent=parents_start;parent<parents_end;parent++){
        AbyssEntity*n=&e->entities[parent];const NpcMechanics*d=&AB_NPC_MECHANICS[n->type_index];
        // Reserve all auxiliary slots now: live/sim slots never move on relaunch.
        for(int j=0;j<d->drone_total;j++){
            if(e->entity_count>=ABYSS_MAX_ENTITIES){fprintf(stderr,"T1 entity capacity exceeded\n");abort();}
            GeneratedSpawn spawn={AB_SWARMER_INDEX,{n->pos.x,n->pos.y,n->pos.z}};
            ab_add_generated_hostile(e,spawn);AbyssEntity*dr=&e->entities[e->entity_count-1];
            dr->parent_index=parent;dr->gate_required=0;dr->suppressor_vulnerable=1;
            dr->alive=0;dr->drone_pending=1;
        }
    }
}
static void ab_step_npc_drones(Env*e){
    for(int i=0;i<e->entity_count;i++){
        AbyssEntity*n=&e->entities[i];if(n->kind!=ENTITY_HOSTILE||n->parent_index>=0)continue;
        const NpcMechanics*d=&AB_NPC_MECHANICS[n->type_index];if(!d->drone_total)continue;
        int active=0;
        for(int j=0;j<e->entity_count;j++){AbyssEntity*dr=&e->entities[j];if(dr->parent_index!=i)continue;
            if(!n->alive){dr->alive=0;dr->drone_pending=0;}else active+=dr->alive;}
        if(!n->alive)continue;
        n->drone_cd=fmaxf(0,n->drone_cd-1);
        if(n->drone_cd>0)continue;
        for(int j=0;j<e->entity_count&&active<d->drone_active;j++){
            AbyssEntity*dr=&e->entities[j];if(dr->parent_index!=i||!dr->drone_pending)continue;
            dr->alive=1;dr->drone_pending=0;dr->pos=n->pos;active++;
        }
        n->drone_cd=5; // Unverified launch/replacement delay; explicit approximation.
    }
}
static float ab_npc_spool(AbyssEntity*n,float distance){
    const NpcMechanics*d=&AB_NPC_MECHANICS[n->type_index];
    if(d->spool_step<=0)return 1;
    // Single-player environment: the player is the sole NPC combat target.
    // Breaking disintegrator range stops the beam and resets buildup.
    if(distance>n->optimal){n->spool=1;n->spool_cd=d->cycle;return 0;}
    float multiplier=n->spool;n->spool_cd-=1;
    if(n->spool_cd<=0){n->spool_cd+=fmaxf(1,d->cycle);n->spool=fminf(d->spool_max,n->spool+d->spool_step);}
    return multiplier;
}

static void ab_retire_orphan_drones(Env*e){
    for(int i=0;i<e->entity_count;i++){
        AbyssEntity*n=&e->entities[i];
        if(n->kind==ENTITY_HOSTILE&&n->parent_index>=0&&!e->entities[n->parent_index].alive){n->alive=0;n->drone_pending=0;}
    }
}
