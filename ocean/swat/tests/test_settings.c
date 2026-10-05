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
        "invert_x=garbage\nunknown_key=42\nweapon_size=500\nweapon_horizontal=-9\nweapon_vertical=nan\n");
    assert(swat_settings_load(&loaded,path));
    assert(loaded.sensitivity==swat_settings_defaults().sensitivity);
    assert(loaded.vertical_multiplier==2 && loaded.ads_multiplier==0.1f);
    assert(loaded.vertical_fov==100 && loaded.frame_limit==120 && loaded.invert_y && !loaded.invert_x);
    assert(loaded.weapon_size==2.4f && loaded.weapon_horizontal==-.10f && loaded.weapon_vertical==.075f);
    write_file(path,"version=1\nvertical_fov=75\n");
    assert(swat_settings_load(&loaded,path));
    assert(loaded.weapon_size==swat_settings_defaults().weapon_size && loaded.weapon_horizontal==-.055f);
    write_file(path,"version=99\nmouse_sensitivity=.1\n");
    assert(!swat_settings_load(&loaded,path) && errno==EINVAL);
    original=swat_settings_defaults();
    assert(swat_settings_equal(&loaded,&original));
    assert(remove(path)==0);
    assert(!swat_settings_load(&loaded,path) && errno==ENOENT);
    assert(swat_settings_equal(&loaded,&original));

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
