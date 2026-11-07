#include "settings.h"
#include "swat_math.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void write_file(const char* path, const char* contents) {
    FILE* file=fopen(path,"w"); assert(file);
    assert(fputs(contents,file)>=0);
    assert(fclose(file)==0);
}

int main(int argc, char** argv) {
    assert(argc==2); // A test-owned path, never the actual user's settings.
    const char* path=argv[1];
    SwatSettings original=swat_settings_defaults(),loaded;
    original.sensitivity=0.017f; original.vertical_multiplier=1.3f;
    original.ads_multiplier=0.4f; original.vertical_fov=83;
    original.frame_limit=144; original.invert_y=true;
    original.master_volume=.6f;
    original.weapon_size=2.1f; original.weapon_horizontal=-.03f; original.weapon_vertical=.09f;
    original.weapon_ads_relief=.16f;
    assert(swat_settings_save(&original,path));
    assert(swat_settings_load(&loaded,path));
    assert(swat_settings_equal(&original,&loaded));
    // Replace an existing file, including on Windows where rename() alone
    // cannot replace an existing destination.
    original.invert_x=true; original.sensitivity=0.06f;
    assert(swat_settings_save(&original,path));
    assert(swat_settings_load(&loaded,path) && swat_settings_equal(&original,&loaded));
    char blocked[1200]; snprintf(blocked,sizeof(blocked),"%s/child.ini",path);
    assert(!swat_settings_save(&original,blocked));
    assert(swat_settings_load(&loaded,path) && swat_settings_equal(&original,&loaded));

    write_file(path,"version=1\nmouse_sensitivity=nan\nvertical_multiplier=9\n"
        "ads_multiplier=-2\nvertical_fov=500\nframe_limit=inf\ninvert_y=1\n"
        "invert_x=garbage\nunknown_key=42\nweapon_size=500\nweapon_horizontal=-9\nweapon_vertical=nan\nweapon_ads_relief=-9\n");
    assert(swat_settings_load(&loaded,path));
    assert(loaded.sensitivity==swat_settings_defaults().sensitivity);
    assert(loaded.vertical_multiplier==2 && loaded.ads_multiplier==0.1f);
    assert(loaded.vertical_fov==100 && loaded.frame_limit==120 && loaded.invert_y && !loaded.invert_x);
    assert(loaded.weapon_size==2.4f && loaded.weapon_horizontal==-.10f && loaded.weapon_vertical==.075f);
    assert(loaded.weapon_ads_relief==.08f);
    write_file(path,"version=1\nvertical_fov=75\n");
    assert(swat_settings_load(&loaded,path));
    assert(loaded.weapon_size==swat_settings_defaults().weapon_size && loaded.weapon_horizontal==-.055f);
    assert(loaded.weapon_ads_relief==.12f);
    write_file(path,"version=99\nmouse_sensitivity=.1\n");
    assert(!swat_settings_load(&loaded,path) && errno==EINVAL);
    original=swat_settings_defaults();
    assert(swat_settings_equal(&loaded,&original));
    assert(remove(path)==0);
    assert(!swat_settings_load(&loaded,path) && errno==ENOENT);
    assert(swat_settings_equal(&loaded,&original));

    // Controls: defaults are complete and unique; a saved table round-trips.
    SwatSettings controls=swat_settings_defaults();
    assert(!swat_bindings_first_conflict(&controls,NULL,NULL,NULL));
    for(int a=0;a<SWAT_BIND_ACTIONS;a++) {
        assert(swat_bind_action_label(a) && swat_bind_action_key(a));
        assert(swat_bind_code_valid(controls.bindings[a][0]));
        assert(!controls.bindings[a][1] || swat_bind_code_valid(controls.bindings[a][1]));
    }
    assert(!swat_bind_action_label(SWAT_BIND_ACTIONS) && !swat_bind_action_label(-1));
    int forward=controls.bindings[SWAT_BIND_FORWARD][0],detonate=controls.bindings[SWAT_BIND_DETONATE][0];
    controls.bindings[SWAT_BIND_FORWARD][0]=swat_bind_code_parse("Up Arrow");
    controls.bindings[SWAT_BIND_FORWARD][1]=forward;
    controls.bindings[SWAT_BIND_CROUCH][0]=swat_bind_code_parse("right ctrl");
    controls.bindings[SWAT_BIND_FIRE][0]=SWAT_BIND_MOUSE+3;
    controls.bindings[SWAT_BIND_TASER][0]=SWAT_BIND_NONE;
    controls.bindings[SWAT_BIND_MELEE][1]=swat_bind_code_parse("Keypad 0");
    controls.crouch_toggle=true; controls.aim_toggle=true; controls.sensitivity=.05f;
    assert(swat_bind_code_parse("Up Arrow")>0 && swat_bind_code_parse("RIGHT CTRL")>0);
    assert(swat_settings_save(&controls,path));
    assert(swat_settings_load(&loaded,path) && swat_settings_equal(&controls,&loaded));
    assert(loaded.crouch_toggle && loaded.aim_toggle && !loaded.bindings[SWAT_BIND_TASER][0]);
    {
        FILE* file=fopen(path,"r"); assert(file);
        char text[8192]; size_t n=fread(text,1,sizeof(text)-1,file); text[n]='\0'; assert(fclose(file)==0);
        assert(strstr(text,"version = 1\n") && strstr(text,"crouch_toggle = 1\n"));
        assert(strstr(text,"bind_move_forward = Up Arrow, W\n") && strstr(text,"bind_crouch = Right Ctrl, C\n"));
        assert(strstr(text,"bind_fire = Mouse 4\n") && strstr(text,"bind_taser = none\n"));
    }
    // Validation: unknown names, reserved keys and over-long lists keep the
    // default; "none", case, comments and duplicate slots are accepted.
    write_file(path,"version=1\nbind_move_forward = Banana\nbind_jump = Escape\nbind_reload = T, R, Y\n"
        "bind_taser = none\nbind_melee = left bracket\nbind_lean_left = Q, q\nbind_aim = Keypad 5 ; comment\n"
        "bind_unknown = W\nbind_use = , MMB\nbind_lockpick = Tab, L\ncrouch_toggle = 2\naim_toggle = 1\n");
    assert(swat_settings_load(&loaded,path));
    SwatSettings defaults=swat_settings_defaults();
    assert(loaded.bindings[SWAT_BIND_FORWARD][0]==defaults.bindings[SWAT_BIND_FORWARD][0]);
    assert(loaded.bindings[SWAT_BIND_JUMP][0]==defaults.bindings[SWAT_BIND_JUMP][0]);
    assert(loaded.bindings[SWAT_BIND_RELOAD][0]==defaults.bindings[SWAT_BIND_RELOAD][0]);
    assert(loaded.bindings[SWAT_BIND_LOCKPICK][0]==defaults.bindings[SWAT_BIND_LOCKPICK][0]);
    assert(!loaded.bindings[SWAT_BIND_TASER][0] && !loaded.bindings[SWAT_BIND_TASER][1]);
    assert(loaded.bindings[SWAT_BIND_MELEE][0]==swat_bind_code_parse("Left Bracket") && !loaded.bindings[SWAT_BIND_MELEE][1]);
    assert(loaded.bindings[SWAT_BIND_LEAN_LEFT][0]==swat_bind_code_parse("Q") && !loaded.bindings[SWAT_BIND_LEAN_LEFT][1]);
    assert(loaded.bindings[SWAT_BIND_AIM][0]==swat_bind_code_parse("Keypad 5"));
    assert(!loaded.bindings[SWAT_BIND_USE][0] && loaded.bindings[SWAT_BIND_USE][1]==SWAT_BIND_MOUSE+2);
    assert(!loaded.crouch_toggle && loaded.aim_toggle);
    // Files written before controls existed load every default binding.
    write_file(path,"version=1\nmouse_sensitivity=.02\n");
    assert(swat_settings_load(&loaded,path) && loaded.sensitivity==.02f && !loaded.crouch_toggle);
    assert(!memcmp(loaded.bindings,defaults.bindings,sizeof(defaults.bindings)));
    // Conflicts name both actions; unbound slots never conflict.
    controls=swat_settings_defaults();
    controls.bindings[SWAT_BIND_LOCKPICK][1]=detonate;
    assert(swat_bindings_conflict(&controls,SWAT_BIND_LOCKPICK,1)==SWAT_BIND_DETONATE);
    assert(swat_bindings_conflict(&controls,SWAT_BIND_DETONATE,0)==SWAT_BIND_LOCKPICK);
    assert(swat_bindings_conflict(&controls,SWAT_BIND_LOCKPICK,0)==-1);
    int first=-1,other=-1,code=0;
    assert(swat_bindings_first_conflict(&controls,&first,&other,&code));
    assert(first==SWAT_BIND_DETONATE && other==SWAT_BIND_LOCKPICK && code==detonate);
    controls.bindings[SWAT_BIND_TASER][0]=controls.bindings[SWAT_BIND_MELEE][1]=SWAT_BIND_NONE;
    assert(swat_bindings_conflict(&controls,SWAT_BIND_TASER,0)==-1);
    controls.bindings[SWAT_BIND_INSPECT][0]=256; // Escape, as a corrupted in-memory value.
    controls.sensitivity=.09f; swat_settings_sanitize(&controls);
    assert(!controls.bindings[SWAT_BIND_INSPECT][0]);
    swat_settings_reset_controls(&controls);
    assert(controls.sensitivity==.09f && !swat_bindings_first_conflict(&controls,NULL,NULL,NULL));
    assert(!memcmp(controls.bindings,defaults.bindings,sizeof(defaults.bindings)));
    assert(remove(path)==0);

    SwatLookDelta right_up=swat_settings_look(&original,100,-100,0);
    b3Vec3 facing=swat_direction(right_up.yaw,right_up.pitch);
    assert(facing.z>0 && facing.y>0); // +Z is the rendered camera's right at yaw=0.
    assert(right_up.yaw<0.1f); // Much lower default than the original 0.18 rad/100 px.
    SwatLookDelta still=swat_settings_look(&original,0,0,0);
    assert(still.yaw==0 && still.pitch==0);
    SwatLookDelta aimed=swat_settings_look(&original,100,-100,1);
    assert(fabsf(aimed.yaw/right_up.yaw-original.ads_multiplier)<1e-6f);
    float combined_yaw=0,combined_pitch=0;
    for(int frame=0;frame<100;frame++) {
        SwatLookDelta sample=swat_settings_look(&original,1,-1,0);
        combined_yaw+=sample.yaw; combined_pitch+=sample.pitch;
    }
    assert(fabsf(combined_yaw-right_up.yaw)<1e-6f && fabsf(combined_pitch-right_up.pitch)<1e-6f);
    original.invert_x=original.invert_y=true;
    SwatLookDelta inverse=swat_settings_look(&original,100,-100,0);
    assert(inverse.yaw==-right_up.yaw && inverse.pitch==-right_up.pitch);
    SwatLookDelta invalid=swat_settings_look(&original,INFINITY,NAN,0);
    assert(invalid.yaw==0 && invalid.pitch==0);
    puts("PASS settings: roundtrip, replacement, failed-save preservation, malformed values, version, missing file, mouse axes and frame independence");
    return 0;
}
