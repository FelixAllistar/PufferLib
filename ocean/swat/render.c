#include "render.h"
#include "rlgl.h"
#include <string.h>

static Vector3 swat_vector(b3Vec3 v) { return (Vector3){v.x,v.y,v.z}; }
static Vector3 swat_position(b3Pos p) { return (Vector3){(float)p.x,(float)p.y,(float)p.z}; }
static const Color swat_gold = {226,181,82,255};
static const Color swat_ink = {12,19,26,235};
static const Color swat_paper = {224,231,229,255};

void swat_view_init(SwatView* view, bool hidden) {
    if (view->initialized) return;
    SetConfigFlags(FLAG_MSAA_4X_HINT | (hidden ? FLAG_WINDOW_HIDDEN : 0));
    view->width = 1440; view->height = 810;
    InitWindow(view->width,view->height,"SWAT: Gold Element");
    if (!IsWindowReady()) return;
    SetTargetFPS(60);
    view->initialized = true;
    EnableCursor(); // Capture only after entering the game with window focus.
}

static Color swat_material_color(const SwatObject* o) {
    const Color colors[] = {{66,76,81,255},{157,160,144,255},{130,94,53,255},
                            {99,181,190,135},{85,101,114,255}};
    Color color = colors[o->material];
    if (o->max_health > 0) {
        float ratio = 0.5f+0.5f*o->health/o->max_health;
        color.r = (unsigned char)(color.r*ratio);
        color.g = (unsigned char)(color.g*ratio);
        color.b = (unsigned char)(color.b*ratio);
    }
    if (o->center.y < 0) color = (Color){36,45,50,255};
    return color;
}

static void swat_draw_object(const SwatObject* o) {
    if (!o->active) return;
    rlPushMatrix();
    rlTranslatef((float)o->center.x,(float)o->center.y,(float)o->center.z);
    rlRotatef(o->yaw/SWAT_RAD,0,1,0);
    Vector3 size = {o->half.x*2,o->half.y*2,o->half.z*2};
    DrawCubeV((Vector3){0},size,swat_material_color(o));
    DrawCubeWiresV((Vector3){0},size,(Color){22,31,36,130});
    if (o->door) {
        DrawCube((Vector3){-o->half.x-0.015f,0.06f,o->half.z-0.17f},0.05f,0.055f,0.22f,swat_gold);
        DrawCube((Vector3){o->half.x+0.015f,0.06f,o->half.z-0.17f},0.05f,0.055f,0.22f,swat_gold);
    }
    rlPopMatrix();
}

static void swat_draw_actor(const SwatActor* a) {
    b3Pos feet = swat_body_feet_position(&a->controller.body);
    float height = a->controller.body.totalHeight;
    Color uniform = a->role == SWAT_CIVILIAN ? (Color){68,123,153,255} : (Color){125,58,52,255};
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
    if (a->role == SWAT_SUSPECT) {
        b3Pos eye = swat_controller_eye(&a->controller);
        b3Vec3 direction = swat_controller_aim(&a->controller);
        b3Pos start = b3OffsetPos(eye,swat_v(0,-0.24f,0));
        DrawCylinderEx(swat_position(start),swat_position(b3OffsetPos(start,swat_mul(direction,0.65f))),0.045f,0.035f,6,(Color){26,29,31,255});
    }
}

static void swat_draw_weapon(const SwatSim* s) {
    const SwatActor* a = &s->actors[0];
    const SwatController* c = &a->controller;
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
    const SwatActor* a=&s->actors[0];
    const SwatController* c=&a->controller;
    const SwatWeapon* w=&a->arsenal.slots[a->arsenal.active];
    b3Vec3 forward,right,up;
    swat_controller_view(c,&forward,&right,&up);
    b3Pos eye=swat_controller_eye(c);
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
    for (int i=-3;i<24;i++) DrawLine3D((Vector3){(float)i,0.006f,-6.8f},(Vector3){(float)i,0.006f,6.8f},(Color){64,73,73,90});
    for (int i=-6;i<=6;i++) DrawLine3D((Vector3){-3.8f,0.006f,(float)i},(Vector3){23.8f,0.006f,(float)i},(Color){64,73,73,90});
    for (int i=1;i<s->actor_count;i++) swat_draw_actor(&s->actors[i]);
    for (int i=0;i<s->world.count;i++) if (s->world.objects[i].material == SWAT_GLASS)
        swat_draw_object(&s->world.objects[i]);
    Color extraction = swat_sim_hostiles(s) == 0 ? (Color){99,197,144,255} : (Color){177,146,77,180};
    DrawCylinder((Vector3){(float)s->extraction.x,0.01f,(float)s->extraction.z},1.3f,1.3f,0.025f,32,extraction);
    for (int i=0;i<s->actor_count;i++) if (s->tick-s->actors[i].last_shot_tick <= 3)
        DrawLine3D(swat_position(s->actors[i].tracer_start),swat_position(s->actors[i].tracer_end),(Color){250,200,98,180});
    if (a->alive) swat_draw_weapon(s);
    EndMode3D();

    DrawRectangle(0,0,width,96,swat_ink);
    DrawRectangle(26,25,4,45,swat_gold);
    DrawText("SWAT",44,20,32,swat_paper);
    DrawText("G O L D  E L E M E N T",46,60,16,swat_gold);
    DrawText("01 / TRAINING ANNEX",width-310,24,21,swat_paper);
    DrawText(policy ? "POLICY CONTROL" : "TACTICAL CONTROLLER PROTOTYPE",width-310,57,15,swat_gold);
    DrawRectangle(24,118,370,72,swat_ink);
    DrawText(swat_sim_hostiles(s) ? "NEUTRALIZE THE ARMED THREAT" : "MOVE TO THE EXTRACTION MARKER",40,130,17,swat_paper);
    DrawText("Protect the unarmed civilian",40,159,16,(Color){160,178,183,255});

    Color reticle = c->muzzle_blocked ? (Color){230,110,80,255} : swat_gold;
    int cx=width/2, cy=height/2;
    int gap=3+(int)((1-c->ads)*5);
    DrawLine(cx-gap-8,cy,cx-gap,cy,reticle); DrawLine(cx+gap,cy,cx+gap+8,cy,reticle);
    DrawLine(cx,cy-gap-8,cx,cy-gap,reticle); DrawLine(cx,cy+gap,cx,cy+gap+8,reticle);
    if (c->muzzle_blocked) DrawText("MUZZLE OBSTRUCTED",cx-95,cy+30,16,reticle);
    SwatHit focus=swat_world_ray(&s->world,eye,forward,2.2f,c->body.body);
    if (focus.kind==SWAT_HIT_WORLD && focus.index>=0 && s->world.objects[focus.index].door)
        DrawText(s->world.objects[focus.index].door_open ? "F  CLOSE DOOR" : "F  OPEN DOOR",cx-70,cy+60,18,swat_paper);

    DrawRectangle(24,height-158,288,86,swat_ink);
    DrawText("GOLD 01",40,height-145,18,swat_gold);
    DrawText(TextFormat("HEALTH  %03d",(int)a->health),40,height-115,23,swat_paper);
    DrawRectangle(40,height-85,248,4,(Color){50,66,69,255});
    DrawRectangle(40,height-85,(int)(248*c->stamina),4,swat_gold);
    DrawRectangle(width-340,height-176,316,104,swat_ink);
    DrawText(swat_weapon_def(a->arsenal.active)->name,width-320,height-164,19,swat_gold);
    const char* modes[]={"SAFE","SEMI","AUTO"};
    DrawText(TextFormat("%02d+%d  /  %03d",w->magazine,w->chambered,w->reserve),width-320,height-133,30,swat_paper);
    DrawText(w->reload_remaining ? "RELOADING" : (a->arsenal.equip_remaining ? "EQUIPPING" : modes[w->mode]),width-320,height-94,16,swat_gold);
    const char* stance=c->sprinting ? "SPRINT" : (c->body.crouched ? "CROUCH" : "STAND");
    DrawText(TextFormat("%s   LEAN %+0.2f   %02d:%02d",stance,c->lean,
        (s->config.max_ticks-s->tick)/3600,((s->config.max_ticks-s->tick)/60)%60),width/2-165,height-92,17,swat_paper);
    DrawRectangle(0,height-52,width,52,(Color){9,15,21,255});
    DrawText("WASD move   Q / E lean   Ctrl crouch   Shift sprint   RMB aim   LMB fire",28,height-40,16,(Color){162,177,181,255});
    DrawText("R reload   1 / 2 weapon   V selector   F door   Backspace restart   Esc pause / settings",28,height-21,15,(Color){126,144,151,255});
}

void swat_view_close(SwatView* view) {
    if (view->initialized && IsWindowReady()) CloseWindow();
    memset(view,0,sizeof(*view));
}
