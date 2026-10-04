#include "render.h"
#include "pose.h"
#include "rlgl.h"
#include <string.h>

static Vector3 swat_vector(b3Vec3 v) { return (Vector3){v.x,v.y,v.z}; }
static Vector3 swat_position(b3Pos p) { return (Vector3){(float)p.x,(float)p.y,(float)p.z}; }
static const Color swat_gold = {226,181,82,255};
static const Color swat_ink = {12,19,26,235};
static const Color swat_paper = {224,231,229,255};

int swat_hud_measure(const SwatView* view,const char* text,int size) {
    return (int)MeasureTextEx(view->hud_font,text,(float)size,.5f).x;
}

void swat_hud_text(const SwatView* view,const char* text,int x,int y,int size,Color color) {
    // A fine shadow keeps text readable against both bright walls and darkness
    // without covering the scene with a panel.
    DrawTextEx(view->hud_font,text,(Vector2){x+1,y+1},size,.5f,(Color){0,0,0,220});
    DrawTextEx(view->hud_font,text,(Vector2){x,y},size,.5f,color);
}

static void swat_hud_center(const SwatView* view,const char* text,int x,int y,int size,Color color) {
    swat_hud_text(view,text,x-swat_hud_measure(view,text,size)/2,y,size,color);
}

static void swat_draw_context(const SwatView* view,const SwatSim* sim,SwatContext context,int width,int height) {
    if(context.action==SWAT_CONTEXT_NONE) return;
    const SwatActor* actor=&sim->actors[view->actor];
    const char* action="",*detail=NULL;
    switch(context.action) {
        case SWAT_CONTEXT_OPEN: case SWAT_CONTEXT_CLOSE:
            action=context.ready ? (context.action==SWAT_CONTEXT_OPEN ? "[F] Open door" : "[F] Close door") : "Move closer to use door"; break;
        case SWAT_CONTEXT_LOCKED:
            action=context.ready ? "Hold [RMB] Pick lock" : "Locked / move closer";
            if(context.ready && swat_kit(actor->gear.kit)->optiwand) detail="[G] Mirror under door";
            break;
        case SWAT_CONTEXT_WEDGED: action="Door wedged / [Alt+9] Remove"; break;
        case SWAT_CONTEXT_TRAP: action="Trap found / hold [8] Disarm"; break;
        case SWAT_CONTEXT_CHARGE:
            action=sim->world.objects[context.hit.index].breach_owner==view->actor ? "[K] Detonate charge" : "Teammate's charge"; break;
        case SWAT_CONTEXT_CUFF:
            action=context.ready ? "Hold [RMB] Handcuff" : "Move closer to handcuff"; break;
        case SWAT_CONTEXT_COMPLY: action="[F] Request compliance"; break;
        case SWAT_CONTEXT_SECURED:
            if(sim->config.tactical_rules && sim->actors[context.hit.index].role==SWAT_CIVILIAN) { action="[F] Escort / hold position"; break; }
            return;
        case SWAT_CONTEXT_DEVICE: action=context.ready ? "[F] Recover device" : "Teammate device"; break;
        case SWAT_CONTEXT_EVIDENCE: action="[F] Collect weapon evidence"; break;
        default: return;
    }
    const SwatEquipment* gear=&actor->gear;
    float progress=gear->door_ticks ? gear->door_ticks/(gear->door_mode==SWAT_LOCKPICK ? 180.0f : gear->door_mode==SWAT_DISARM ? 120.0f : gear->door_mode==SWAT_PLACE_CHARGE ? 90.0f : 45.0f) : gear->cuff_ticks/72.0f;
    if(gear->door_ticks) {
        const char* tools[]={"","Picking lock...","Mounting charge...","","Placing wedge...","Removing wedge...","Disarming trap..."};
        action=tools[gear->door_mode];
    }
    if(gear->cuff_ticks) action="Handcuffing...";
    int cx=width/2,y=height/2+48;
    swat_hud_center(view,action,cx,y,18,swat_paper);
    if(progress>0) {
        DrawRectangle(cx-52,y+27,104,2,(Color){0,0,0,180});
        DrawRectangle(cx-52,y+27,(int)(104*swat_clamp(progress,0,1)),2,swat_gold);
    } else if(detail) swat_hud_center(view,detail,cx,y+23,14,(Color){190,198,199,255});
}

SwatCameraLayout swat_camera_layout(int width,int height,float expansion,bool has_feed) {
    float t=swat_clamp(expansion,0,1); t=t*t*(3-2*t);
    float small=fminf(280,width-48),large=fminf(840,fminf(width-96,(height-160)*16.0f/9));
    float w=small+(large-small)*t,h=has_feed ? w*9.0f/16 : 0;
    float x=(width-small-24)*(1-t)+(width-w)*.5f*t;
    float y=48*(1-t)+(height-h)*.5f*t;
    SwatCameraLayout layout={0};
    layout.panel=layout.feed=(Rectangle){x,y,w,h};
    layout.close=(Rectangle){x+w-28,y-24,28,24};
    float buttons=y+h+4;
    for(int i=0;i<SWAT_SNIPERS;i++) layout.unit[i]=(Rectangle){x+i*30,buttons,26,26};
    layout.previous=(Rectangle){x+62,buttons,26,26};
    layout.next=(Rectangle){x+92,buttons,26,26};
    layout.takeover=(Rectangle){x+w-140,buttons,140,26};
    return layout;
}

void swat_view_init(SwatView* view, bool hidden) {
    if (view->initialized) return;
    SetConfigFlags(FLAG_MSAA_4X_HINT | (hidden ? FLAG_WINDOW_HIDDEN : 0));
    view->width = 1440; view->height = 810;
    InitWindow(view->width,view->height,"SWAT: Gold Element");
    if (!IsWindowReady()) return;
    SetTargetFPS(60);
    view->initialized = true;
    const char* font_path="resources/shared/Roboto-Regular.ttf";
    if(!FileExists(font_path)) font_path=TextFormat("%sassets/ui/Roboto-Regular.ttf",GetApplicationDirectory());
    view->hud_font=FileExists(font_path) ? LoadFontEx(font_path,32,NULL,0) : GetFontDefault();
    SetTextureFilter(view->hud_font.texture,TEXTURE_FILTER_BILINEAR);
    swat_environment_art_init(&view->environment);
    swat_lighting_init(&view->lighting);
    swat_weapon_art_init(&view->weapons);
    EnableCursor(); // Capture only after entering the game with window focus.
}

static Color swat_material_color(const SwatObject* o) {
    const unsigned char* rgba=swat_material(o->material)->color;
    Color color={rgba[0],rgba[1],rgba[2],rgba[3]};
    if (o->max_health > 0) {
        float ratio = 0.5f+0.5f*o->health/o->max_health;
        color.r = (unsigned char)(color.r*ratio);
        color.g = (unsigned char)(color.g*ratio);
        color.b = (unsigned char)(color.b*ratio);
    }
    return color;
}

static void swat_draw_object(const SwatView* view,const SwatObject* o) {
    if(o->breach_ticks>0) {
        DrawSphereWires(swat_position(o->center),.25f+(24-o->breach_ticks)*.055f,5,8,
            Fade(swat_gold,o->breach_ticks/24.0f));
        if(o->breach_ticks>20) DrawSphere(swat_position(o->center),.18f,swat_paper);
    }
    if (!o->active) return;
    rlPushMatrix();
    rlTranslatef((float)o->center.x,(float)o->center.y,(float)o->center.z);
    rlRotatef(o->yaw/SWAT_RAD,0,1,0);
    rlRotatef(o->pitch/SWAT_RAD,0,0,1);
    Vector3 size = {o->half.x*2,o->half.y*2,o->half.z*2};
    bool art=swat_environment_art_draw(&view->environment,o);
    if(!art) DrawCubeV((Vector3){0},size,swat_material_color(o));
    if(view->debug)
        DrawCubeWiresV((Vector3){0},size,(Color){22,31,36,130});
    if (o->door) {
        bool leaf_art=art && view->environment.door.meshCount && o->material==SWAT_WOOD;
        if(!leaf_art) DrawCube((Vector3){-o->half.x-0.015f,0.06f,o->half.z-0.17f},0.05f,0.055f,0.22f,swat_gold);
        if(!leaf_art) DrawCube((Vector3){o->half.x+0.015f,0.06f,o->half.z-0.17f},0.05f,0.055f,0.22f,swat_gold);
        if(o->wedge_owner>=0) DrawCube((Vector3){0,-o->half.y+.035f,o->half.z-.2f},.22f,.07f,.28f,(Color){184,98,45,255});
        if(o->breach_owner>=0) for(int side=-1;side<=1;side+=2) {
            DrawCube((Vector3){side*(o->half.x+.018f),.12f,o->half.z-.22f},.035f,.18f,.22f,swat_gold);
            DrawCube((Vector3){side*(o->half.x+.04f),.12f,o->half.z-.22f},.014f,.06f,.08f,(Color){205,65,48,255});
        }
    }
    rlPopMatrix();
}

static void swat_draw_actor(SwatView* view,const SwatActor* a) {
    if(!a->present) return;
    b3Pos feet = swat_body_feet_position(&a->controller.body);
    float height = a->controller.body.totalHeight;
    Color uniform = a->role == SWAT_CIVILIAN ? (Color){68,123,153,255} :
        (a->role==SWAT_OFFICER || a->role==SWAT_SNIPER ? (Color){65,79,56,255} : (Color){125,58,52,255});
    if (!a->alive) {
        DrawCube((Vector3){(float)feet.x,0.17f,(float)feet.z},0.48f,0.28f,1.4f,uniform);
        return;
    }
    b3Vec3 lean = a->controller.body.upperOffset;
    Vector3 waist = {(float)feet.x,(float)feet.y+height*0.5f,(float)feet.z};
    Vector3 neck = {(float)feet.x+lean.x,(float)feet.y+height-0.26f,(float)feet.z+lean.z};
    DrawCylinder((Vector3){(float)feet.x,(float)feet.y,(float)feet.z},0.19f,0.22f,height*0.53f,12,(Color){37,44,48,255});
    DrawCapsule(waist,neck,0.245f,8,8,uniform);
    DrawSphere((Vector3){neck.x,neck.y+0.10f,neck.z},0.16f,(Color){175,153,129,255});
    if(a->gear.surrendered || a->gear.restrained) {
        float y=a->gear.restrained ? waist.y : neck.y+.12f;
        float spread=a->gear.restrained ? .12f : .38f;
        Color skin={175,153,129,255};
        DrawCapsule((Vector3){neck.x-.19f,neck.y-.08f,neck.z},(Vector3){neck.x-spread,y,neck.z},.06f,4,4,uniform);
        DrawCapsule((Vector3){neck.x+.19f,neck.y-.08f,neck.z},(Vector3){neck.x+spread,y,neck.z},.06f,4,4,uniform);
        DrawSphere((Vector3){neck.x-spread,y,neck.z},.075f,skin);
        DrawSphere((Vector3){neck.x+spread,y,neck.z},.075f,skin);
        if(a->gear.restrained) DrawCylinderEx((Vector3){neck.x-.12f,y,neck.z},(Vector3){neck.x+.12f,y,neck.z},.022f,.022f,6,swat_gold);
    } else if (a->role != SWAT_CIVILIAN) {
        SwatPose pose=swat_pose(&a->controller,&a->arsenal);
        b3Pos left=b3OffsetPos(pose.shoulder,swat_mul(pose.right,-.19f));
        b3Pos right=b3OffsetPos(pose.shoulder,swat_mul(pose.right,.19f));
        DrawCapsule(swat_position(left),swat_position(pose.left_hand),.065f,6,6,uniform);
        DrawCapsule(swat_position(right),swat_position(pose.right_hand),.065f,6,6,uniform);
        DrawSphere(swat_position(pose.left_hand),.045f,(Color){85,87,69,255});
        DrawSphere(swat_position(pose.right_hand),.045f,(Color){85,87,69,255});
        if(!view || !swat_weapon_art_draw(&view->weapons,&view->lighting,&a->arsenal,&pose))
            DrawCylinderEx(swat_position(pose.shoulder),swat_position(pose.muzzle),.035f,.018f,8,(Color){26,29,31,255});
    }
}

static void swat_draw_floors(const SwatView* view,const SwatSim* sim) {
    const SwatWorld* world=&sim->world;
    // Every mission has authoritative finish-floor boxes. A second room-sized
    // cube only fights their depth and hides their real surface material.
    swat_environment_art_draw_props(&view->environment,world,
        sim->config.mission==SWAT_GENERATED ? &sim->layout : NULL);
}

static void swat_draw_shadow_scene(const SwatSim* sim,bool cutaway) {
    for(int i=0;i<sim->world.count;i++) {
        const SwatObject* o=&sim->world.objects[i];
        if(!o->active || o->material==SWAT_GLASS) continue;
        if(cutaway && o->center.y>2.7f && o->half.x>3 && o->half.z>3) continue;
        rlPushMatrix(); rlTranslatef((float)o->center.x,(float)o->center.y,(float)o->center.z);
        rlRotatef(o->yaw/SWAT_RAD,0,1,0); rlRotatef(o->pitch/SWAT_RAD,0,0,1);
        DrawCubeV((Vector3){0},(Vector3){o->half.x*2,o->half.y*2,o->half.z*2},WHITE); rlPopMatrix();
    }
    if(!cutaway) for(int i=0;i<sim->actor_count;i++) swat_draw_actor(NULL,&sim->actors[i]);
}

static void swat_begin_scene(SwatView* view,const SwatSim* sim,Camera3D camera) {
    BeginMode3D(camera);
    swat_lighting_begin(&view->lighting,&view->environment,&sim->world,camera.position);
}
static void swat_end_scene(SwatView* view) {
    swat_lighting_end(&view->lighting,&view->environment); EndMode3D();
}

static void swat_draw_projectiles(const SwatSim* s) {
    for(int i=0;i<SWAT_MAX_DEVICES;i++) if(s->devices[i].active) {
        const SwatDevice* d=&s->devices[i]; Vector3 p=swat_position(d->position);
        if(d->kind==SWAT_ROBOT) { DrawCube(p,.26f,.18f,.20f,(Color){42,52,56,255}); DrawSphere((Vector3){p.x,p.y+.12f,p.z},.06f,swat_gold); }
        else if(d->kind==SWAT_DRONE) { DrawCube(p,.14f,.07f,.14f,(Color){44,52,58,255}); for(int arm=0;arm<4;arm++) { float angle=arm*SWAT_PI*.5f; DrawCylinder((Vector3){p.x+.16f*cosf(angle),p.y,p.z+.16f*sinf(angle)},.08f,.08f,.015f,8,swat_gold); } }
        else DrawSphere(p,.10f,d->kind==SWAT_BALL ? (Color){53,151,115,255} : swat_gold);
    }
    for(int i=0;i<SWAT_MAX_ACTORS;i++) if(s->evidence[i].dropped && !s->evidence[i].collected) {
        Vector3 p=swat_position(s->evidence[i].position); DrawCube(p,.42f,.06f,.08f,(Color){28,32,35,255});
    }
    for(int i=0;i<SWAT_MAX_PROJECTILES;i++) {
        const SwatProjectile* p=&s->projectiles[i]; if(!p->active) continue;
        Color color=p->kind==SWAT_FLASHBANG ? (Color){220,175,60,255} : (Color){83,150,87,255};
        if(!p->detonated) {
            DrawSphereEx(swat_position(p->position),.065f,6,8,color);
            DrawLine3D(swat_position(p->position),swat_position(b3OffsetPos(p->position,swat_v(0,.1f,0))),swat_paper);
        } else if(p->kind==SWAT_PROBE && p->target>=0) {
            DrawLine3D(swat_position(swat_controller_eye(&s->actors[p->owner].controller)),swat_position(p->position),Fade(swat_gold,.7f));
            DrawSphere(swat_position(p->position),.025f,swat_gold);
        } else if(p->kind==SWAT_FLASHBANG || p->kind==SWAT_LAUNCH_FLASH || p->kind==SWAT_IMPACT_ROUND || p->kind==SWAT_BOLA) {
            DrawSphereWires(swat_position(p->position),.2f+(24-p->remaining_ticks)*.05f,4,8,
                Fade(swat_paper,p->remaining_ticks/24.0f));
        } else {
            float radius=p->kind==SWAT_PEPPERBALL ? 1.25f : fminf(3.5f,.8f+fmaxf(0,p->age-90)*SWAT_DT*.6f);
            for(int cloud=0;cloud<14;cloud++) {
                float angle=cloud*2.4f,extent=radius*(.2f+.65f*(cloud%3)/2);
                b3Pos center=b3OffsetPos(p->position,swat_v(cosf(angle)*extent,.25f+(cloud%4)*.34f,sinf(angle)*extent));
                if(swat_gas_at(s,center)>.05f) DrawSphereEx(swat_position(center),.30f,5,8,
                    (Color){151,165,121,(unsigned char)(50*fminf(1,p->remaining_ticks/120.0f))});
            }
        }
    }
}
static void swat_draw_exposure(const SwatActor* a,int width,int height) {
    if(a->gear.gas_ticks>0) DrawRectangle(0,0,width,height,(Color){100,125,55,(unsigned char)(a->gear.gas_ticks*.55f)});
    if(a->gear.flash_ticks>0) DrawRectangle(0,0,width,height,(Color){242,245,221,(unsigned char)(240*fminf(1,a->gear.flash_ticks/120.0f))});
}
static void swat_draw_plan(SwatView* view,const SwatSim* sim) {
    const SwatMissionDef* mission=swat_sim_mission(sim);
    int preview=view->plan_preview;
    if(preview>mission->overwatch_count) preview=0;
    Camera3D camera={0}; camera.up=(Vector3){0,1,0};
    if(preview) {
        const SwatOverwatch* post=&mission->overwatch[preview-1];
        camera.position=swat_position(post->position); camera.target=swat_position(post->target);
        camera.fovy=55; camera.projection=CAMERA_PERSPECTIVE;
    } else {
        float yaw=view->plan_yaw+.65f;
        float center=sim->config.mission==SWAT_GENERATED ? 4+sim->layout.width*.5f : 11;
        camera.position=(Vector3){center-20*cosf(yaw),24,-20*sinf(yaw)};
        camera.target=(Vector3){center,0,0}; camera.fovy=28; camera.projection=CAMERA_ORTHOGRAPHIC;
        // Centre the model in the space left of the planning sidebar.
        camera.position.x-=7.2f*sinf(yaw); camera.target.x-=7.2f*sinf(yaw);
        camera.position.z+=7.2f*cosf(yaw); camera.target.z+=7.2f*cosf(yaw);
    }
    ClearBackground((Color){57,76,86,255});
    swat_begin_scene(view,sim,camera);
    for(int i=0;i<sim->world.count;i++) {
        const SwatObject* o=&sim->world.objects[i];
        if(!preview && o->center.y>2.7f && o->half.x>3 && o->half.z>3) continue;
        if(o->material!=SWAT_GLASS) swat_draw_object(view,o);
    }
    swat_draw_floors(view,sim);
    if(preview) for(int i=0;i<sim->actor_count;i++) swat_draw_actor(view,&sim->actors[i]);
    for(int i=0;i<sim->world.count;i++) if(sim->world.objects[i].material==SWAT_GLASS) swat_draw_object(view,&sim->world.objects[i]);
    DrawCylinder(swat_position(mission->staging),1.2f,1.2f,.035f,32,swat_gold);
    if(!preview) for(int i=0;i<mission->overwatch_count;i++) {
        Vector3 p=swat_position(mission->overwatch[i].position);
        DrawSphere(p,.3f,swat_gold); DrawLine3D(p,swat_position(mission->overwatch[i].target),swat_gold);
    }
    swat_end_scene(view);
    DrawRectangle(0,0,GetScreenWidth(),96,swat_ink);
    DrawText(TextFormat("%s / OPERATION BRIEF",mission->name),32,22,28,swat_paper);
    DrawText(preview ? mission->overwatch[preview-1].name : "DRONE OVERVIEW / CUTAWAY",34,61,18,swat_gold);
}

static void swat_draw_scope(SwatView* view,const SwatSim* sim,int width,int height) {
    int unit=view->sniper_unit,actor=swat_sniper_actor(unit);
    float reticle_scale=width/1024.0f;
    ClearBackground((Color){12,19,24,255});
    if(unit>=SWAT_SNIPERS) {
        if(!swat_feed_present(sim,unit)) return;
        const SwatDevice* device=&sim->devices[unit-SWAT_SNIPERS];
        if(device->kind==SWAT_BALL) { DrawText("AUDIO LINK",width/2-65,height/2-10,22,swat_gold); return; }
        b3Pos eye=swat_device_eye(device); b3Vec3 aim=swat_direction(device->yaw,device->pitch);
        Camera3D camera={swat_position(eye),swat_position(b3OffsetPos(eye,aim)),{0,1,0},80,CAMERA_PERSPECTIVE};
        swat_begin_scene(view,sim,camera);
        for(int i=0;i<sim->world.count;i++) if(sim->world.objects[i].material!=SWAT_GLASS) swat_draw_object(view,&sim->world.objects[i]);
        swat_draw_floors(view,sim);
        for(int i=0;i<sim->actor_count;i++) swat_draw_actor(view,&sim->actors[i]);
        for(int i=0;i<sim->world.count;i++) if(sim->world.objects[i].material==SWAT_GLASS) swat_draw_object(view,&sim->world.objects[i]);
        swat_end_scene(view); return;
    }
    if(actor<0 || !sim->snipers[unit].deployed || !sim->actors[actor].alive) {
        const char* message=actor>=0 && sim->snipers[unit].deployed ? "SNIPER DOWN" : "NO SNIPER ASSIGNED";
        int size=(int)(40*reticle_scale);
        DrawText(message,(width-MeasureText(message,size))/2,height/2-size/2,size,swat_gold); return;
    }
    const SwatSniper* sniper=&sim->snipers[unit]; const SwatActor* a=&sim->actors[actor];
    b3Pos eye=swat_controller_eye(&a->controller);
    b3Vec3 aim=swat_direction(a->controller.yaw+(view->scope ? view->yaw_offset : 0),
        a->controller.pitch+(view->scope ? view->pitch_offset : 0));
    Camera3D camera={0}; camera.position=swat_position(eye); camera.target=swat_position(b3OffsetPos(eye,aim));
    camera.up=(Vector3){0,1,0}; camera.fovy=sniper->rifle ? 25 : 17; camera.projection=CAMERA_PERSPECTIVE;
    swat_begin_scene(view,sim,camera);
    for(int i=0;i<sim->world.count;i++) if(sim->world.objects[i].material!=SWAT_GLASS) swat_draw_object(view,&sim->world.objects[i]);
    swat_draw_floors(view,sim);
    for(int i=0;i<sim->actor_count;i++) if(i!=actor) swat_draw_actor(view,&sim->actors[i]);
    swat_draw_projectiles(sim);
    for(int i=0;i<sim->world.count;i++) if(sim->world.objects[i].material==SWAT_GLASS) swat_draw_object(view,&sim->world.objects[i]);
    for(int i=0;i<sim->actor_count;i++) if(sim->tick-sim->actors[i].last_shot_tick<=3)
        DrawLine3D(swat_position(sim->actors[i].tracer_start),swat_position(sim->actors[i].tracer_end),swat_gold);
    swat_end_scene(view);
    if(a->gear.gas_ticks>0) DrawRectangle(0,0,width,height,(Color){100,125,55,(unsigned char)(a->gear.gas_ticks*.55f)});
    if(a->gear.flash_ticks>0) DrawRectangle(0,0,width,height,(Color){242,245,221,(unsigned char)(240*fminf(1,a->gear.flash_ticks/120.0f))});
    int cx=width/2,cy=height/2;
    int hx=(int)(230*reticle_scale),hy=(int)(170*reticle_scale),gap=(int)(12*reticle_scale),tick=(int)(5*reticle_scale);
    DrawLine(cx-hx,cy,cx-gap,cy,swat_ink); DrawLine(cx+gap,cy,cx+hx,cy,swat_ink);
    DrawLine(cx,cy-hy,cx,cy-gap,swat_ink); DrawLine(cx,cy+gap,cx,cy+hy,swat_ink);
    DrawCircleLines(cx,cy,10*reticle_scale,sniper->status==SWAT_SNIPER_READY ? GREEN : swat_gold);
    for(int t=-3;t<=3;t++) if(t) {
        int dx=(int)(t*50*reticle_scale),dy=(int)(t*45*reticle_scale);
        DrawLine(cx+dx,cy-tick,cx+dx,cy+tick,swat_ink); DrawLine(cx-tick,cy+dy,cx+tick,cy+dy,swat_ink);
    }
    if(sniper->travel_ticks) DrawText(TextFormat("REPOSITIONING  %.1f s",sniper->travel_ticks/60.0f),cx-140,cy+70,22,swat_gold);
}

static void swat_draw_weapon(SwatView* view,const SwatSim* s,const SwatActor* a,const SwatController* c) {
    SwatPose pose=swat_pose(c,&a->arsenal);
    const SwatWeaponDef* definition=swat_arsenal_def(&a->arsenal,a->arsenal.active);
    if(!swat_weapon_art_draw(&view->weapons,&view->lighting,&a->arsenal,&pose)) {
        b3Vec3 axis=swat_normalize(b3SubPos(pose.muzzle,pose.shoulder));
        b3Pos receiver=b3OffsetPos(pose.shoulder,swat_mul(axis,definition->barrel*.35f));
        DrawCylinderEx(swat_position(pose.shoulder),swat_position(receiver),.033f,.033f,8,(Color){26,32,35,255});
        DrawCylinderEx(swat_position(receiver),swat_position(pose.muzzle),.020f,.013f,8,(Color){19,23,27,255});
        b3Pos grip=b3OffsetPos(pose.right_hand,swat_mul(pose.up,-.065f));
        DrawCylinderEx(swat_position(pose.right_hand),swat_position(grip),.023f,.020f,6,(Color){30,36,40,255});
        b3Pos magazine=b3OffsetPos(receiver,swat_mul(pose.up,-.10f));
        if(a->arsenal.slots[a->arsenal.active].magazine_seated)
            DrawCylinderEx(swat_position(receiver),swat_position(magazine),.026f,.020f,6,(Color){38,43,47,255});
        DrawSphere(swat_position(pose.sight),a->arsenal.sight==SWAT_OPTIC ? .026f : .012f,(Color){54,60,64,255});
    }
    DrawSphere(swat_position(pose.right_hand),.040f,(Color){85,87,69,255});
    DrawSphere(swat_position(pose.left_hand),.040f,(Color){85,87,69,255});
    if(s->tick-a->last_shot_tick<=2) DrawSphere(swat_position(pose.muzzle),.035f,(Color){255,216,112,230});
}

void swat_view_draw(SwatView* view, const SwatSim* s, bool policy, float vertical_fov) {
    if (!view->initialized) return;
    Vector3 light_eye={11,1.6f,0};
    if(view->actor>=0 && view->actor<s->actor_count && s->actors[view->actor].present)
        light_eye=swat_position(swat_controller_eye(&s->actors[view->actor].controller));
    swat_lighting_prepare(&view->lighting,s,light_eye,view->planning && !view->plan_preview,swat_draw_shadow_scene);
    if(view->planning) { swat_draw_plan(view,s); return; }
    if(view->actor<0 || view->actor>=s->actor_count || !s->actors[view->actor].present) {
        ClearBackground((Color){25,37,47,255});
        DrawText("Receiving session...",40,120,24,swat_paper); return;
    }
    const SwatActor* a=&s->actors[view->actor];
    bool show_camera=view->sniper_camera && (s->mission.overwatch_count>0 || view->sniper_unit>=SWAT_SNIPERS) && !policy;
    if(show_camera) {
        // A 280px inset does not need the full takeover render target. Keep a
        // little supersampling when compact; restore 1024px during expansion.
        int feed_width=view->camera_expansion>.05f ? 1024 : 512;
        if(view->camera_target.id && view->camera_target.texture.width!=feed_width) {
            UnloadRenderTexture(view->camera_target); view->camera_target=(RenderTexture2D){0};
        }
        if(!view->camera_target.id) view->camera_target=LoadRenderTexture(feed_width,feed_width*9/16);
        if(view->camera_target.id) {
            BeginTextureMode(view->camera_target);
            swat_draw_scope(view,s,view->camera_target.texture.width,view->camera_target.texture.height);
            EndTextureMode();
        }
    }
    SwatController displayed=a->controller;
    displayed.yaw=swat_angle(displayed.yaw+(view->scope ? 0 : view->yaw_offset));
    displayed.pitch=swat_clamp(displayed.pitch+(view->scope ? 0 : view->pitch_offset),-85*SWAT_RAD,85*SWAT_RAD);
    const SwatController* c=&displayed;
    const SwatWeapon* w=&a->arsenal.slots[a->arsenal.active];
    b3Vec3 forward,right,up;
    swat_controller_view(c,&forward,&right,&up);
    b3Pos eye=swat_controller_eye(c);
    if(a->gear.inspecting) {
        eye=swat_sim_inspection_camera(s,view->actor);
        forward=swat_direction(a->controller.yaw+a->gear.wand_yaw+(view->scope ? 0 : view->yaw_offset),
            swat_clamp(a->gear.wand_pitch+(view->scope ? 0 : view->pitch_offset),-80*SWAT_RAD,80*SWAT_RAD));
        up=swat_v(0,1,0);
    }
    Camera3D camera = {0};
    camera.position=swat_position(eye);
    camera.target=swat_position(b3OffsetPos(eye,forward));
    float fov=policy ? 70 : vertical_fov;
    camera.up=swat_vector(up); camera.fovy=fov+((a->arsenal.sight==SWAT_OPTIC ? 30 : 45)-fov)*c->ads; camera.projection=CAMERA_PERSPECTIVE;
    int width=GetScreenWidth(), height=GetScreenHeight();
    ClearBackground((Color){25,37,47,255});
    swat_begin_scene(view,s,camera);
    for (int i=0;i<s->world.count;i++) if (s->world.objects[i].material != SWAT_GLASS)
        swat_draw_object(view,&s->world.objects[i]);
    swat_draw_floors(view,s);
    for (int i=0;i<s->actor_count;i++) if(i!=view->actor) swat_draw_actor(view,&s->actors[i]);
    swat_draw_projectiles(s);
    for (int i=0;i<s->world.count;i++) if (s->world.objects[i].material == SWAT_GLASS)
        swat_draw_object(view,&s->world.objects[i]);
    bool cleared=swat_sim_hostiles(s)==0 && (s->config.mission==SWAT_ANNEX || !swat_sim_unsecured(s));
    Color extraction = cleared ? (Color){99,197,144,255} : (Color){177,146,77,180};
    DrawCylinder((Vector3){(float)s->extraction.x,0.01f,(float)s->extraction.z},1.3f,1.3f,0.025f,32,extraction);
    for (int i=0;i<s->actor_count;i++) if (s->tick-s->actors[i].last_shot_tick <= 3)
        DrawLine3D(swat_position(s->actors[i].tracer_start),swat_position(s->actors[i].tracer_end),(Color){250,200,98,180});
    if (a->alive && !a->gear.inspecting) swat_draw_weapon(view,s,a,c);
    if(view->debug) {
        for(int i=0;i<s->actor_count;i++) if(s->actors[i].present && s->actors[i].alive) {
            const SwatActor* actor=&s->actors[i];
            b3Capsule capsule=b3Shape_GetCapsule(actor->controller.body.capsuleId);
            b3Transform transform=b3Body_GetTransform(actor->controller.body.body);
            DrawCapsuleWires(swat_position(b3TransformPoint(transform,capsule.center1)),
                swat_position(b3TransformPoint(transform,capsule.center2)),capsule.radius,8,8,GREEN);
            SwatPose pose=swat_pose(&actor->controller,&actor->arsenal);
            DrawSphereWires(swat_position(pose.eye),.06f,6,6,BLUE);
            DrawSphereWires(swat_position(pose.muzzle),.035f,6,6,RED);
            DrawLine3D(swat_position(pose.eye),swat_position(pose.muzzle),YELLOW);
            if(s->tick-actor->last_shot_tick<60) DrawLine3D(swat_position(actor->tracer_start),swat_position(actor->tracer_end),RED);
        }
        for(int row=0;row<SWAT_SENSOR_ROWS;row++) for(int col=0;col<SWAT_SENSOR_COLS;col++) {
            b3Vec3 direction=swat_direction(c->yaw+(col-(SWAT_SENSOR_COLS-1)*.5f)*8*SWAT_RAD,
                c->pitch+((SWAT_SENSOR_ROWS-1)*.5f-row)*8*SWAT_RAD);
            SwatHit hit=swat_world_ray(&s->world,eye,direction,30,c->body.body);
            DrawLine3D(swat_position(eye),swat_position(hit.point),Fade(hit.kind==SWAT_HIT_ACTOR ? RED : SKYBLUE,.3f));
        }
    }
    swat_end_scene(view);
    swat_draw_exposure(a,width,height);

    if(view->debug) swat_hud_center(view,TextFormat("%d FPS / %.1f ms / tick %d / %d objects / %s",GetFPS(),GetFrameTime()*1000,s->tick,s->world.count,view->lighting.enabled ? "lit" : "unlit"),width/2,80,14,swat_gold);
    swat_hud_text(view,swat_sim_mission(s)->name,24,22,15,swat_paper);
    const char* objective=swat_sim_hostiles(s) ? "Secure the armed threats" :
        (s->config.mission!=SWAT_ANNEX && swat_sim_unsecured(s) ? "Restrain the hostages" : "Return to extraction");
    swat_hud_text(view,objective,24,43,15,swat_gold);
    if(view->session_status[0] || policy)
        swat_hud_text(view,policy ? "Policy control" : view->session_status,24,64,13,(Color){180,191,195,255});

    Color reticle = c->muzzle_blocked ? (Color){230,110,80,255} : swat_gold;
    int cx=width/2, cy=height/2;
    int gap=3+(int)((1-c->ads)*5);
    DrawLine(cx-gap-8,cy,cx-gap,cy,reticle); DrawLine(cx+gap,cy,cx+gap+8,cy,reticle);
    DrawLine(cx,cy-gap-8,cx,cy-gap,reticle); DrawLine(cx,cy+gap,cx,cy+gap+8,reticle);
    if(a->gear.inspecting) {
        const char* modes[]={"Forward","Under door","Corner left","Corner right","Over cover"};
        swat_hud_center(view,TextFormat("Optiwand / %s",modes[a->gear.wand_mode]),cx,28,16,swat_paper);
        b3Vec3 base_forward=swat_direction(a->controller.yaw,0);
        float reach=b3Dot(b3SubPos(eye,swat_body_feet_position(&a->controller.body)),base_forward);
        swat_hud_center(view,reach<.5f ? "Lens blocked / reposition" : "Release [G] to return",cx,51,14,swat_gold);
    } else if(c->muzzle_blocked) swat_hud_center(view,"Muzzle obstructed",cx,cy+27,14,reticle);
    if(!a->gear.inspecting && !view->scope) swat_draw_context(view,s,swat_context(s,view->actor),width,height);

    int officer_number=view->actor==0 ? 1 : view->actor-1;
    swat_hud_text(view,TextFormat("Gold %02d / %d%%",officer_number,(int)a->health),24,height-48,16,
        a->health<40 ? (Color){230,110,80,255} : swat_paper);
    if(c->stamina<.98f) {
        DrawRectangle(24,height-24,92,2,(Color){0,0,0,160});
        DrawRectangle(24,height-24,(int)(92*c->stamina),2,swat_gold);
    }
    if(a->gear.wounds[SWAT_LEGS]>0 || a->gear.wounds[SWAT_ARMS]>0)
        swat_hud_text(view,"Injured",24,height-70,14,(Color){230,110,80,255});
    int remaining=s->config.max_ticks-s->tick;
    if(remaining<60*60) swat_hud_center(view,TextFormat("%02d:%02d",remaining/3600,(remaining/60)%60),cx,height-40,16,swat_gold);
    const char* name=swat_arsenal_def(&a->arsenal,a->arsenal.active)->name;
    swat_hud_text(view,name,width-24-swat_hud_measure(view,name,14),height-84,14,swat_paper);
    const char* ammo=TextFormat("%02d+%d / %03d",w->magazine,w->chambered,w->reserve);
    swat_hud_text(view,ammo,width-24-swat_hud_measure(view,ammo,24),height-62,24,swat_paper);
    const char* modes[]={"Safe","Semi","Auto"};
    const char* reload_names[]={"Reloading","Removing magazine","Inserting magazine","Chambering"};
    const char* state=w->reload_remaining ? reload_names[w->reload_stage] : (a->arsenal.equip_remaining ? "Equipping" : modes[w->mode]);
    swat_hud_text(view,state,width-24-swat_hud_measure(view,state,14),height-30,14,swat_gold);
    if(c->ready!=SWAT_READY) swat_hud_center(view,c->ready==SWAT_HIGH_READY ? "High ready" : "Low ready",cx,height-40,14,swat_paper);
    if(show_camera) {
        bool has_feed=swat_feed_present(s,view->sniper_unit);
        SwatCameraLayout layout=swat_camera_layout(width,height,view->camera_expansion,has_feed);
        DrawRectangle(0,0,width,height,(Color){0,0,0,(unsigned char)(70*view->camera_expansion)});
        if(has_feed && view->camera_target.id) DrawTexturePro(view->camera_target.texture,
            (Rectangle){0,0,view->camera_target.texture.width,-view->camera_target.texture.height},layout.feed,(Vector2){0},0,WHITE);
        if(has_feed) DrawRectangleLinesEx(layout.feed,1,(Color){174,186,190,110});
    }
}

void swat_view_close(SwatView* view) {
    if (view->initialized && IsWindowReady()) {
        if(view->camera_target.id) UnloadRenderTexture(view->camera_target);
        if(view->hud_font.texture.id && view->hud_font.texture.id!=GetFontDefault().texture.id) UnloadFont(view->hud_font);
        swat_environment_art_close(&view->environment);
        swat_weapon_art_close(&view->weapons);
        swat_lighting_close(&view->lighting);
        CloseWindow();
    }
    memset(view,0,sizeof(*view));
}
