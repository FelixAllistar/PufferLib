// Included after abyss.h by the native mechanics test.
static void test_npc_repairs(void) {
    Env e={0};
    float expected=0;
    int shield_repairers=0,armor_repairers=0;
    for(int i=0;i<GENERATED_NPC_COUNT;i++){
        e.entity_count=0;
        ab_add_generated_hostile(&e,(GeneratedSpawn){.npc=i});
        AbyssEntity* n=&e.entities[0];
        const GeneratedNpcDef* def=&GENERATED_NPCS[i];
        assert(n->local_repair==def->local_repair);
        assert(n->remote_repair==def->remote_repair);
        assert(n->remote_repair_layer==def->remote_repair_layer);
        n->remote_repair=0;
        n->shield=0;n->armor=0;
        if(n->local_repair_layer>=0){
            expected+=fminf(2*n->local_repair,ab_npc_layer_max(n,n->local_repair_layer));
            shield_repairers+=n->local_repair_layer==LAYER_SHIELD;
            armor_repairers+=n->local_repair_layer==LAYER_ARMOR;
        }
        float before=e.npc_local_repaired;
        ab_step_npc_repairs(&e,2);
        assert(e.npc_local_repaired>=before);
        if(n->local_repair_layer==LAYER_SHIELD){assert(n->shield>0);assert(n->armor==0);}
        else if(n->local_repair_layer==LAYER_ARMOR){assert(n->armor>0);assert(n->shield==0);}
    }
    assert(shield_repairers>0&&armor_repairers>0);
    assert(fabsf(e.npc_local_repaired-expected)<.001f);
    for(int i=0;i<e.entity_count;i++){
        AbyssEntity* n=&e.entities[i];
        if(n->local_repair_layer==LAYER_SHIELD){assert(n->shield>0);assert(n->armor==0);}
        else if(n->local_repair_layer==LAYER_ARMOR){assert(n->armor>0);assert(n->shield==0);}
        else {assert(n->armor==0);assert(n->shield==0);}
    }
    // Known exported rates: Hunter shields 12.5/s, Pacifier armor 26/s.
    assert(GENERATED_NPCS[3].local_repair==12.5f);
    assert(GENERATED_NPCS[3].local_repair_layer==LAYER_SHIELD);
    assert(GENERATED_NPCS[0].local_repair==26);
    assert(GENERATED_NPCS[0].local_repair_layer==LAYER_ARMOR);
    AbyssEntity n={.kind=ENTITY_HOSTILE,.alive=1,.hull=10,
        .shield=95,.shield_max=100,.armor=0,.armor_max=50};
    assert(ab_repair_npc(&n,LAYER_SHIELD,20)==5);
    assert(n.shield==100&&n.armor==0);
    n.alive=0;assert(ab_repair_npc(&n,LAYER_ARMOR,20)==0);
    n.alive=1;n.hull=0;assert(ab_repair_npc(&n,LAYER_ARMOR,20)==0);
    n.hull=10;n.kind=ENTITY_CACHE;assert(ab_repair_npc(&n,LAYER_ARMOR,20)==0);

    memset(&e,0,sizeof(e));e.entity_count=4;e.capacitor=123;
    for(int i=0;i<4;i++)e.entities[i]=(AbyssEntity){.kind=ENTITY_HOSTILE,
        .alive=1,.hull=100,.shield=100,.shield_max=100,.armor=100,.armor_max=100};
    AbyssEntity* healer=&e.entities[0];
    healer->remote_repair=20;healer->remote_repair_layer=LAYER_ARMOR;
    healer->remote_repair_optimal=1000;healer->remote_repair_falloff=1000;
    healer->armor=0; // remote repair cannot heal itself
    e.entities[1].armor=0;e.entities[1].pos.x=2000;
    e.entities[2].armor=0;e.entities[2].alive=0;
    e.entities[3].armor=0;e.entities[3].kind=ENTITY_CACHE;
    ab_step_npc_repairs(&e,1);
    assert(healer->armor==0&&e.entities[1].armor==10); // half at optimal+falloff
    assert(e.entities[1].shield==100&&e.entities[2].armor==0&&e.entities[3].armor==0);
    assert(e.npc_remote_repaired==10&&e.capacitor==123);
    healer->remote_repair_falloff=0;ab_step_npc_repairs(&e,1);
    assert(e.entities[1].armor==10); // outside hard range
    e.entities[1].pos.x=1000;ab_step_npc_repairs(&e,1);
    assert(e.entities[1].armor==30);
    healer->alive=0;ab_step_npc_repairs(&e,1);assert(e.entities[1].armor==30);
}
