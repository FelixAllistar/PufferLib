#include "lighting.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdlib.h>
#include <string.h>

// Immediate-mode vertices and normals already include rlPushMatrix transforms.
// Mesh vertices do not: separate programs prevent a second transform on batches.
static const char* vertex_source=
    "#version 330\n"
    "in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal; in vec4 vertexColor;\n"
    "uniform mat4 mvp,matModel,matNormal;\n"
    "out vec3 position,normal; out vec2 uv; out vec4 tint;\n"
    "void main(){ position=(matModel*vec4(vertexPosition,1.0)).xyz;"
    "normal=normalize((matNormal*vec4(vertexNormal,0.0)).xyz); uv=vertexTexCoord; tint=vertexColor;"
    "gl_Position=mvp*vec4(vertexPosition,1.0); }\n";
static const char* fragment_source=
    "#version 330\n"
    "in vec3 position,normal; in vec2 uv; in vec4 tint; out vec4 finalColor;\n"
    "uniform sampler2D texture0,sunMap,lampMap; uniform vec4 colDiffuse;\n"
    "uniform mat4 sunMatrix,lampMatrix; uniform vec3 camera;\n"
    "uniform int rooms,lampRoom; uniform vec3 centers[8],halves[8]; uniform float exposure;\n"
    "float visible(sampler2D map,mat4 matrix,vec3 n,vec3 l){"
    "vec4 clip=matrix*vec4(position,1.0); if(clip.w<=0.0)return 1.0;"
    "vec3 p=clip.xyz/clip.w*0.5+0.5;"
    // Compare at the sampled texel's plane depth. A fixed bias alone produces
    // acne on large/sloping surfaces as the orthographic map covers more area.
    "vec2 dx=dFdx(p.xy),dy=dFdy(p.xy); float det=dx.x*dy.y-dx.y*dy.x;"
    "vec2 gradient=abs(det)>1e-12?vec2(dy.y*dFdx(p.z)-dx.y*dFdy(p.z),dx.x*dFdy(p.z)-dy.x*dFdx(p.z))/det:vec2(0.0);"
    "if(any(lessThan(p,vec3(0.0)))||any(greaterThan(p,vec3(1.0))))return 1.0;"
    "float bias=max(0.000025,0.00005*(1.0-max(dot(n,l),0.0)));"
    "vec2 pixel=1.0/vec2(textureSize(map,0)); float v=0.0;"
    "for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++){"
    "vec2 sampleUV=(floor((p.xy+vec2(x,y)*pixel)/pixel)+0.5)*pixel;"
    "v+=step(p.z+dot(gradient,sampleUV-p.xy)-bias,texture(map,sampleUV).r); } return v/9.0; }\n"
    "vec3 tone(vec3 x){return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);}\n"
    "void main(){ vec4 surface=texture(texture0,uv)*tint*colDiffuse;"
    "vec3 albedo=pow(max(surface.rgb,vec3(0.0)),vec3(2.2)); vec3 n=normalize(normal);"
    "vec3 sun=normalize(vec3(-0.45,0.82,-0.35));"
    "float direct=max(dot(n,sun),0.0)*visible(sunMap,sunMatrix,n,sun);"
    "vec3 illumination=mix(vec3(0.13,0.12,0.105),vec3(0.30,0.34,0.39),n.y*0.5+0.5);"
    "illumination+=vec3(0.95,0.88,0.76)*direct;"
    "for(int i=0;i<rooms;i++){ vec3 delta=position-centers[i];"
    "if(abs(delta.x)>halves[i].x+0.12||abs(delta.z)>halves[i].z+0.12||abs(delta.y)>halves[i].y+0.15)continue;"
    "vec3 origin=centers[i]+vec3(0.0,halves[i].y-0.18,0.0); vec3 toLight=origin-position;"
    "float d2=dot(toLight,toLight); vec3 l=normalize(toLight);"
    "float visibility=i==lampRoom?visible(lampMap,lampMatrix,n,l):1.0;"
    "illumination+=vec3(1.0,0.82,0.62)*max(dot(n,l),0.0)*visibility*1.8/(1.0+0.18*d2); }"
    "vec3 color=tone(albedo*illumination*exposure);"
    "finalColor=vec4(pow(color,vec3(1.0/2.2)),surface.a); }\n";

static SwatLightingProgram program(void) {
    SwatLightingProgram p={0}; p.shader=LoadShaderFromMemory(vertex_source,fragment_source);
    if(p.shader.id==rlGetShaderIdDefault()) return p;
    p.shader.locs[SHADER_LOC_MATRIX_MODEL]=GetShaderLocation(p.shader,"matModel");
    p.shader.locs[SHADER_LOC_MATRIX_NORMAL]=GetShaderLocation(p.shader,"matNormal");
    p.camera=GetShaderLocation(p.shader,"camera");
    p.sun_matrix=GetShaderLocation(p.shader,"sunMatrix"); p.lamp_matrix=GetShaderLocation(p.shader,"lampMatrix");
    p.sun_map=GetShaderLocation(p.shader,"sunMap"); p.lamp_map=GetShaderLocation(p.shader,"lampMap");
    p.rooms=GetShaderLocation(p.shader,"rooms"); p.centers=GetShaderLocation(p.shader,"centers[0]");
    p.halves=GetShaderLocation(p.shader,"halves[0]"); p.lamp_room=GetShaderLocation(p.shader,"lampRoom");
    p.exposure=GetShaderLocation(p.shader,"exposure"); return p;
}

static RenderTexture2D depth_target(int size) {
    RenderTexture2D target={0}; target.id=rlLoadFramebuffer();
    if(!target.id) return target;
    target.texture.width=target.texture.height=size;
    target.depth=(Texture2D){rlLoadTextureDepth(size,size,false),size,size,1,19};
    rlFramebufferAttach(target.id,target.depth.id,RL_ATTACHMENT_DEPTH,RL_ATTACHMENT_TEXTURE2D,0);
    if(!target.depth.id || !rlFramebufferComplete(target.id)) {
        rlUnloadFramebuffer(target.id); target=(RenderTexture2D){0};
    } else {
        SetTextureFilter(target.depth,TEXTURE_FILTER_POINT);
        SetTextureWrap(target.depth,TEXTURE_WRAP_CLAMP);
    }
    rlDisableFramebuffer(); return target;
}

void swat_lighting_init(SwatLighting* light) {
    if(light->initialized) return;
    light->initialized=true; light->exposure=1.1f; light->lamp_room=-1;
    const char* mode=getenv("SWAT_LIGHTING"),*exposure=getenv("SWAT_EXPOSURE");
    if(mode && !strcmp(mode,"0")) return;
    if(exposure) { char* end; float v=strtof(exposure,&end); if(end!=exposure && !*end && isfinite(v)) light->exposure=swat_clamp(v,.25f,3); }
    light->batch=program(); light->mesh=program();
    light->sun=depth_target(1536); light->lamp=depth_target(768);
    light->enabled=light->sun.id && light->lamp.id && light->batch.shader.id!=rlGetShaderIdDefault() &&
        light->mesh.shader.id!=rlGetShaderIdDefault();
    light->sun_direction=Vector3Normalize((Vector3){-.45f,.82f,-.35f});
    if(!light->enabled) {
        TraceLog(LOG_WARNING,"SWAT: lighting unavailable, using unlit fallback"); return;
    }
    // Batch uniforms are explicitly identity, never the transform at flush time.
    Matrix identity=MatrixIdentity();
    SetShaderValueMatrix(light->batch.shader,light->batch.shader.locs[SHADER_LOC_MATRIX_MODEL],identity);
    SetShaderValueMatrix(light->batch.shader,light->batch.shader.locs[SHADER_LOC_MATRIX_NORMAL],identity);
    light->batch.shader.locs[SHADER_LOC_MATRIX_MODEL]=-1;
    light->batch.shader.locs[SHADER_LOC_MATRIX_NORMAL]=-1;
    TraceLog(light->enabled ? LOG_INFO : LOG_WARNING,"SWAT: %s; exposure %.2f",light->enabled ? "linear lighting + cached sun/room shadows" : "lighting unavailable, using unlit fallback",light->exposure);
}

void swat_lighting_close(SwatLighting* light) {
    if(light->sun.id) rlUnloadFramebuffer(light->sun.id);
    if(light->lamp.id) rlUnloadFramebuffer(light->lamp.id);
    if(light->batch.shader.id && light->batch.shader.id!=rlGetShaderIdDefault()) UnloadShader(light->batch.shader);
    if(light->mesh.shader.id && light->mesh.shader.id!=rlGetShaderIdDefault()) UnloadShader(light->mesh.shader);
    memset(light,0,sizeof(*light));
}

static uint32_t hash_bytes(uint32_t hash,const void* data,size_t size) {
    const unsigned char* p=data; for(size_t i=0;i<size;i++) { hash^=p[i]; hash*=16777619u; } return hash;
}
static uint32_t geometry_hash(const SwatWorld* world) {
    uint32_t hash=2166136261u;
    for(int i=0;i<world->count;i++) {
        const SwatObject* o=&world->objects[i]; hash=hash_bytes(hash,&o->active,sizeof(o->active));
        if(!o->active || o->material==SWAT_GLASS) continue;
        hash=hash_bytes(hash,&o->center,sizeof(o->center)); hash=hash_bytes(hash,&o->half,sizeof(o->half));
        hash=hash_bytes(hash,&o->yaw,sizeof(o->yaw)); hash=hash_bytes(hash,&o->pitch,sizeof(o->pitch));
    }
    return hash_bytes(hash,world->rooms,(size_t)world->room_count*sizeof(*world->rooms));
}

static Matrix shadow(RenderTexture2D target,Camera3D camera,const SwatSim* sim,bool cutaway,SwatShadowScene draw) {
    double near=rlGetCullDistanceNear(),far=rlGetCullDistanceFar(); rlSetClipPlanes(.05,120);
    BeginTextureMode(target); ClearBackground(WHITE); BeginMode3D(camera);
    Matrix matrix=MatrixMultiply(rlGetMatrixModelview(),rlGetMatrixProjection());
    draw(sim,cutaway); EndMode3D(); EndTextureMode(); rlSetClipPlanes(near,far); return matrix;
}
void swat_lighting_prepare(SwatLighting* light,const SwatSim* sim,Vector3 eye,bool cutaway,SwatShadowScene draw) {
    if(!light->enabled) return;
    const SwatWorld* world=&sim->world; int room=-1; float distance=1e30f;
    Vector3 center={11,0,0}; float extent=28;
    if(world->room_count) {
        float x0=1e30f,z0=1e30f,x1=-1e30f,z1=-1e30f;
        for(int i=0;i<world->room_count;i++) {
            const SwatRoom* r=&world->rooms[i];
            x0=fminf(x0,(float)r->center.x-r->half.x); x1=fmaxf(x1,(float)r->center.x+r->half.x);
            z0=fminf(z0,(float)r->center.z-r->half.z); z1=fmaxf(z1,(float)r->center.z+r->half.z);
            float dx=eye.x-(float)r->center.x,dz=eye.z-(float)r->center.z,d=dx*dx+dz*dz;
            if(d<distance) { distance=d; room=i; }
        }
        center=(Vector3){(x0+x1)*.5f,0,(z0+z1)*.5f}; extent=fmaxf(x1-x0,z1-z0)*1.5f+12;
    }
    uint32_t hash=geometry_hash(world);
    if(light->prepared && hash==light->geometry && room==light->lamp_room && cutaway==light->cutaway &&
        sim->tick>=light->last_tick && sim->tick-light->last_tick<4) return;
    Camera3D sun={Vector3Add(center,Vector3Scale(light->sun_direction,45)),center,{0,1,0},extent,CAMERA_ORTHOGRAPHIC};
    light->sun_matrix=shadow(light->sun,sun,sim,cutaway,draw);
    if(room>=0) {
        const SwatRoom* r=&world->rooms[room];
        Vector3 origin={(float)r->center.x,(float)r->center.y+r->half.y-.18f,(float)r->center.z};
        Camera3D lamp={origin,{origin.x,origin.y-1,origin.z},{0,0,-1},150,CAMERA_PERSPECTIVE};
        light->lamp_matrix=shadow(light->lamp,lamp,sim,cutaway,draw);
    } else light->lamp_matrix=MatrixIdentity();
    light->geometry=hash; light->lamp_room=room; light->last_tick=sim->tick;
    light->cutaway=cutaway; light->prepared=true; light->updates++;
}

static void model_shader(Model model,Shader shader) {
    for(int i=0;i<model.materialCount;i++) model.materials[i].shader=shader;
}
static void art_shader(SwatEnvironmentArt* art,Shader shader) {
    model_shader(art->door,shader);
    for(int i=0;i<SWAT_ENV_PROP_KINDS;i++) model_shader(art->props[i],shader);
}
void swat_lighting_begin(SwatLighting* light,SwatEnvironmentArt* art,const SwatWorld* world,Vector3 camera) {
    if(!light->enabled || !light->prepared) return;
    Vector3 centers[SWAT_MAX_ROOMS],halves[SWAT_MAX_ROOMS];
    for(int i=0;i<world->room_count;i++) {
        const SwatRoom* r=&world->rooms[i];
        centers[i]=(Vector3){(float)r->center.x,(float)r->center.y,(float)r->center.z};
        halves[i]=(Vector3){r->half.x,r->half.y,r->half.z};
    }
    SwatLightingProgram* programs[]={&light->batch,&light->mesh};
    for(int i=0;i<2;i++) {
        SwatLightingProgram* p=programs[i]; Shader s=p->shader; int sun=14,lamp=15;
        SetShaderValueMatrix(s,p->sun_matrix,light->sun_matrix); SetShaderValueMatrix(s,p->lamp_matrix,light->lamp_matrix);
        SetShaderValue(s,p->camera,&camera,SHADER_UNIFORM_VEC3);
        SetShaderValue(s,p->sun_map,&sun,SHADER_UNIFORM_INT); SetShaderValue(s,p->lamp_map,&lamp,SHADER_UNIFORM_INT);
        SetShaderValue(s,p->rooms,&world->room_count,SHADER_UNIFORM_INT); SetShaderValue(s,p->lamp_room,&light->lamp_room,SHADER_UNIFORM_INT);
        SetShaderValue(s,p->exposure,&light->exposure,SHADER_UNIFORM_FLOAT);
        if(world->room_count) {
            SetShaderValueV(s,p->centers,centers,SHADER_UNIFORM_VEC3,world->room_count);
            SetShaderValueV(s,p->halves,halves,SHADER_UNIFORM_VEC3,world->room_count);
        }
    }
    rlActiveTextureSlot(14); rlEnableTexture(light->sun.depth.id);
    rlActiveTextureSlot(15); rlEnableTexture(light->lamp.depth.id); rlActiveTextureSlot(0);
    art->lit=true; art_shader(art,light->mesh.shader); BeginShaderMode(light->batch.shader);
}
void swat_lighting_end(SwatLighting* light,SwatEnvironmentArt* art) {
    if(!light->enabled || !light->prepared) return;
    EndShaderMode(); art->lit=false; art_shader(art,(Shader){rlGetShaderIdDefault(),rlGetShaderLocsDefault()});
    rlActiveTextureSlot(14); rlDisableTexture(); rlActiveTextureSlot(15); rlDisableTexture(); rlActiveTextureSlot(0);
}
