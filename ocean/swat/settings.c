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

// Raylib 5.5 KeyboardKey/MouseButton values. This file stays independent of
// raylib.h; tests/test_frontend_input.c checks the defaults against raylib.
#define SWAT_MOUSE(button) (SWAT_BIND_MOUSE+(button))
typedef struct SwatBindDef { const char* key; const char* label; int code[SWAT_BIND_SLOTS]; } SwatBindDef;
static const SwatBindDef swat_bind_defs[SWAT_BIND_ACTIONS]={
    [SWAT_BIND_FORWARD]={"move_forward","Move forward",{87}},
    [SWAT_BIND_BACK]={"move_back","Move back",{83}},
    [SWAT_BIND_LEFT]={"move_left","Move left",{65}},
    [SWAT_BIND_RIGHT]={"move_right","Move right",{68}},
    [SWAT_BIND_LEAN_LEFT]={"lean_left","Lean left",{81}},
    [SWAT_BIND_LEAN_RIGHT]={"lean_right","Lean right",{69}},
    [SWAT_BIND_CROUCH]={"crouch","Crouch",{341,67}},
    [SWAT_BIND_SPRINT]={"sprint","Sprint / queue order",{340}},
    [SWAT_BIND_MODIFIER]={"modifier","Slow / peek / modifier",{342}},
    [SWAT_BIND_JUMP]={"jump","Jump",{32}},
    [SWAT_BIND_FIRE]={"fire","Fire / use gadget",{SWAT_MOUSE(0)}},
    [SWAT_BIND_AIM_CONTEXT]={"aim_context","Aim / cuff / pick",{SWAT_MOUSE(1)}},
    [SWAT_BIND_AIM]={"aim","Aim only",{90}},
    [SWAT_BIND_USE]={"use","Use",{70,SWAT_MOUSE(2)}},
    [SWAT_BIND_RELOAD]={"reload","Reload",{82}},
    [SWAT_BIND_CANCEL_RELOAD]={"cancel_reload","Cancel reload (+mod)",{92}},
    [SWAT_BIND_PRIMARY]={"primary","Primary weapon",{49}},
    [SWAT_BIND_SIDEARM]={"sidearm","Sidearm",{50}},
    [SWAT_BIND_FLASHBANG]={"flashbang","Flashbang",{52}},
    [SWAT_BIND_GAS]={"gas","CS gas",{53}},
    [SWAT_BIND_PEPPER_SPRAY]={"pepper_spray","Pepper spray",{54}},
    [SWAT_BIND_PLACE_CHARGE]={"place_charge","Place charge",{55}},
    [SWAT_BIND_DISARM]={"disarm","Disarm kit",{56}},
    [SWAT_BIND_WEDGE]={"wedge","Wedge (+mod removes)",{57}},
    [SWAT_BIND_DETONATE]={"detonate","Detonate charge",{75}},
    [SWAT_BIND_LOCKPICK]={"lockpick","Lockpick",{76}},
    [SWAT_BIND_TASER]={"taser","Taser",{84}},
    [SWAT_BIND_INSPECT]={"inspect","Inspect",{71}},
    [SWAT_BIND_COMMAND]={"command","Command",{89}},
    [SWAT_BIND_MELEE]={"melee","Melee",{66}},
    [SWAT_BIND_SELECTOR]={"selector","Fire selector",{86}},
    [SWAT_BIND_LOW_READY]={"low_ready","Low ready",{268}},
    [SWAT_BIND_HIGH_READY]={"high_ready","High ready",{269}},
    [SWAT_BIND_SQUAD_MENU]={"squad_menu","Squad orders (hold)",{77}},
    [SWAT_BIND_SQUAD_EXECUTE]={"squad_execute","Squad execute",{74}},
    [SWAT_BIND_SNIPER_EXECUTE]={"sniper_execute","Snipers: execute",{88}},
    [SWAT_BIND_SNIPER_HOLD]={"sniper_hold","Snipers: clear marks",{72}},
    [SWAT_BIND_CAMERA]={"camera","Camera feed",{78}},
    [SWAT_BIND_PLANNING]={"planning","Planning",{80}},
};

typedef struct SwatKeyName { int code; const char* name; } SwatKeyName;
static const SwatKeyName swat_key_names[]={
    {65,"A"},{66,"B"},{67,"C"},{68,"D"},{69,"E"},{70,"F"},{71,"G"},{72,"H"},{73,"I"},
    {74,"J"},{75,"K"},{76,"L"},{77,"M"},{78,"N"},{79,"O"},{80,"P"},{81,"Q"},{82,"R"},
    {83,"S"},{84,"T"},{85,"U"},{86,"V"},{87,"W"},{88,"X"},{89,"Y"},{90,"Z"},
    {48,"0"},{49,"1"},{50,"2"},{51,"3"},{52,"4"},{53,"5"},{54,"6"},{55,"7"},{56,"8"},{57,"9"},
    {32,"Space"},{39,"Apostrophe"},{44,"Comma"},{45,"Minus"},{46,"Period"},{47,"Slash"},
    {59,"Semicolon"},{61,"Equal"},{91,"Left Bracket"},{92,"Backslash"},{93,"Right Bracket"},
    {96,"Grave"},{256,"Escape"},{257,"Enter"},{258,"Tab"},{259,"Backspace"},{260,"Insert"},
    {261,"Delete"},{262,"Right Arrow"},{263,"Left Arrow"},{264,"Down Arrow"},{265,"Up Arrow"},
    {266,"Page Up"},{267,"Page Down"},{268,"Home"},{269,"End"},{280,"Caps Lock"},
    {281,"Scroll Lock"},{282,"Num Lock"},{283,"Print Screen"},{284,"Pause"},
    {290,"F1"},{291,"F2"},{292,"F3"},{293,"F4"},{294,"F5"},{295,"F6"},{296,"F7"},{297,"F8"},
    {298,"F9"},{299,"F10"},{300,"F11"},{301,"F12"},
    {320,"Keypad 0"},{321,"Keypad 1"},{322,"Keypad 2"},{323,"Keypad 3"},{324,"Keypad 4"},
    {325,"Keypad 5"},{326,"Keypad 6"},{327,"Keypad 7"},{328,"Keypad 8"},{329,"Keypad 9"},
    {330,"Keypad Decimal"},{331,"Keypad Divide"},{332,"Keypad Multiply"},{333,"Keypad Subtract"},
    {334,"Keypad Add"},{335,"Keypad Enter"},{336,"Keypad Equal"},
    {340,"Shift"},{341,"Ctrl"},{342,"Alt"},{343,"Super"},{344,"Right Shift"},
    {345,"Right Ctrl"},{346,"Right Alt"},{347,"Right Super"},{348,"Menu"},
    {SWAT_MOUSE(0),"LMB"},{SWAT_MOUSE(1),"RMB"},{SWAT_MOUSE(2),"MMB"},{SWAT_MOUSE(3),"Mouse 4"},
    {SWAT_MOUSE(4),"Mouse 5"},{SWAT_MOUSE(5),"Mouse 6"},{SWAT_MOUSE(6),"Mouse 7"},
};

const char* swat_bind_action_label(int action) {
    return action>=0 && action<SWAT_BIND_ACTIONS ? swat_bind_defs[action].label : NULL;
}
const char* swat_bind_action_key(int action) {
    return action>=0 && action<SWAT_BIND_ACTIONS ? swat_bind_defs[action].key : NULL;
}
const char* swat_bind_code_name(int code) {
    for(size_t i=0;i<sizeof(swat_key_names)/sizeof(swat_key_names[0]);i++)
        if(swat_key_names[i].code==code) return swat_key_names[i].name;
    return NULL;
}
static bool swat_name_equal(const char* a, const char* b) {
    for(;*a && *b;a++,b++) if(tolower((unsigned char)*a)!=tolower((unsigned char)*b)) return false;
    return *a==*b;
}
int swat_bind_code_parse(const char* name) {
    if(swat_name_equal(name,"none")) return SWAT_BIND_NONE;
    for(size_t i=0;i<sizeof(swat_key_names)/sizeof(swat_key_names[0]);i++)
        if(swat_name_equal(swat_key_names[i].name,name)) return swat_key_names[i].code;
    return -1;
}
bool swat_bind_code_reserved(int code) {
    // Escape/Tab: pause and camera pointer. Enter: camera takeover. Backspace:
    // restart. Delete clears a slot while rebinding. F3 debug, F5/F9 save/load.
    return code==256 || code==257 || code==258 || code==259 || code==261 ||
        code==292 || code==294 || code==298;
}
bool swat_bind_code_valid(int code) {
    return code!=SWAT_BIND_NONE && swat_bind_code_name(code) && !swat_bind_code_reserved(code);
}
void swat_settings_reset_controls(SwatSettings* s) {
    s->crouch_toggle=s->aim_toggle=false;
    for(int a=0;a<SWAT_BIND_ACTIONS;a++)
        for(int slot=0;slot<SWAT_BIND_SLOTS;slot++) s->bindings[a][slot]=swat_bind_defs[a].code[slot];
}
int swat_bindings_conflict(const SwatSettings* s, int action, int slot) {
    if(action<0 || action>=SWAT_BIND_ACTIONS || slot<0 || slot>=SWAT_BIND_SLOTS) return -1;
    int code=s->bindings[action][slot];
    if(code==SWAT_BIND_NONE) return -1;
    for(int a=0;a<SWAT_BIND_ACTIONS;a++) {
        if(a==action) continue;
        for(int k=0;k<SWAT_BIND_SLOTS;k++) if(s->bindings[a][k]==code) return a;
    }
    return -1;
}
bool swat_bindings_first_conflict(const SwatSettings* s, int* action, int* other, int* code) {
    for(int a=0;a<SWAT_BIND_ACTIONS;a++) for(int slot=0;slot<SWAT_BIND_SLOTS;slot++) {
        int b=swat_bindings_conflict(s,a,slot);
        if(b<0) continue;
        if(action) *action=a;
        if(other) *other=b;
        if(code) *code=s->bindings[a][slot];
        return true;
    }
    return false;
}

SwatSettings swat_settings_defaults(void) {
    SwatSettings s={.sensitivity=0.035f,.vertical_multiplier=1.0f,.ads_multiplier=0.65f,
        .vertical_fov=70.0f,.frame_limit=120,.invert_x=false,.invert_y=false,.master_volume=1.0f,
        .weapon_size=1.7f,.weapon_horizontal=-.055f,.weapon_vertical=.075f,.weapon_ads_relief=.12f};
    swat_settings_reset_controls(&s);
    return s;
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
    s->weapon_ads_relief=swat_setting_range(s->weapon_ads_relief,defaults.weapon_ads_relief,.08f,.22f);
    for(int a=0;a<SWAT_BIND_ACTIONS;a++) {
        int* pair=s->bindings[a];
        for(int slot=0;slot<SWAT_BIND_SLOTS;slot++)
            if(pair[slot]!=SWAT_BIND_NONE && !swat_bind_code_valid(pair[slot])) pair[slot]=SWAT_BIND_NONE;
        if(pair[1]==pair[0]) pair[1]=SWAT_BIND_NONE; // One input twice adds nothing.
    }
}

bool swat_settings_equal(const SwatSettings* a, const SwatSettings* b) {
    return a->sensitivity==b->sensitivity && a->vertical_multiplier==b->vertical_multiplier &&
        a->ads_multiplier==b->ads_multiplier && a->vertical_fov==b->vertical_fov &&
        a->frame_limit==b->frame_limit && a->invert_x==b->invert_x && a->invert_y==b->invert_y &&
        a->master_volume==b->master_volume && a->weapon_size==b->weapon_size &&
        a->weapon_horizontal==b->weapon_horizontal && a->weapon_vertical==b->weapon_vertical &&
        a->weapon_ads_relief==b->weapon_ads_relief && a->crouch_toggle==b->crouch_toggle &&
        a->aim_toggle==b->aim_toggle && !memcmp(a->bindings,b->bindings,sizeof(a->bindings));
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

// "W", "Ctrl, C" or "none". Any unknown or reserved name rejects the whole
// line so that the action keeps its default instead of a half-parsed pair.
static bool swat_parse_binding(SwatSettings* s, const char* action, char* value) {
    int index=-1;
    for(int a=0;a<SWAT_BIND_ACTIONS;a++) if(!strcmp(action,swat_bind_defs[a].key)) index=a;
    if(index<0) return false;
    char* comment=strpbrk(value,"#;");
    if(comment) *comment='\0';
    int codes[SWAT_BIND_SLOTS]={SWAT_BIND_NONE,SWAT_BIND_NONE};
    int count=0;
    for(char* token=value;;) {
        char* comma=strchr(token,',');
        if(comma) *comma='\0';
        if(count>=SWAT_BIND_SLOTS) return false;
        char* name=swat_trim(token);
        int code=name[0] ? swat_bind_code_parse(name) : SWAT_BIND_NONE;
        if(code<0 || (code!=SWAT_BIND_NONE && !swat_bind_code_valid(code))) return false;
        codes[count++]=code;
        if(!comma) break;
        token=comma+1;
    }
    for(int slot=0;slot<SWAT_BIND_SLOTS;slot++) s->bindings[index][slot]=codes[slot];
    return true;
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
        if(!strncmp(key,"bind_",5)) { (void)swat_parse_binding(&parsed,key+5,value); continue; }
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
        else if(!strcmp(key,"weapon_ads_relief")) parsed.weapon_ads_relief=number;
        else if(!strcmp(key,"frame_limit")) parsed.frame_limit=(int)fmaxf(30,fminf(240,number));
        else if(!strcmp(key,"invert_x") && (number==0 || number==1)) parsed.invert_x=number!=0;
        else if(!strcmp(key,"invert_y") && (number==0 || number==1)) parsed.invert_y=number!=0;
        else if(!strcmp(key,"crouch_toggle") && (number==0 || number==1)) parsed.crouch_toggle=number!=0;
        else if(!strcmp(key,"aim_toggle") && (number==0 || number==1)) parsed.aim_toggle=number!=0;
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
        "weapon_size = %.9g\nweapon_horizontal = %.9g\nweapon_vertical = %.9g\nweapon_ads_relief = %.9g\n",
        (double)s.sensitivity,(double)s.vertical_multiplier,(double)s.ads_multiplier,
        (double)s.vertical_fov,s.frame_limit,s.invert_x,s.invert_y,(double)s.master_volume,
        (double)s.weapon_size,(double)s.weapon_horizontal,(double)s.weapon_vertical,(double)s.weapon_ads_relief)>0;
    if(fprintf(file,"crouch_toggle = %d\naim_toggle = %d\n"
        "# Controls: up to two keys or mouse buttons each, e.g. bind_crouch = Ctrl, C\n",
        s.crouch_toggle,s.aim_toggle)<0) ok=false;
    for(int a=0;a<SWAT_BIND_ACTIONS;a++) {
        const int* pair=s.bindings[a];
        const char* first=pair[0] ? swat_bind_code_name(pair[0]) : "none";
        int written=pair[1] ?
            fprintf(file,"bind_%s = %s, %s\n",swat_bind_defs[a].key,first,swat_bind_code_name(pair[1])) :
            fprintf(file,"bind_%s = %s\n",swat_bind_defs[a].key,first);
        if(written<0) ok=false;
    }
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
