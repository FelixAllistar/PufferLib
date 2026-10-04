#include "render.h"
#include "rlgl.h"
#include <string.h>

static Vector3 swat_vector(b3Vec3 v) { return (Vector3){v.x,v.y,v.z}; }
static Vector3 swat_position(b3Pos p) { return (Vector3){(float)p.x,(float)p.y,(float)p.z}; }
static const Color swat_gold = {226,181,82,255};
static const Color swat_ink = {12,19,26,235};
static const Color swat_paper = {224,231,229,255};

static Texture2D swat_load_ui(const char* name) {
    const char* path=TextFormat("ocean/swat/assets/ui/%s",name);
    if(!FileExists(path)) path=TextFormat("%sassets/ui/%s",GetApplicationDirectory(),name);
    return FileExists(path) ? LoadTexture(path) : (Texture2D){0};
}

static void swat_draw_panel(const SwatView* view,Rectangle box) {
    if(!view->panel_skin.id) { DrawRectangleRec(box,swat_ink); return; }
    // Source corners and output borders have independent sizes, so the same
    // generated skin stays subtle on both an inset and a large floating panel.
    Texture2D texture=view->panel_skin;
    float source_x[]={0,96,texture.width-96,texture.width};
    float source_y[]={0,96,texture.height-96,texture.height};
    float x[]={box.x,box.x+12,box.x+box.width-12,box.x+box.width};
    float y[]={box.y,box.y+12,box.y+box.height-12,box.y+box.height};
    for(int row=0;row<3;row++) for(int col=0;col<3;col++)
        DrawTexturePro(texture,(Rectangle){source_x[col],source_y[row],source_x[col+1]-source_x[col],source_y[row+1]-source_y[row]},
            (Rectangle){x[col],y[row],x[col+1]-x[col],y[row+1]-y[row]},(Vector2){0},0,WHITE);
}

static void swat_draw_ui_icon(const SwatView* view,int icon,Rectangle box) {
    if(!view->ui_icons.id) return;
    float width=view->ui_icons.width*.5f,height=view->ui_icons.height*.5f;
    DrawTexturePro(view->ui_icons,(Rectangle){(icon%2)*width,(icon/2)*height,width,height},box,(Vector2){0},0,WHITE);
}

static void swat_draw_context(const SwatView* view,const SwatSim* sim,SwatContext context,int width,int height) {
    if(context.action==SWAT_CONTEXT_NONE) return;
    const SwatActor* actor=&sim->actors[view->actor];
    const char* title="",*action="",*detail=""; int icon=3;
    switch(context.action) {
        case SWAT_CONTEXT_OPEN: case SWAT_CONTEXT_CLOSE:
            title="DOOR / UNLOCKED";
            action=context.ready ? (context.action==SWAT_CONTEXT_OPEN ? "F  Open door" : "F  Close door") : "Move closer to use the door";
            detail="RMB / Z  Aim"; break;
        case SWAT_CONTEXT_LOCKED:
            title="DOOR / LOCKED"; icon=2;
            action=context.ready ? "Hold RMB  Pick lock" : "Move closer to pick the lock";
            detail=swat_kit(actor->gear.kit)->optiwand ? "F  Check lock     G  Inspect     L  Pick" : "F  Check lock     L  Pick"; break;
        case SWAT_CONTEXT_CHARGE:
            title="DOOR / CHARGE MOUNTED"; icon=2;
            action=sim->world.objects[context.hit.index].breach_owner==view->actor ? "K  Detonate your charges" : "Teammate controls this charge";
            detail="RMB / Z  Aim"; break;
        case SWAT_CONTEXT_CUFF:
            title="PERSON / COMPLIANT"; icon=1;
            action=context.ready ? "Hold RMB  Handcuff" : "Move closer to handcuff";
            detail="F  Request compliance     Z  Aim"; break;
        case SWAT_CONTEXT_COMPLY:
            title="PERSON / UNSECURED";
            action="F  Request compliance"; detail="RMB / Z  Aim"; break;
        case SWAT_CONTEXT_SECURED:
            title="PERSON / SECURED"; icon=1;
            action="Handcuffed"; detail="RMB / Z  Aim"; break;
        default: return;
    }
    const SwatEquipment* gear=&actor->gear;
    float progress=gear->door_ticks ? gear->door_ticks/(gear->door_mode==SWAT_LOCKPICK ? 180.0f : 90.0f) : gear->cuff_ticks/72.0f;
    if(gear->door_ticks) { action=gear->door_mode==SWAT_LOCKPICK ? "Picking lock..." : "Mounting charge..."; icon=2; }
    if(gear->cuff_ticks) action="Handcuffing...";
    if(progress>0) detail="Keep holding; turn away to cancel";
    Rectangle box={width*.5f-214,height-288,428,88};
    swat_draw_panel(view,box);
    swat_draw_ui_icon(view,icon,(Rectangle){box.x+12,box.y+19,46,46});
    int x=(int)box.x+(view->ui_icons.id ? 68 : 18),y=(int)box.y+13;
    DrawText(title,x,y,13,swat_gold);
    DrawText(action,x,y+20,18,context.ready ? swat_paper : (Color){166,178,180,255});
    DrawText(detail,x,y+46,13,(Color){153,172,178,255});
    if(progress>0) {
        DrawRectangle((int)box.x+16,(int)(box.y+box.height)-10,(int)box.width-32,3,(Color){55,69,72,255});
        DrawRectangle((int)box.x+16,(int)(box.y+box.height)-10,(int)((box.width-32)*progress),3,swat_gold);
    }
}

SwatCameraLayout swat_camera_layout(int width,int height,float expansion) {
    float t=swat_clamp(expansion,0,1); t=t*t*(3-2*t);
    float small=fminf(384,width-48),large=fminf(960,fminf(width-96,(height-210)*16.0f/9+16));
    float w=small+(large-small)*t,h=(w-16)*9.0f/16+114;
    float x=(width-small-24)*(1-t)+(width-w)*.5f*t;
    float y=110*(1-t)+(height-h)*.5f*t;
    SwatCameraLayout layout={0};
    layout.panel=(Rectangle){x,y,w,h};
    layout.feed=(Rectangle){x+8,y+40,w-16,(w-16)*9.0f/16};
    layout.close=(Rectangle){x+w-38,y+7,28,26};
    float buttons=layout.feed.y+layout.feed.height+8;
    for(int i=0;i<SWAT_SNIPERS;i++) layout.unit[i]=(Rectangle){x+8+i*48,buttons,42,28};
    layout.previous=(Rectangle){x+106,buttons,34,28};
    layout.next=(Rectangle){x+146,buttons,34,28};
    layout.takeover=(Rectangle){x+w-160,buttons,152,28};
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
    view->panel_skin=swat_load_ui("panel.png");
    view->ui_icons=swat_load_ui("icons.png");
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

static void swat_draw_object(const SwatObject* o) {
    if(o->breach_ticks>0) {
        DrawSphereWires(swat_position(o->center),.25f+(24-o->breach_ticks)*.055f,5,8,
            Fade(swat_gold,o->breach_ticks/24.0f));
        if(o->breach_ticks>20) DrawSphere(swat_position(o->center),.18f,swat_paper);
    }
    if (!o->active) return;
    rlPushMatrix();
    rlTranslatef((float)o->center.x,(float)o->center.y,(float)o->center.z);
    rlRotatef(o->yaw/SWAT_RAD,0,1,0);
    Vector3 size = {o->half.x*2,o->half.y*2,o->half.z*2};
    DrawCubeV((Vector3){0},size,swat_material_color(o));
    if(o->part!=SWAT_PART_SKIN) DrawCubeWiresV((Vector3){0},size,(Color){22,31,36,130});
    if (o->door) {
        DrawCube((Vector3){-o->half.x-0.015f,0.06f,o->half.z-0.17f},0.05f,0.055f,0.22f,swat_gold);
        DrawCube((Vector3){o->half.x+0.015f,0.06f,o->half.z-0.17f},0.05f,0.055f,0.22f,swat_gold);
        if(o->breach_owner>=0) for(int side=-1;side<=1;side+=2) {
            DrawCube((Vector3){side*(o->half.x+.018f),.12f,o->half.z-.22f},.035f,.18f,.22f,swat_gold);
            DrawCube((Vector3){side*(o->half.x+.04f),.12f,o->half.z-.22f},.014f,.06f,.08f,(Color){205,65,48,255});
        }
    }
    rlPopMatrix();
}

static void swat_draw_actor(const SwatActor* a) {
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
        b3Pos eye = swat_controller_eye(&a->controller);
        b3Vec3 direction = swat_controller_aim(&a->controller);
        b3Pos start = b3OffsetPos(eye,swat_v(0,-0.24f,0));
        DrawCylinderEx(swat_position(start),swat_position(b3OffsetPos(start,swat_mul(direction,0.65f))),0.045f,0.035f,6,(Color){26,29,31,255});
    }
}

static void swat_draw_floors(const SwatWorld* world) {
    for(int i=0;i<world->room_count;i++) {
        const SwatRoom* room=&world->rooms[i];
        const unsigned char* rgba=swat_material(room->floor)->color;
        Color color={rgba[0],rgba[1],rgba[2],255};
        DrawCube((Vector3){(float)room->center.x,.004f,(float)room->center.z},room->half.x*2,.008f,room->half.z*2,color);
    }
}

static void swat_draw_projectiles(const SwatSim* s) {
    for(int i=0;i<SWAT_MAX_PROJECTILES;i++) {
        const SwatProjectile* p=&s->projectiles[i]; if(!p->active) continue;
        Color color=p->kind==SWAT_FLASHBANG ? (Color){220,175,60,255} : (Color){83,150,87,255};
        if(!p->detonated) {
            DrawSphereEx(swat_position(p->position),.065f,6,8,color);
            DrawLine3D(swat_position(p->position),swat_position(b3OffsetPos(p->position,swat_v(0,.1f,0))),swat_paper);
        } else if(p->kind==SWAT_FLASHBANG) {
            DrawSphereWires(swat_position(p->position),.2f+(24-p->remaining_ticks)*.05f,4,8,
                Fade(swat_paper,p->remaining_ticks/24.0f));
        } else {
            float radius=fminf(3.5f,.8f+(p->age-90)*SWAT_DT*.6f);
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
    if(a->gear.gas_ticks>0) DrawRectangle(0,96,width,height-148,(Color){100,125,55,(unsigned char)(a->gear.gas_ticks*.55f)});
    if(a->gear.flash_ticks>0) DrawRectangle(0,96,width,height-148,(Color){242,245,221,(unsigned char)(240*fminf(1,a->gear.flash_ticks/120.0f))});
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
    BeginMode3D(camera);
    for(int i=0;i<sim->world.count;i++) {
        const SwatObject* o=&sim->world.objects[i];
        if(!preview && o->center.y>2.7f && o->half.x>3 && o->half.z>3) continue;
        if(o->material!=SWAT_GLASS) swat_draw_object(o);
    }
    swat_draw_floors(&sim->world);
    if(preview) for(int i=0;i<sim->actor_count;i++) swat_draw_actor(&sim->actors[i]);
    for(int i=0;i<sim->world.count;i++) if(sim->world.objects[i].material==SWAT_GLASS) swat_draw_object(&sim->world.objects[i]);
    DrawCylinder(swat_position(mission->staging),1.2f,1.2f,.035f,32,swat_gold);
    if(!preview) for(int i=0;i<mission->overwatch_count;i++) {
        Vector3 p=swat_position(mission->overwatch[i].position);
        DrawSphere(p,.3f,swat_gold); DrawLine3D(p,swat_position(mission->overwatch[i].target),swat_gold);
    }
    EndMode3D();
    DrawRectangle(0,0,GetScreenWidth(),96,swat_ink);
    DrawText(TextFormat("%s / OPERATION BRIEF",mission->name),32,22,28,swat_paper);
    DrawText(preview ? mission->overwatch[preview-1].name : "DRONE OVERVIEW / CUTAWAY",34,61,18,swat_gold);
}

static void swat_draw_scope(const SwatView* view,const SwatSim* sim,int width,int height) {
    int unit=view->sniper_unit,actor=swat_sniper_actor(unit);
    ClearBackground((Color){12,19,24,255});
    if(actor<0 || !sim->snipers[unit].deployed || !sim->actors[actor].alive) {
        const char* message=actor>=0 && sim->snipers[unit].deployed ? "SNIPER DOWN" : "NO SNIPER ASSIGNED";
        DrawText(message,(width-MeasureText(message,40))/2,height/2-20,40,swat_gold); return;
    }
    const SwatSniper* sniper=&sim->snipers[unit]; const SwatActor* a=&sim->actors[actor];
    b3Pos eye=swat_controller_eye(&a->controller);
    b3Vec3 aim=swat_direction(a->controller.yaw+(view->scope ? view->yaw_offset : 0),
        a->controller.pitch+(view->scope ? view->pitch_offset : 0));
    Camera3D camera={0}; camera.position=swat_position(eye); camera.target=swat_position(b3OffsetPos(eye,aim));
    camera.up=(Vector3){0,1,0}; camera.fovy=sniper->rifle ? 25 : 17; camera.projection=CAMERA_PERSPECTIVE;
    BeginMode3D(camera);
    for(int i=0;i<sim->world.count;i++) if(sim->world.objects[i].material!=SWAT_GLASS) swat_draw_object(&sim->world.objects[i]);
    swat_draw_floors(&sim->world);
    for(int i=0;i<sim->actor_count;i++) if(i!=actor) swat_draw_actor(&sim->actors[i]);
    swat_draw_projectiles(sim);
    for(int i=0;i<sim->world.count;i++) if(sim->world.objects[i].material==SWAT_GLASS) swat_draw_object(&sim->world.objects[i]);
    for(int i=0;i<sim->actor_count;i++) if(sim->tick-sim->actors[i].last_shot_tick<=3)
        DrawLine3D(swat_position(sim->actors[i].tracer_start),swat_position(sim->actors[i].tracer_end),swat_gold);
    EndMode3D();
    if(a->gear.gas_ticks>0) DrawRectangle(0,0,width,height,(Color){100,125,55,(unsigned char)(a->gear.gas_ticks*.55f)});
    if(a->gear.flash_ticks>0) DrawRectangle(0,0,width,height,(Color){242,245,221,(unsigned char)(240*fminf(1,a->gear.flash_ticks/120.0f))});
    int cx=width/2,cy=height/2;
    DrawLine(cx-230,cy,cx-12,cy,swat_ink); DrawLine(cx+12,cy,cx+230,cy,swat_ink);
    DrawLine(cx,cy-170,cx,cy-12,swat_ink); DrawLine(cx,cy+12,cx,cy+170,swat_ink);
    DrawCircleLines(cx,cy,10,sniper->status==SWAT_SNIPER_READY ? GREEN : swat_gold);
    for(int t=-3;t<=3;t++) if(t) { DrawLine(cx+t*50,cy-5,cx+t*50,cy+5,swat_ink); DrawLine(cx-5,cy+t*45,cx+5,cy+t*45,swat_ink); }
    if(sniper->travel_ticks) DrawText(TextFormat("REPOSITIONING  %.1f s",sniper->travel_ticks/60.0f),cx-140,cy+70,22,swat_gold);
}

static void swat_draw_weapon(const SwatSim* s,const SwatActor* a,const SwatController* c) {
    b3Vec3 forward,right,up;
    swat_controller_view(c,&forward,&right,&up);
    b3Pos eye = swat_controller_eye(c);
    float lowered = swat_weapons_busy(&a->arsenal) || c->sprinting ? 0.18f : 0;
    b3Vec3 offset = swat_add(swat_mul(forward,c->muzzle_blocked ? 0.13f : 0.25f),
        swat_add(swat_mul(right,0.12f*(1-c->ads)),swat_mul(up,-0.19f-lowered)));
    b3Pos center = b3OffsetPos(eye,offset);
    rlPushMatrix();
    rlTranslatef((float)center.x,(float)center.y,(float)center.z);
    rlRotatef(-(c->yaw+c->recoil_yaw)/SWAT_RAD,0,1,0);
    rlRotatef((c->pitch+c->recoil_pitch)/SWAT_RAD,0,0,1);
    rlRotatef(c->lean*8,1,0,0);
    float length = a->arsenal.active == 0 ? 0.26f : 0.16f;
    DrawCube((Vector3){0},length,0.07f,0.055f,(Color){26,32,35,255});
    DrawCube((Vector3){length*0.55f,0,0},length*0.65f,0.025f,0.022f,(Color){19,23,27,255});
    DrawCube((Vector3){-0.03f,-0.085f,0},0.065f,0.12f,0.04f,(Color){30,36,40,255});
    DrawCube((Vector3){0,0.047f,0},0.065f,0.022f,0.043f,swat_gold);
    DrawSphere((Vector3){-0.02f,-0.04f,-0.03f},0.045f,(Color){85,87,69,255});
    if (s->tick-a->last_shot_tick <= 2)
        DrawSphere((Vector3){length*0.9f,0,0},0.047f,(Color){255,216,112,230});
    rlPopMatrix();
}

void swat_view_draw(SwatView* view, const SwatSim* s, bool policy, float vertical_fov) {
    if (!view->initialized) return;
    if(view->planning) { swat_draw_plan(view,s); return; }
    if(view->actor<0 || view->actor>=s->actor_count || !s->actors[view->actor].present) {
        ClearBackground((Color){25,37,47,255});
        DrawText("Receiving session...",40,120,24,swat_paper); return;
    }
    const SwatActor* a=&s->actors[view->actor];
    bool show_camera=view->sniper_camera && s->mission.overwatch_count>0 && !policy;
    if(show_camera) {
        if(!view->camera_target.id) view->camera_target=LoadRenderTexture(1024,576);
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
    camera.up=swat_vector(up); camera.fovy=fov+(45-fov)*c->ads; camera.projection=CAMERA_PERSPECTIVE;
    int width=GetScreenWidth(), height=GetScreenHeight();
    ClearBackground((Color){25,37,47,255});
    BeginMode3D(camera);
    for (int i=0;i<s->world.count;i++) if (s->world.objects[i].material != SWAT_GLASS)
        swat_draw_object(&s->world.objects[i]);
    swat_draw_floors(&s->world);
    for (int i=0;i<s->actor_count;i++) if(i!=view->actor) swat_draw_actor(&s->actors[i]);
    swat_draw_projectiles(s);
    for (int i=0;i<s->world.count;i++) if (s->world.objects[i].material == SWAT_GLASS)
        swat_draw_object(&s->world.objects[i]);
    bool cleared=swat_sim_hostiles(s)==0 && (s->config.mission==SWAT_ANNEX || !swat_sim_unsecured(s));
    Color extraction = cleared ? (Color){99,197,144,255} : (Color){177,146,77,180};
    DrawCylinder((Vector3){(float)s->extraction.x,0.01f,(float)s->extraction.z},1.3f,1.3f,0.025f,32,extraction);
    for (int i=0;i<s->actor_count;i++) if (s->tick-s->actors[i].last_shot_tick <= 3)
        DrawLine3D(swat_position(s->actors[i].tracer_start),swat_position(s->actors[i].tracer_end),(Color){250,200,98,180});
    if (a->alive && !a->gear.inspecting) swat_draw_weapon(s,a,c);
    EndMode3D();
    swat_draw_exposure(a,width,height);

    DrawRectangle(0,0,width,96,swat_ink);
    DrawRectangle(26,25,4,45,swat_gold);
    DrawText("SWAT",44,20,32,swat_paper);
    DrawText("G O L D  E L E M E N T",46,60,16,swat_gold);
    DrawText(swat_sim_mission(s)->name,width-310,24,21,swat_paper);
    DrawText(view->session_status[0] ? view->session_status :
        (policy ? "POLICY CONTROL" : "SOLO / OFFLINE"),width-310,57,15,swat_gold);
    DrawRectangle(24,118,370,72,swat_ink);
    DrawText(swat_sim_hostiles(s) ? "SECURE THE ARMED THREATS" :
        (s->config.mission!=SWAT_ANNEX && swat_sim_unsecured(s) ? "RESTRAIN THE HOSTAGES" : "MOVE TO THE EXTRACTION MARKER"),40,130,17,swat_paper);
    DrawText(s->config.mission!=SWAT_ANNEX ? TextFormat("%d threats / %d hostages unsecured",swat_sim_hostiles(s),swat_sim_unsecured(s)) : "Protect the unarmed civilian",40,159,16,(Color){160,178,183,255});

    Color reticle = c->muzzle_blocked ? (Color){230,110,80,255} : swat_gold;
    int cx=width/2, cy=height/2;
    int gap=3+(int)((1-c->ads)*5);
    DrawLine(cx-gap-8,cy,cx-gap,cy,reticle); DrawLine(cx+gap,cy,cx+gap+8,cy,reticle);
    DrawLine(cx,cy-gap-8,cx,cy-gap,reticle); DrawLine(cx,cy+gap,cx,cy+gap+8,reticle);
    if(a->gear.inspecting) {
        DrawRectangleLinesEx((Rectangle){16,106,width-32,height-170},3,swat_gold);
        const char* modes[]={"FORWARD","UNDER DOOR","CORNER LEFT","CORNER RIGHT","OVER COVER"};
        DrawText(TextFormat("OPTIWAND / %s",modes[a->gear.wand_mode]),cx-150,120,20,swat_gold);
        b3Vec3 base_forward=swat_direction(a->controller.yaw,0);
        float reach=b3Dot(b3SubPos(eye,swat_body_feet_position(&a->controller.body)),base_forward);
        DrawText(reach<.5f ? "LENS BLOCKED / reposition or select another reach" : "LENS INSERTED / mouse rotates the lens",cx-230,148,17,swat_paper);
        DrawText("Hold G / Q-E reach around / Ctrl under / Space over / release G to move",cx-320,height-195,17,swat_paper);
    } else if (c->muzzle_blocked) DrawText("MUZZLE OBSTRUCTED",cx-95,cy+30,16,reticle);
    if(!a->gear.inspecting) swat_draw_context(view,s,swat_context(s,view->actor),width,height);

    DrawRectangle(24,height-158,288,86,swat_ink);
    int officer_number=view->actor==0 ? 1 : view->actor-1;
    DrawText(TextFormat("GOLD %02d",officer_number),40,height-145,18,swat_gold);
    DrawText(TextFormat("HEALTH  %03d",(int)a->health),40,height-115,23,swat_paper);
    DrawRectangle(40,height-85,248,4,(Color){50,66,69,255});
    DrawRectangle(40,height-85,(int)(248*c->stamina),4,swat_gold);
    DrawRectangle(width-340,height-176,316,104,swat_ink);
    DrawText(swat_arsenal_def(&a->arsenal,a->arsenal.active)->name,width-320,height-164,19,swat_gold);
    const char* modes[]={"SAFE","SEMI","AUTO"};
    DrawText(TextFormat("%02d+%d  /  %03d",w->magazine,w->chambered,w->reserve),width-320,height-133,30,swat_paper);
    DrawText(w->reload_remaining ? "RELOADING" : (a->arsenal.equip_remaining ? "EQUIPPING" : modes[w->mode]),width-320,height-94,16,swat_gold);
    const char* stance=c->sprinting ? "SPRINT" : (c->body.crouched ? "CROUCH" : "STAND");
    DrawText(TextFormat("%s   LEAN %+0.2f   %02d:%02d",stance,c->lean,
        (s->config.max_ticks-s->tick)/3600,((s->config.max_ticks-s->tick)/60)%60),width/2-165,height-92,17,swat_paper);
    DrawText(TextFormat("%s / LEGS %.0f / ARMS %.0f",swat_kit(a->gear.kit)->name,a->gear.wounds[SWAT_LEGS],a->gear.wounds[SWAT_ARMS]),width/2-165,height-119,16,swat_gold);
    DrawText(TextFormat("4 FLASH %d   5 CS %d   T TASER %d%s",a->gear.flashbangs,a->gear.gas_grenades,a->gear.taser_charges,
        a->gear.taser_cooldown ? " / CHARGING" : ""),width/2-225,height-146,16,swat_gold);
    int mounted=0;
    for(int i=0;i<s->world.count;i++) if(s->world.objects[i].active && s->world.objects[i].breach_owner==view->actor) mounted++;
    DrawText(TextFormat("L PICKS   7 CHARGE %d   K DETONATE %d",a->gear.breaching_charges,mounted),width/2-225,height-170,16,swat_gold);
    DrawRectangle(0,height-52,width,52,(Color){9,15,21,255});
    DrawText("WASD move   Q / E lean   Ctrl / C crouch   Shift sprint   F use / comply   RMB context / aim   Z aim   LMB fire",28,height-40,16,(Color){162,177,181,255});
    DrawText("G wand   B melee   4 / 5 grenades   T taser   P plan   N camera   Comma / Period feeds   Enter control   Hold Tab cursor   R reload   Esc pause",28,height-21,15,(Color){126,144,151,255});
    if(show_camera) {
        SwatCameraLayout layout=swat_camera_layout(width,height,view->camera_expansion);
        DrawRectangle(0,0,width,height,(Color){0,0,0,(unsigned char)(155*view->camera_expansion)});
        DrawRectangleRec((Rectangle){layout.panel.x+6,layout.panel.y+8,layout.panel.width,layout.panel.height},(Color){0,0,0,130});
        swat_draw_panel(view,layout.panel);
        swat_draw_ui_icon(view,0,(Rectangle){layout.panel.x+7,layout.panel.y+6,28,28});
        if(view->camera_target.id) DrawTexturePro(view->camera_target.texture,
            (Rectangle){0,0,view->camera_target.texture.width,-view->camera_target.texture.height},layout.feed,(Vector2){0},0,WHITE);
    }
}

void swat_view_close(SwatView* view) {
    if (view->initialized && IsWindowReady()) {
        if(view->camera_target.id) UnloadRenderTexture(view->camera_target);
        if(view->panel_skin.id) UnloadTexture(view->panel_skin);
        if(view->ui_icons.id) UnloadTexture(view->ui_icons);
        CloseWindow();
    }
    memset(view,0,sizeof(*view));
}
