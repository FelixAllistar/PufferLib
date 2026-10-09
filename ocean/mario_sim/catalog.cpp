// Read-only inventory from the locally supplied ROM. No level bytes are emitted.
#include "../retro/retro.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

static const char* enemy_name(int id) {
    static const char* names[64]={
        "green_koopa","koopa_variant","buzzy_beetle","red_koopa","unused_04","hammer_bro","goomba","blooper",
        "bullet_bill_frenzy","unused_09","grey_cheep_cheep","red_cheep_cheep","podoboo","piranha_plant","jumping_paratroopa","vertical_paratroopa",
        "horizontal_paratroopa","lakitu","spiny","unused_13","flying_cheep_cheep","bowser_flame","fireworks","bullet_or_fish_frenzy",
        "stop_frenzy","unused_19","unused_1a","short_firebar_slow_forward","short_firebar_fast_forward","short_firebar_slow_reverse","short_firebar_fast_reverse","long_firebar",
        "firebar_variant_20","firebar_variant_21","firebar_variant_22","unused_23","balance_platform","vertical_platform","large_lift_up","large_lift_down",
        "horizontal_platform","drop_platform","right_platform","small_lift_up","small_lift_down","bowser","powerup","vine",
        "flagpole_flag","castle_flag","jumpspring","bullet_bill_cannon","warp_zone","retainer","unused_36","enemy_group_37",
        "enemy_group_38","enemy_group_39","enemy_group_3a","enemy_group_3b","enemy_group_3c","enemy_group_3d","enemy_group_3e","unused_3f"};
    return names[id&63];
}
static const char* terrain_name(int id) {
    static const char* names[]={"warp_pipe","area_style","brick_row","solid_row","coin_row","brick_column","solid_column","pipe",
        "pit","pulley_rope","bridge_high","bridge_middle","bridge_low","water_hole","question_row_high","question_row_low",
        "endless_rope","balance_rope","castle","stairs","exit_pipe","residual_flag_balls",
        "question_powerup","question_coin","hidden_coin","hidden_1up","brick_powerup","brick_vine","brick_star","multi_coin_brick","brick_1up","water_pipe","empty_block","jumpspring",
        "intro_pipe","flagpole","axe","chain","castle_bridge","warp_zone","scroll_lock","scroll_lock_variant","flying_fish_frenzy","bullet_or_fish_frenzy","stop_frenzy","maze_loop","area_attribute_change"};
    return id>=0&&id<47?names[id]:"unsupported_area_object";
}
struct Entry {int x,row,id,hard;};
struct Area {int pointer,type,terrain_ptr,enemy_ptr;std::vector<Entry> enemies,terrain;std::set<int> destinations;std::vector<std::string> stages;};
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("usage: catalog OUTPUT.json");
        RetroRom& rom=retro_rom();retro_load_rom_locked(rom,RETRO_SMB1_ROM_PATH);
        const auto* prg=rom.cart.prg();auto read=[&](int addr){if(addr<0x8000||addr>0xffff)throw std::runtime_error("ROM pointer out of bounds");return (int)prg[addr-0x8000];};
        std::map<int,Area> areas;const int count[4]={3,22,3,6};
        for(int type=0;type<4;type++)for(int index=0;index<count[type];index++) {
            Area a={};a.pointer=type*32+index;a.type=type;
            int eo=read(0x9ce0+type)+index,ao=read(RETRO_AREA_DATA_OFFSETS+type)+index;
            a.enemy_ptr=read(0x9ce4+eo)+256*read(0x9d06+eo);
            a.terrain_ptr=read(RETRO_AREA_DATA_LOW+ao)+256*read(RETRO_AREA_DATA_HIGH+ao);
            int page=0;
            for(int off=0;off<256;) {
                int b0=read(a.enemy_ptr+off);if(b0==255)break;
                int b1=read(a.enemy_ptr+off+1),row=b0&15;if(b1&128)page++;
                if(row==15&&!(b1&128)){page=b1&63;off+=2;continue;}
                if(row==14){a.destinations.insert(b1&127);off+=3;continue;}
                a.enemies.push_back({page*256+(b0&240),row,b1&63,(b1>>6)&1});off+=2;
            }
            page=0;
            for(int off=2;off<258;off+=2) {
                int b0=read(a.terrain_ptr+off);if(b0==253)break;
                int b1=read(a.terrain_ptr+off+1),row=b0&15;if(b1&128)page++;
                if(row==13&&!(b1&64)){if(!(b1&128))page=b1&31;continue;}
                int id;
                if(row==14)id=46;
                else if(row==13)id=34+(b1&63);
                else if(row==12)id=8+((b1>>4)&7);
                else if(row==15)id=16+((b1>>4)&7);
                else if(!(b1&112))id=22+(b1&15);
                else id=(b1&112)==112&&(b1&8)?0:(b1>>4)&7;
                a.terrain.push_back({page*256+(b0&240),row,id,0});
            }
            areas.emplace(a.pointer,a);
        }
        for(int w=1;w<=8;w++)for(int l=1;l<=4;l++) {
            retro_prepare_start_locked(rom,w,l);auto& s=rom.starts[retro_level_id(w,l)];
            Nes_Emu e;retro_check(e.set_cart(&rom.cart,&rom.seed));e.load_state(s->state);auto* m=e.low_mem();
            int ep=m[0xe9]+256*m[0xea],ap=m[0xe7]+256*m[0xe8]-2;bool found=false;
            for(auto& pair:areas)if(pair.second.enemy_ptr==ep&&pair.second.terrain_ptr==ap) {
                pair.second.stages.push_back(std::to_string(w)+"-"+std::to_string(l));found=true;
            }
            if(!found)throw std::runtime_error("catalog pointer failed real stage boot validation");
        }
        std::filesystem::create_directories(std::filesystem::path(argv[1]).parent_path());std::ofstream out(argv[1]);
        out<<"{\"rom_fingerprint\":\"6e01246e5d215cb3\",\"stages_validated\":32,\"areas\":[";bool first=true;
        for(const auto& pair:areas) {
            const auto& a=pair.second;out<<(first?"":",")<<"\n{\"pointer\":"<<a.pointer<<",\"type\":"<<a.type<<",\"stages\":[";first=false;
            for(size_t i=0;i<a.stages.size();i++)out<<(i?",":"")<<'"'<<a.stages[i]<<'"';out<<"],\"destinations\":[";bool dfirst=true;
            for(int d:a.destinations){out<<(dfirst?"":",")<<d;dfirst=false;}out<<"],\"enemies\":[";
            for(size_t i=0;i<a.enemies.size();i++){auto e=a.enemies[i];out<<(i?",":"")<<"{\"id\":"<<e.id<<",\"name\":\""<<enemy_name(e.id)<<"\",\"x\":"<<e.x<<",\"row\":"<<e.row<<",\"hard_only\":"<<e.hard<<"}";}
            out<<"],\"terrain_features\":[";
            for(size_t i=0;i<a.terrain.size();i++){auto e=a.terrain[i];out<<(i?",":"")<<"{\"id\":"<<e.id<<",\"name\":\""<<terrain_name(e.id)<<"\",\"x\":"<<e.x<<",\"row\":"<<e.row<<"}";}
            out<<"]}";
        }
        out<<"\n]}\n";printf("Validated all 32 stage starts; inventoried %zu unique area data sets.\n",areas.size());return 0;
    }catch(const std::exception& e){fprintf(stderr,"Mario catalog: %s\n",e.what());return 2;}
}
