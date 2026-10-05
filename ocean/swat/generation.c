#include "generation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "generated/layout_policy.h"

// width, depth, topology, X split, entry, Z split, windows, door offset,
// furnishing, floor palette, occupants, rear exit. Conditional decisions
// are sampled in order; geometry remains independently validated.
static const int categories[SWAT_LAYOUT_TOKENS]={3,3,3,3,2,3,3,3,3,3,3,2};
static float loaded_policy[SWAT_LAYOUT_PARAMETERS];
static bool has_loaded_policy;
static uint32_t loaded_id;
const int* swat_layout_categories(void) { return categories; }
uint32_t swat_layout_policy_id(void) { return has_loaded_policy ? loaded_id : swat_layout_builtin_id; }
static uint32_t hash_word(uint32_t hash,uint32_t word) {
    for(int i=0;i<4;i++) { hash^=(word>>(8*i))&255; hash*=16777619u; }
    return hash;
}
bool swat_layout_load_policy(const char* path) {
    FILE* file=fopen(path,"r"); if(!file) return false;
    char magic[32]; int version,inputs,hidden,outputs; unsigned int id;
    bool ok=fscanf(file,"%31s %d %d %d %d %u",magic,&version,&inputs,&hidden,&outputs,&id)==6 &&
        !strcmp(magic,"SWAT_LAYOUT_NET") && version==1 && inputs==SWAT_LAYOUT_INPUT && hidden==SWAT_LAYOUT_HIDDEN && outputs==SWAT_LAYOUT_OUTPUT;
    float weights[SWAT_LAYOUT_PARAMETERS];
    for(int i=0;ok && i<SWAT_LAYOUT_PARAMETERS;i++) ok=fscanf(file,"%f",&weights[i])==1 && isfinite(weights[i]) && fabsf(weights[i])<100;
    char extra; if(ok && fscanf(file," %c",&extra)==1) ok=false;
    fclose(file);
    if(ok) { memcpy(loaded_policy,weights,sizeof(weights)); loaded_id=id; has_loaded_policy=true; }
    return ok;
}
void swat_layout_logits(const int* tokens,int step,int difficulty,float logits[SWAT_LAYOUT_OUTPUT]) {
    float input[SWAT_LAYOUT_INPUT]={0},hidden[SWAT_LAYOUT_HIDDEN];
    int offset=0;
    for(int i=0;i<SWAT_LAYOUT_TOKENS;i++) {
        if(i<step && tokens[i]>=0 && tokens[i]<categories[i]) input[offset+tokens[i]]=1;
        offset+=categories[i];
    }
    if(step>=0 && step<SWAT_LAYOUT_TOKENS) input[offset+step]=1;
    if(difficulty>=0 && difficulty<3) input[offset+SWAT_LAYOUT_TOKENS+difficulty]=1;
    const float* weights=has_loaded_policy ? loaded_policy : swat_layout_builtin;
    int cursor=0;
    for(int h=0;h<SWAT_LAYOUT_HIDDEN;h++) {
        float value=0; for(int i=0;i<SWAT_LAYOUT_INPUT;i++) value+=weights[cursor++]*input[i]; hidden[h]=value;
    }
    for(int h=0;h<SWAT_LAYOUT_HIDDEN;h++) hidden[h]=tanhf(hidden[h]+weights[cursor++]);
    for(int o=0;o<SWAT_LAYOUT_OUTPUT;o++) {
        float value=0; for(int h=0;h<SWAT_LAYOUT_HIDDEN;h++) value+=weights[cursor++]*hidden[h]; logits[o]=value;
    }
    for(int o=0;o<SWAT_LAYOUT_OUTPUT;o++) logits[o]+=weights[cursor++];
}
static void room(SwatLayout* p,float x0,float x1,float z0,float z1,bool hall) {
    SwatMaterial palette[3]={SWAT_WOOD,SWAT_CARPET,SWAT_TILE};
    int i=p->room_count++; p->rooms[i]=(SwatPlanRoom){.x0=x0,.x1=x1,.z0=z0,.z1=z1,.hall=hall,.floor=palette[(i+p->tokens[9])%3]};
}
static b3Pos wall_opening(const SwatPlanWall* wall) {
    return b3OffsetPos(wall->origin,swat_v(sinf(wall->yaw)*wall->opening,0,cosf(wall->yaw)*wall->opening));
}
static void wall(SwatLayout* p,int a,int b,b3Pos origin,float yaw,float length,bool exterior,bool door) {
    if(p->wall_count>=SWAT_LAYOUT_WALLS) return;
    SwatPlanWall* w=&p->walls[p->wall_count++];
    *w=(SwatPlanWall){.origin=origin,.yaw=yaw,.length=length,.a=a,.b=b,.exterior=exterior,.door=door};
    if(length<1.65f) return;
    w->width=door ? 1.2f : fminf(length-.5f,p->tokens[6]==0 ? 1.8f : 1.3f);
    if(exterior && !door && p->tokens[6]==2 && a%2) { w->width=0; return; }
    w->opening=swat_clamp((p->tokens[7]-1)*.35f,-(length-w->width)*.5f+.2f,(length-w->width)*.5f-.2f);
    w->sill=door ? 0 : .85f;
}
static bool spawn_clear(const SwatLayout* p,b3Pos pos) {
    for(int i=0;i<p->spawn_count;i++) if(b3Distance(pos,p->spawns[i].feet)<1.02f) return false;
    for(int i=0;i<p->wall_count;i++) if(p->walls[i].door && p->walls[i].width && b3Distance(pos,wall_opening(&p->walls[i]))<1.0f) return false;
    return true;
}
static bool spawn(SwatLayout* p,int actor,int role,int preferred,int variant) {
    static const float choices[9][2]={{0,0},{.75f,.75f},{-.75f,-.75f},{.75f,-.75f},{-.75f,.75f},{0,.8f},{0,-.8f},{.8f,0},{-.8f,0}};
    for(int offset=0;offset<p->room_count;offset++) {
        const SwatPlanRoom* r=&p->rooms[(preferred+offset)%p->room_count]; if(r->hall) continue;
        for(int t=0;t<9;t++) {
            int choice=role==1 && t==0 ? 0 : (t+variant)%9;
            b3Pos pos={(r->x0+r->x1)*.5f+choices[choice][0]*((r->x1-r->x0)*.5f-.6f),0,
                       (r->z0+r->z1)*.5f+choices[choice][1]*((r->z1-r->z0)*.5f-.6f)};
            if(!spawn_clear(p,pos)) continue;
            p->spawns[p->spawn_count++]=(SwatPlanSpawn){actor,role,pos,SWAT_PI}; return true;
        }
    }
    return false;
}
bool swat_layout_plan(SwatLayout* p,const int* tokens,int difficulty) {
    if(difficulty<0 || difficulty>2) return false;
    for(int i=0;i<SWAT_LAYOUT_TOKENS;i++) if(tokens[i]<0 || tokens[i]>=categories[i]) return false;
    memset(p,0,sizeof(*p)); memcpy(p->tokens,tokens,sizeof(p->tokens)); p->difficulty=difficulty;
    p->fingerprint=hash_word(2166136261u,difficulty);
    for(int i=0;i<SWAT_LAYOUT_TOKENS;i++) p->fingerprint=hash_word(p->fingerprint,tokens[i]);
    p->width=10+2*tokens[0]; p->depth=8+2*tokens[1];
    float x0=4,x1=4+p->width,z0=-p->depth*.5f,z1=p->depth*.5f;
    float sx=x0+p->width*(.42f+.08f*tokens[3]),sz=(float)(tokens[5]-1);
    if(tokens[2]==0) {
        room(p,x0,sx,z0,z1,false); room(p,sx,x1,z0,sz,false); room(p,sx,x1,sz,z1,false);
    } else if(tokens[2]==1) {
        room(p,x0,sx,z0,sz,false); room(p,x0,sx,sz,z1,false);
        room(p,sx,x1,z0,-sz,false); room(p,sx,x1,-sz,z1,false);
    } else {
        sx=x0+p->width*.5f+(tokens[3]-1)*.5f;
        room(p,x0,sx-.9f,z0,sz,false); room(p,x0,sx-.9f,sz,z1,false);
        room(p,sx-.9f,sx+.9f,z0,z1,true);
        room(p,sx+.9f,x1,z0,-sz,false); room(p,sx+.9f,x1,-sz,z1,false);
    }
    p->entry_room=tokens[2] && tokens[4] ? 1 : 0;
    const SwatPlanRoom* entry=&p->rooms[p->entry_room];
    float entry_z=(entry->z0+entry->z1)*.5f+(tokens[2]==0 ? (tokens[4] ? 1 : -1)*p->depth*.22f : 0);
    int rear_room=p->room_count-1-(tokens[11] ? 1 : 0);
    if(p->rooms[rear_room].hall) rear_room=p->room_count-1;
    for(int i=0;i<p->room_count;i++) {
        const SwatPlanRoom* a=&p->rooms[i];
        if(a->x0==x0) {
            wall(p,i,-1,(b3Pos){x0,0,(a->z0+a->z1)*.5f},0,a->z1-a->z0,true,i==p->entry_room);
            if(i==p->entry_room) p->walls[p->wall_count-1].opening=entry_z-(a->z0+a->z1)*.5f;
        }
        if(a->x1==x1) wall(p,i,-1,(b3Pos){x1,0,(a->z0+a->z1)*.5f},SWAT_PI,a->z1-a->z0,true,i==rear_room);
        if(a->z0==z0) wall(p,i,-1,(b3Pos){(a->x0+a->x1)*.5f,0,z0},-SWAT_PI*.5f,a->x1-a->x0,true,false);
        if(a->z1==z1) wall(p,i,-1,(b3Pos){(a->x0+a->x1)*.5f,0,z1},SWAT_PI*.5f,a->x1-a->x0,true,false);
        for(int j=i+1;j<p->room_count;j++) {
            const SwatPlanRoom* b=&p->rooms[j]; float lo=fmaxf(a->z0,b->z0),hi=fminf(a->z1,b->z1);
            if((a->x1==b->x0 || a->x0==b->x1) && hi>lo+.01f)
                wall(p,i,j,(b3Pos){a->x1==b->x0 ? a->x1 : a->x0,0,(lo+hi)*.5f},0,hi-lo,false,hi-lo>=1.65f);
            lo=fmaxf(a->x0,b->x0); hi=fminf(a->x1,b->x1);
            if((a->z1==b->z0 || a->z0==b->z1) && hi>lo+.01f)
                wall(p,i,j,(b3Pos){(lo+hi)*.5f,0,a->z1==b->z0 ? a->z1 : a->z0},SWAT_PI*.5f,hi-lo,false,hi-lo>=1.65f);
        }
    }
    p->mission=(SwatMissionDef){.name="Generated residence",.briefing="Secure the occupants and return to staging.",
        .staging={0,0,entry_z},.extraction={-1.5f,0,entry_z}};
    int candidate[SWAT_LAYOUT_ROOMS],n=0;
    for(int i=p->room_count-1;i>=0;i--) if(!p->rooms[i].hall) candidate[n++]=i;
    int guards=1+difficulty;
    for(int i=0;i<guards;i++) if(!spawn(p,i==0 ? 1 : (i==1 ? 8 : 11),1,candidate[(i+tokens[10])%n],tokens[10])) return false;
    const int civilians[3]={2,6,7};
    for(int i=0;i<3;i++) if(!spawn(p,civilians[i],2,candidate[(i/2+tokens[10])%n],i+1)) return false;
    for(int i=0;i<p->room_count;i++) {
        const SwatPlanRoom* r=&p->rooms[i]; if(r->hall) continue;
        int added=0;
        for(int corner=0;corner<4 && added<tokens[8];corner++) {
            SwatPlanFurniture f={{corner&1 ? r->x1-.85f : r->x0+.85f,.4f,corner&2 ? r->z1-.75f : r->z0+.75f},
                {.6f,.4f,.4f},SWAT_WOOD};
            bool blocked=false;
            for(int j=0;j<p->spawn_count;j++) {
                b3Vec3 d=b3SubPos(p->spawns[j].feet,f.center);
                if(fabsf(d.x)<f.half.x+.55f && fabsf(d.z)<f.half.z+.55f) blocked=true;
            }
            for(int j=0;j<p->wall_count;j++) if(p->walls[j].door && p->walls[j].width) {
                b3Vec3 d=b3SubPos(wall_opening(&p->walls[j]),f.center);
                if(fabsf(d.x)<f.half.x+1.1f && fabsf(d.z)<f.half.z+1.1f) blocked=true;
            }
            if(!blocked && p->furniture_count<SWAT_LAYOUT_FURNITURE) { p->furniture[p->furniture_count++]=f; added++; }
        }
    }
    // Place three real exterior posts facing windows in different rooms.
    bool covered[SWAT_LAYOUT_ROOMS]={0};
    static const char* labels[3]={"Window post A","Window post B","Window post C"};
    for(int pass=0;pass<2 && p->mission.overwatch_count<3;pass++) for(int i=p->wall_count-1;i>=0 && p->mission.overwatch_count<3;i--) {
        const SwatPlanWall* w=&p->walls[i];
        if(!w->exterior || w->door || !w->width || (pass==0 && covered[w->a])) continue;
        b3Pos opening=wall_opening(w); b3Vec3 normal=swat_v(-cosf(w->yaw),0,sinf(w->yaw));
        b3Pos post=b3OffsetPos(opening,swat_mul(normal,8)); post.y=2.3f;
        bool duplicate=false;
        for(int k=0;k<p->mission.overwatch_count;k++) if(b3Distance(post,p->mission.overwatch[k].position)<2) duplicate=true;
        if(duplicate) continue;
        const SwatPlanRoom* r=&p->rooms[w->a]; int index=p->mission.overwatch_count++;
        p->mission.overwatch[index]=(SwatOverwatch){labels[index],post,{(r->x0+r->x1)*.5f,1.45f,(r->z0+r->z1)*.5f}};
        covered[w->a]=true;
    }
    return swat_layout_validate(p);
}

// Conservative standing-agent clearance on a 30 cm grid. Doors are treated as
// openable portals, but their frames, all wall faces and furniture remain solid.
static bool walkable(const SwatLayout* p,float x,float z) {
    const float radius=.43f;
    for(int i=0;i<p->wall_count;i++) {
        const SwatPlanWall* w=&p->walls[i]; float dx=x-(float)w->origin.x,dz=z-(float)w->origin.z;
        float across,along;
        if(fabsf(w->yaw)<.1f) { across=dx; along=dz; }
        else if(fabsf(w->yaw)>3) { across=-dx; along=-dz; }
        else if(w->yaw>0) { across=-dz; along=dx; }
        else { across=dz; along=-dx; }
        if(fabsf(across)>.062f+radius || fabsf(along)>w->length*.5f+radius) continue;
        if(w->door && w->width && fabsf(along-w->opening)<w->width*.5f-radius) continue;
        return false;
    }
    for(int i=0;i<p->furniture_count;i++) {
        const SwatPlanFurniture* f=&p->furniture[i];
        if(fabsf(x-(float)f->center.x)<f->half.x+radius && fabsf(z-(float)f->center.z)<f->half.z+radius) return false;
    }
    return true;
}
bool swat_layout_validate(SwatLayout* p) {
    if(p->room_count<3 || p->room_count>5 || p->mission.overwatch_count<2) return false;
    // Count through the same wall builder before allocating any physics bodies.
    p->object_count=6+p->room_count+p->furniture_count+p->mission.overwatch_count;
    int graph[SWAT_LAYOUT_ROOMS]={0},doors=0,windows=0;
    for(int i=0;i<p->wall_count;i++) {
        const SwatPlanWall* w=&p->walls[i];
        if(w->width && (w->width>w->length-.3f || fabsf(w->opening)+w->width*.5f>w->length*.5f-.1f)) return false;
        p->object_count+=swat_framed_wall_pieces(w->length,2.7f,w->opening,w->width,w->sill,w->door ? 2.1f : 1.25f,w->exterior);
        if(w->door && w->width) { doors++; if(w->b>=0) { graph[w->a]|=1<<w->b; graph[w->b]|=1<<w->a; } }
        else windows+=w->width>0;
    }
    if(p->object_count>SWAT_MAX_OBJECTS-8) return false;
    unsigned seen=1u<<p->entry_room;
    for(int pass=0;pass<p->room_count;pass++) for(int i=0;i<p->room_count;i++) if(seen&(1u<<i)) seen|=(unsigned)graph[i];
    if(seen!=(1u<<p->room_count)-1) return false;
    enum { W=96,H=64,N=W*H }; unsigned char blocked[N]; int distance[N],queue[N];
    const float minx=-2,minz=-9,cell=.3f;
    for(int z=0;z<H;z++) for(int x=0;x<W;x++) {
        int i=z*W+x; blocked[i]=!walkable(p,minx+x*cell,minz+z*cell); distance[i]=-1;
    }
    int sx=(int)lroundf(((float)p->mission.staging.x-minx)/cell),sz=(int)lroundf(((float)p->mission.staging.z-minz)/cell);
    int head=0,tail=0; queue[tail++]=sz*W+sx; distance[sz*W+sx]=0;
    while(head<tail) {
        int at=queue[head++],x=at%W,z=at/W;
        int neighbors[4]={x>0 ? at-1 : -1,x<W-1 ? at+1 : -1,z>0 ? at-W : -1,z<H-1 ? at+W : -1};
        for(int n=0;n<4;n++) { int id=neighbors[n]; if(id>=0 && !blocked[id] && distance[id]<0) { distance[id]=distance[at]+1; queue[tail++]=id; } }
    }
    float path=0;
    for(int i=0;i<p->spawn_count;i++) {
        const SwatPlanSpawn* spawn=&p->spawns[i]; int best=100000;
        int x=(int)lroundf(((float)spawn->feet.x-minx)/cell),z=(int)lroundf(((float)spawn->feet.z-minz)/cell);
        for(int dz=-1;dz<=1;dz++) for(int dx=-1;dx<=1;dx++) {
            int cx=x+dx,cz=z+dz; if(cx<0 || cx>=W || cz<0 || cz>=H) continue;
            int d=distance[cz*W+cx]; if(d>=0 && d<best) best=d;
        }
        if(best==100000) return false;
        path+=best*cell;
    }
    p->path_length=path/p->spawn_count;
    float aspect=0;
    for(int i=0;i<p->room_count;i++) if(!p->rooms[i].hall) {
        float w=p->rooms[i].x1-p->rooms[i].x0,d=p->rooms[i].z1-p->rooms[i].z0;
        if(w<2.5f || d<2.5f) return false;
        aspect+=fmaxf(w/d,d/w)-1;
    }
    // A bootstrap design heuristic, never a claim about human enjoyment.
    float area_target=100+25*p->difficulty;
    p->quality=2-.015f*fabsf(p->width*p->depth-area_target)-.32f*abs(p->room_count-(3+p->difficulty))-
        .12f*aspect-.035f*abs(windows-(7+p->difficulty))+.06f*fminf(p->furniture_count,6)+.03f*fminf(doors,8);
    return true;
}
bool swat_layout_generate(SwatLayout* p,uint32_t seed,int difficulty,SwatGenerator generator) {
    if(difficulty<0 || difficulty>2 || generator<0 || generator>=SWAT_GENERATORS) return false;
    // Avalanche sequential user seeds before xorshift; adjacent seeds must not
    // all select the same first room-size category.
    uint32_t rng=seed+0x9e3779b9u;
    rng^=rng>>16; rng*=0x85ebca6bu; rng^=rng>>13; rng*=0xc2b2ae35u; rng^=rng>>16;
    if(!rng) rng=1;
    for(int attempt=0;attempt<128;attempt++) {
        int tokens[SWAT_LAYOUT_TOKENS]={0};
        for(int step=0;step<SWAT_LAYOUT_TOKENS;step++) {
            float logits[3]={0}; if(generator==SWAT_LAYOUT_NEURAL) swat_layout_logits(tokens,step,difficulty,logits);
            float highest=-1e9f,total=0,prob[3]; for(int i=0;i<categories[step];i++) highest=fmaxf(highest,logits[i]);
            for(int i=0;i<categories[step];i++) { prob[i]=expf((logits[i]-highest)/.9f); total+=prob[i]; }
            float choice=swat_rand01(&rng)*total; int selected=categories[step]-1;
            for(int i=0;i<categories[step];i++) { choice-=prob[i]; if(choice<=0) { selected=i; break; } }
            tokens[step]=selected;
        }
        if(swat_layout_plan(p,tokens,difficulty)) {
            p->seed=seed; p->policy_id=generator==SWAT_LAYOUT_NEURAL ? swat_layout_policy_id() : 0; return true;
        }
    }
    return false;
}
void swat_layout_build(SwatWorld* w,const SwatLayout* p) {
    float x0=4,x1=4+p->width,z0=-p->depth*.5f,z1=p->depth*.5f,cx=(x0+x1)*.5f;
    swat_world_box(w,(b3Pos){-6,-.5f,0},swat_v(10,.5f,20),SWAT_SOIL,0);
    swat_world_box(w,(b3Pos){x1+8,-.5f,0},swat_v(8,.5f,20),SWAT_SOIL,0);
    swat_world_box(w,(b3Pos){cx,-.5f,z0-7},swat_v(p->width*.5f,.5f,7),SWAT_SOIL,0);
    swat_world_box(w,(b3Pos){cx,-.5f,z1+7},swat_v(p->width*.5f,.5f,7),SWAT_SOIL,0);
    swat_world_box(w,(b3Pos){cx,-.12f,0},swat_v(p->width*.5f,.1f,p->depth*.5f),SWAT_CONCRETE,0);
    for(int i=0;i<p->room_count;i++) {
        const SwatPlanRoom* r=&p->rooms[i]; b3Vec3 half=swat_v((r->x1-r->x0)*.5f,.01f,(r->z1-r->z0)*.5f);
        b3Pos center={(r->x0+r->x1)*.5f,-.01f,(r->z0+r->z1)*.5f};
        swat_world_box(w,center,half,r->floor,0);
        center.y=1.35f; half.x-=.10f; half.z-=.10f; half.y=1.35f;
        w->rooms[w->room_count++]=(SwatRoom){center,half,SWAT_DRYWALL,r->floor};
    }
    for(int i=0;i<p->wall_count;i++) {
        const SwatPlanWall* wall=&p->walls[i];
        swat_build_framed_wall(w,wall->origin,wall->yaw,wall->length,2.7f,wall->opening,wall->width,
            wall->sill,wall->door ? 2.1f : 1.25f,wall->exterior);
    }
    for(int i=0;i<p->furniture_count;i++) swat_world_box(w,p->furniture[i].center,p->furniture[i].half,p->furniture[i].material,130);
    swat_world_box(w,(b3Pos){cx,2.8f,0},swat_v(p->width*.5f+.15f,.1f,p->depth*.5f+.15f),SWAT_WOOD,0);
    for(int i=0;i<p->mission.overwatch_count;i++) {
        b3Pos post=p->mission.overwatch[i].position; float top=(float)post.y-1.6256f;
        swat_world_box(w,(b3Pos){post.x,top*.5f,post.z},swat_v(.85f,top*.5f,.85f),SWAT_BRICK,0);
    }
}
