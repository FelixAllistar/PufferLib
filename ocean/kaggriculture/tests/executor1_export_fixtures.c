/* Disposable states for the public/native export parity gate. Not a trainer. */
#include "kag_experiment_view.c"

void kg_export_fixture(KGState* g, int fixture) {
    KGConfig cfg; kg_config_default(&cfg); cfg.weed_spawn_chance=0;
    kg_init(g,&cfg);
    KGPlayer* p=&g->players[0]; p->money=100000000;
    if(fixture==0) {
        p->seeds[KG_MELON]=1;
        KGUnitAction plant={KG_OP_PLANT,KG_MELON,1};
        kg_apply_unit_action(g,p,0,&plant);
        g->day=1;g->step=24;g->hour=0;
        p->tiles[44].watered_today=1;
        p->tiles[44].fertilized_until_day=0;
        kg_inventory_add(&p->units[0],KG_ITEM_FERTILIZER,1);
        p->shed[KG_ITEM_FERTILIZER]=5;
    } else if(fixture==1) {
        for(int u=0;u<20;u++)kg_do_hire(g,p);
        for(int u=0;u<p->unit_count;u++)kg_set_unit_position(p,u,u%5,u/5);
        p->seeds[KG_MELON]=18;
    } else {
        for(int y=0;y<5;y++)for(int x=0;x<5;x++)
            kg_set_player_tile(p,kg_tile_index(x,y),KG_TILE_PASTURE);
        kg_set_player_tile(p,0,KG_TILE_COOP);
        kg_inventory_add(&p->units[0],KG_ITEM_COW,1);
        p->shed[KG_ITEM_SHEEP]=2;
        for(int i=0;i<KG_NUM_PRODUCTS;i++)p->shed[i]=3;
        /* Sales sort ties must follow native selection sort, not stable sort. */
        for(int i=0;i<KG_NUM_PRODUCTS;i++)g->market.prices[i]=100+(i%3)*10;
    }
}
