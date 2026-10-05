#include "settings.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <process.h>
#define swat_getpid _getpid
#else
#include <unistd.h>
#define swat_getpid getpid
#endif

SwatSettings swat_settings_defaults(void) {
    return (SwatSettings){0.035f,1.0f,0.65f,70.0f,120,false,false,1.0f,1.7f,-.055f,.075f};
}

static float swat_setting_range(float value, float fallback, float lo, float hi) {
    return isfinite(value) ? fmaxf(lo,fminf(hi,value)) : fallback;
}

void swat_settings_sanitize(SwatSettings* s) {
    SwatSettings defaults=swat_settings_defaults();
    s->sensitivity=swat_setting_range(s->sensitivity,defaults.sensitivity,
                                    SWAT_SENSITIVITY_MIN,SWAT_SENSITIVITY_MAX);
    s->vertical_multiplier=swat_setting_range(s->vertical_multiplier,1,0.25f,2);
    s->ads_multiplier=swat_setting_range(s->ads_multiplier,defaults.ads_multiplier,0.1f,1.5f);
    s->vertical_fov=swat_setting_range(s->vertical_fov,70,55,100);
    if(s->frame_limit<30) s->frame_limit=30;
    if(s->frame_limit>240) s->frame_limit=240;
    s->master_volume=swat_setting_range(s->master_volume,1,0,1);
    s->weapon_size=swat_setting_range(s->weapon_size,defaults.weapon_size,1,2.4f);
    s->weapon_horizontal=swat_setting_range(s->weapon_horizontal,defaults.weapon_horizontal,-.10f,.10f);
    s->weapon_vertical=swat_setting_range(s->weapon_vertical,defaults.weapon_vertical,-.08f,.12f);
}

bool swat_settings_equal(const SwatSettings* a, const SwatSettings* b) {
    return a->sensitivity==b->sensitivity && a->vertical_multiplier==b->vertical_multiplier &&
        a->ads_multiplier==b->ads_multiplier && a->vertical_fov==b->vertical_fov &&
        a->frame_limit==b->frame_limit && a->invert_x==b->invert_x && a->invert_y==b->invert_y &&
        a->master_volume==b->master_volume && a->weapon_size==b->weapon_size &&
        a->weapon_horizontal==b->weapon_horizontal && a->weapon_vertical==b->weapon_vertical;
}

bool swat_settings_default_path(char* out, size_t capacity) {
    const char* base=NULL;
    const char* suffix=NULL;
#if defined(_WIN32)
    base=getenv("LOCALAPPDATA");
    if(!base || !base[0]) base=getenv("APPDATA");
    suffix="/SWAT Gold Element/settings.ini";
#else
    base=getenv("XDG_CONFIG_HOME");
    suffix="/swat-gold-element/settings.ini";
    if(!base || !base[0]) {
        base=getenv("HOME");
        suffix="/.config/swat-gold-element/settings.ini";
    }
#endif
    if(!base || !base[0]) { errno=ENOENT; return false; }
    int n=snprintf(out,capacity,"%s%s",base,suffix);
    if(n<0 || (size_t)n>=capacity) { errno=ENAMETOOLONG; return false; }
    return true;
}

static char* swat_trim(char* value) {
    while(isspace((unsigned char)*value)) value++;
    char* end=value+strlen(value);
    while(end>value && isspace((unsigned char)end[-1])) *--end='\0';
    return value;
}

bool swat_settings_load(SwatSettings* settings, const char* path) {
    *settings=swat_settings_defaults();
    FILE* file=fopen(path,"r");
    if(!file) return false;
    SwatSettings parsed=*settings;
    char line[256];
    bool supported=true;
    while(fgets(line,sizeof(line),file)) {
        char* key=swat_trim(line);
        if(!key[0] || key[0]=='#' || key[0]==';' || key[0]=='[') continue;
        char* equals=strchr(key,'=');
        if(!equals) continue;
        *equals='\0';
        key=swat_trim(key);
        char* value=swat_trim(equals+1);
        char* end=NULL;
        errno=0;
        float number=strtof(value,&end);
        if(end==value || errno || !isfinite(number)) continue;
        end=swat_trim(end);
        if(*end && *end!='#' && *end!=';') continue;
        if(!strcmp(key,"version")) supported=number==1;
        else if(!strcmp(key,"mouse_sensitivity")) parsed.sensitivity=number;
        else if(!strcmp(key,"vertical_multiplier")) parsed.vertical_multiplier=number;
        else if(!strcmp(key,"ads_multiplier")) parsed.ads_multiplier=number;
        else if(!strcmp(key,"vertical_fov")) parsed.vertical_fov=number;
        else if(!strcmp(key,"master_volume")) parsed.master_volume=number;
        else if(!strcmp(key,"weapon_size")) parsed.weapon_size=number;
        else if(!strcmp(key,"weapon_horizontal")) parsed.weapon_horizontal=number;
        else if(!strcmp(key,"weapon_vertical")) parsed.weapon_vertical=number;
        else if(!strcmp(key,"frame_limit")) parsed.frame_limit=(int)fmaxf(30,fminf(240,number));
        else if(!strcmp(key,"invert_x") && (number==0 || number==1)) parsed.invert_x=number!=0;
        else if(!strcmp(key,"invert_y") && (number==0 || number==1)) parsed.invert_y=number!=0;
    }
    bool failed=ferror(file)!=0;
    if(fclose(file)!=0) failed=true;
    if(failed || !supported) { errno=failed ? EIO : EINVAL; return false; }
    swat_settings_sanitize(&parsed);
    *settings=parsed;
    return true;
}

bool swat_settings_prepare_path(const char* path) {
    if(!path || !path[0]) { errno=EINVAL; return false; }
    char directory[SWAT_SETTINGS_PATH_SIZE];
    if(strlen(path)>=sizeof(directory)) { errno=ENAMETOOLONG; return false; }
    strcpy(directory,path);
    char* first=directory+1;
#if defined(_WIN32)
    if((directory[0]=='/' || directory[0]=='\\') &&
       (directory[1]=='/' || directory[1]=='\\')) {
        // Skip \\server\share; create only descendants of an existing share.
        first=directory+2;
        for(int part=0;part<2;part++) {
            while(*first && *first!='/' && *first!='\\') first++;
            if(!*first) return true;
            first++;
        }
    }
#endif
    for(char* p=first;*p;p++) {
        if(*p!='/' && *p!='\\') continue;
        // A Windows drive root already exists. UNC share roots are likewise
        // not directories that this application should attempt to create.
        if(p[-1]==':') continue;
        char slash=*p; *p='\0';
        struct stat info;
        if(stat(directory,&info)!=0) {
#if defined(_WIN32)
            int rc=_mkdir(directory);
#else
            int rc=mkdir(directory,0700);
#endif
            if(rc!=0 && errno!=EEXIST) { *p=slash; return false; }
        }
        *p=slash;
    }
    return true;
}

bool swat_settings_save(const SwatSettings* settings, const char* path) {
    if(!path || !path[0]) { errno=EINVAL; return false; }
    if(!swat_settings_prepare_path(path)) return false;
    SwatSettings s=*settings;
    swat_settings_sanitize(&s);
    char temporary[SWAT_SETTINGS_PATH_SIZE+32];
    int n=snprintf(temporary,sizeof(temporary),"%s.tmp.%ld",path,(long)swat_getpid());
    if(n<0 || (size_t)n>=sizeof(temporary)) { errno=ENAMETOOLONG; return false; }
    FILE* file=fopen(temporary,"w");
    if(!file) return false;
    bool ok=fprintf(file,
        "# SWAT: Gold Element player preferences\nversion = 1\n"
        "mouse_sensitivity = %.9g\nvertical_multiplier = %.9g\nads_multiplier = %.9g\n"
        "vertical_fov = %.9g\nframe_limit = %d\ninvert_x = %d\ninvert_y = %d\nmaster_volume = %.9g\n"
        "weapon_size = %.9g\nweapon_horizontal = %.9g\nweapon_vertical = %.9g\n",
        (double)s.sensitivity,(double)s.vertical_multiplier,(double)s.ads_multiplier,
        (double)s.vertical_fov,s.frame_limit,s.invert_x,s.invert_y,(double)s.master_volume,
        (double)s.weapon_size,(double)s.weapon_horizontal,(double)s.weapon_vertical)>0;
    if(fflush(file)!=0) ok=false;
    if(fclose(file)!=0) ok=false;
    if(ok) {
#if defined(_WIN32)
        ok=MoveFileExA(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
        if(!ok) errno=EIO;
#else
        ok=rename(temporary,path)==0;
#endif
    }
    if(!ok) { int error=errno ? errno : EIO; remove(temporary); errno=error; }
    return ok;
}

SwatLookDelta swat_settings_look(const SwatSettings* s, float x, float y, float ads) {
    if(!isfinite(x) || !isfinite(y)) return (SwatLookDelta){0};
    // Positive yaw is screen-right for the game's +X forward / +Y up basis.
    // Pixels are displacement, not velocity: never multiply them by frame time.
    float aimed=fmaxf(0,fminf(1,ads));
    float scale=s->sensitivity*(3.14159265358979323846f/180.0f)*
        (1.0f+(s->ads_multiplier-1.0f)*aimed);
    return (SwatLookDelta){x*scale*(s->invert_x ? -1 : 1),
        y*scale*s->vertical_multiplier*(s->invert_y ? 1 : -1)};
}
