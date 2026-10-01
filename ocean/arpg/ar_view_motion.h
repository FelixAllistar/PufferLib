#pragma once
// Viewer snapshots only. Simulation state and its fixed tick never interpolate.
static inline void ar_pose_capture(ARPoses* poses,ARPG* e) {
    poses->tick=e->tick;
    poses->origin_x=e->campaign ? ((ARWorld*)e->campaign)->origin_x : 0;
    poses->origin_y=e->campaign ? ((ARWorld*)e->campaign)->origin_y : 0;
    poses->keeper=(ARPose){{e->px,e->py},b3StoreBodyId(e->player_body),!e->keeper_dormant,0};
    for(int p=0;p<AR_MAX_PETS;p++)
        poses->pets[p]=(ARPose){{e->pets.x[p],e->pets.y[p]},b3StoreBodyId(e->pet_body[p]),
            e->pets.active[p] && !e->pets.dormant[p],e->pets.kind[p]};
    for(int i=0;i<AR_MAX_ENEMIES;i++)
        poses->enemies[i]=(ARPose){{e->enemies.x[i],e->enemies.y[i]},b3StoreBodyId(e->enemy_body[i]),
            e->enemies.active[i],e->enemies.type[i]};
}
static inline void ar_pose_reset(ARClient* c,ARPG* e) {
    ar_pose_capture(&c->pose_current,e);c->pose_previous=c->pose_current;
    c->pose_ready=1;c->view_ready=0;c->render_alpha=1;
}
static inline void ar_pose_before_step(ARClient* c,ARPG* e) {
    // Also capture changes made by input (summons, possession, teleports).
    ar_pose_capture(&c->pose_previous,e);
}
static inline void ar_pose_after_step(ARClient* c,ARPG* e) {
    ar_pose_capture(&c->pose_current,e);c->pose_ready=1;
}
static inline int ar_pose_equal(ARPose a,ARPose b) {
    return a.at.x==b.at.x && a.at.y==b.at.y && a.body==b.body && a.active==b.active && a.kind==b.kind;
}
static inline Vector2 ar_pose_blend(ARPose old,ARPose now,float dx,float dy,float alpha) {
    old.at.x-=dx;old.at.y-=dy;
    // Body identity catches slot reuse and physics reloads; distance catches
    // teleports within the same body. Neither should sweep across the screen.
    if(!old.active || !now.active || old.body!=now.body || old.kind!=now.kind ||
            ar_geometry_dist2(old.at.x,old.at.y,now.at.x,now.at.y)>4)return now.at;
    return (Vector2){old.at.x+(now.at.x-old.at.x)*alpha,old.at.y+(now.at.y-old.at.y)*alpha};
}
static inline void ar_view_prepare(ARClient* c,ARPG* e) {
    ARPoses live;ar_pose_capture(&live,e);
    int reset=!c->pose_ready || c->pose_current.tick!=live.tick ||
        c->pose_current.origin_x!=live.origin_x || c->pose_current.origin_y!=live.origin_y ||
        !ar_pose_equal(c->pose_current.keeper,live.keeper);
    for(int p=0;p<AR_MAX_PETS;p++)reset|=!ar_pose_equal(c->pose_current.pets[p],live.pets[p]);
    for(int i=0;i<AR_MAX_ENEMIES;i++)reset|=!ar_pose_equal(c->pose_current.enemies[i],live.enemies[i]);
    if(reset)ar_pose_reset(c,e);
    ARPoses* old=&c->pose_previous;ARPoses* now=&c->pose_current;
    float dx=(now->origin_x-old->origin_x)*ar_world_cell(e);
    float dy=(now->origin_y-old->origin_y)*ar_world_cell(e);
    float alpha=c->paused ? 1 : ar_clampf(c->render_alpha,0,1);
    c->view_keeper=ar_pose_blend(old->keeper,now->keeper,dx,dy,alpha);
    for(int p=0;p<AR_MAX_PETS;p++)c->view_pets[p]=ar_pose_blend(old->pets[p],now->pets[p],dx,dy,alpha);
    for(int i=0;i<AR_MAX_ENEMIES;i++)c->view_enemies[i]=ar_pose_blend(old->enemies[i],now->enemies[i],dx,dy,alpha);
    c->view_ready=1;
}
static inline Vector2 ar_view_keeper(ARClient* c,ARPG* e) {
    return c->view_ready && c->pose_current.keeper.at.x==e->px && c->pose_current.keeper.at.y==e->py ?
        c->view_keeper : (Vector2){e->px,e->py};
}
static inline Vector2 ar_view_pet(ARClient* c,ARPG* e,int p) {
    return c->view_ready && c->pose_current.pets[p].at.x==e->pets.x[p] && c->pose_current.pets[p].at.y==e->pets.y[p] ?
        c->view_pets[p] : (Vector2){e->pets.x[p],e->pets.y[p]};
}
static inline Vector2 ar_view_enemy(ARClient* c,ARPG* e,int i) {
    return c->view_ready && c->pose_current.enemies[i].at.x==e->enemies.x[i] && c->pose_current.enemies[i].at.y==e->enemies.y[i] ?
        c->view_enemies[i] : (Vector2){e->enemies.x[i],e->enemies.y[i]};
}
