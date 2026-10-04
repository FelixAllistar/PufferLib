#include "lighting.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdlib.h>
#include <string.h>

// Immediate-mode vertices and normals already include rlPushMatrix transforms.
// Mesh vertices do not: separate programs prevent a second transform on batches.
static const char* vertex_source=
    "#version 330\n"
    "in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal; in vec4 vertexColor,vertexTangent;\n"
    "uniform mat4 mvp,matModel,matNormal;\n"
    "uniform vec3 environmentSize; uniform vec2 environmentTile;\n"
    "out vec3 position,normal; out vec2 uv; out vec4 tint,tangent;\n"
    "void main(){ position=(matModel*vec4(vertexPosition,1.0)).xyz;"
    "normal=normalize((matNormal*vec4(vertexNormal,0.0)).xyz); uv=vertexTexCoord; tint=vertexColor;"
    "if(environmentTile.x>0.0){ vec3 p=vertexPosition*environmentSize;"
    "vec3 a=abs(vertexNormal); uv=(a.y>max(a.x,a.z)?vec2(p.x,p.z):vec2(a.x>=a.z?p.z:p.x,p.y))/environmentTile; }"
    "tangent=vec4((matModel*vec4(vertexTangent.xyz,0.0)).xyz,vertexTangent.w);"
    "gl_Position=mvp*vec4(vertexPosition,1.0); }\n";
static const char* fragment_source=
    "#version 330\n"
    "in vec3 position,normal; in vec2 uv; in vec4 tint,tangent; out vec4 finalColor;\n"
    "uniform sampler2D texture0,sunMap,lampMap; uniform vec4 colDiffuse;\n"
    "uniform sampler2D normalMap,ormMap; uniform int usePbr,useNormal; uniform float roughnessFactor,metalnessFactor;\n"
    "uniform sampler2D environmentNormalMap,environmentRoughnessMap; uniform int useEnvironment,useEnvironmentNormal;\n"
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
    // Continuous tent weights avoid the blocky jumps of equal-weight snapped
    // PCF as an edge or camera crosses a shadow-map texel.
    "vec2 pixel=1.0/vec2(textureSize(map,0)),texel=p.xy/pixel-0.5,base=floor(texel),fraction=fract(texel); float v=0.0;"
    "for(int x=-1;x<=2;x++)for(int y=-1;y<=2;y++){"
    "vec2 offset=vec2(x,y),sampleUV=(base+offset+0.5)*pixel;"
    "vec2 weight=2.0-abs(offset-fraction);"
    "v+=weight.x*weight.y*step(p.z+dot(gradient,sampleUV-p.xy)-bias,texture(map,sampleUV).r); } return v/16.0; }\n"
    "vec3 tone(vec3 x){return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);}\n"
    "vec3 specular(vec3 n,vec3 v,vec3 l,vec3 f0,float r){"
    "vec3 h=normalize(v+l); float nv=max(dot(n,v),0.001),nl=max(dot(n,l),0.0),nh=max(dot(n,h),0.0);"
    "float a=r*r,a2=a*a,d=nh*nh*(a2-1.0)+1.0; float distribution=a2/(3.14159265*d*d);"
    "float k=(r+1.0)*(r+1.0)/8.0; float g=nv/(nv*(1.0-k)+k)*nl/(nl*(1.0-k)+k);"
    "vec3 f=f0+(1.0-f0)*pow(1.0-max(dot(v,h),0.0),5.0);"
    "return distribution*g*f/(4.0*nv); }\n"
    "void main(){ vec4 surface=texture(texture0,uv)*tint*colDiffuse;"
    "vec3 albedo=pow(max(surface.rgb,vec3(0.0)),vec3(2.2)); vec3 n=normalize(normal);"
    "float roughness=1.0,metalness=0.0; vec3 reflection=vec3(0.0),f0=vec3(0.04),v=normalize(camera-position);"
    // Solve the signed UV derivatives, preserving mirrored face handedness.
    // No mesh tangents are assumed for immediate boxes or the old door GLB.
    "if(useEnvironment!=0){ roughness=clamp(texture(environmentRoughnessMap,uv).r,0.08,1.0);"
    "if(useEnvironmentNormal!=0){ vec3 dx=dFdx(position),dy=dFdy(position); vec2 tx=dFdx(uv),ty=dFdy(uv);"
    "float determinant=tx.x*ty.y-tx.y*ty.x; if(abs(determinant)>1e-12){"
    "vec3 t=(dx*ty.y-dy*tx.y)/determinant,b=(dy*tx.x-dx*ty.x)/determinant;"
    "t-=n*dot(n,t); b-=n*dot(n,b); if(dot(t,t)>1e-12&&dot(b,b)>1e-12)"
    "n=normalize(mat3(normalize(t),normalize(b),n)*(texture(environmentNormalMap,uv).xyz*2.0-1.0)); } } }"
    "if(usePbr!=0){ vec4 orm=texture(ormMap,uv); roughness=clamp(orm.g*roughnessFactor,0.08,1.0);"
    "metalness=clamp(orm.b*metalnessFactor,0.0,1.0); f0=mix(vec3(0.04),albedo,metalness);"
    "if(useNormal!=0){ vec3 t=normalize(tangent.xyz-n*dot(n,tangent.xyz));"
    "vec3 b=cross(n,t)*tangent.w; n=normalize(mat3(t,b,n)*(texture(normalMap,uv).xyz*2.0-1.0)); } }"
    "vec3 sun=normalize(vec3(-0.45,0.82,-0.35));"
    "float sunVisibility=visible(sunMap,sunMatrix,n,sun); float direct=max(dot(n,sun),0.0)*sunVisibility;"
    "vec3 illumination=mix(vec3(0.13,0.12,0.105),vec3(0.30,0.34,0.39),n.y*0.5+0.5);"
    "illumination+=vec3(0.95,0.88,0.76)*direct;"
    "if(usePbr!=0||useEnvironment!=0)reflection+=vec3(0.95,0.88,0.76)*sunVisibility*specular(n,v,sun,f0,roughness);"
    "for(int i=0;i<rooms;i++){ vec3 delta=position-centers[i];"
    "if(abs(delta.x)>halves[i].x+0.12||abs(delta.z)>halves[i].z+0.12||abs(delta.y)>halves[i].y+0.15)continue;"
    "vec3 origin=centers[i]+vec3(0.0,halves[i].y-0.18,0.0); vec3 toLight=origin-position;"
    "float d2=dot(toLight,toLight); vec3 l=normalize(toLight);"
    "float visibility=i==lampRoom?visible(lampMap,lampMatrix,n,l):1.0;"
    "vec3 radiance=vec3(1.0,0.82,0.62)*visibility*1.8/(1.0+0.18*d2);"
    "illumination+=radiance*max(dot(n,l),0.0); if(usePbr!=0||useEnvironment!=0)reflection+=radiance*specular(n,v,l,f0,roughness); }"
    // Broad hemisphere reflection keeps metal readable without inventing a
    // second diffuse response. This is an approximation, not scene IBL.
    "vec3 reflectedDirection=reflect(-v,n);"
    "vec3 environment=mix(vec3(0.13,0.12,0.105),vec3(0.30,0.34,0.39),reflectedDirection.y*0.5+0.5);"
    "vec3 color=tone((albedo*illumination*(1.0-metalness)+reflection+f0*metalness*environment)*exposure);"
    "finalColor=vec4(pow(color,vec3(1.0/2.2)),surface.a); }\n";

static SwatLightingProgram program(void) {
    SwatLightingProgram p={0}; p.shader=LoadShaderFromMemory(vertex_source,fragment_source);
    if(p.shader.id==rlGetShaderIdDefault()) return p;
    p.shader.locs[SHADER_LOC_MATRIX_MODEL]=GetShaderLocation(p.shader,"matModel");
    p.shader.locs[SHADER_LOC_MATRIX_NORMAL]=GetShaderLocation(p.shader,"matNormal");
    p.shader.locs[SHADER_LOC_VERTEX_TANGENT]=GetShaderLocationAttrib(p.shader,"vertexTangent");
    p.shader.locs[SHADER_LOC_MAP_NORMAL]=GetShaderLocation(p.shader,"normalMap");
    p.shader.locs[SHADER_LOC_MAP_ROUGHNESS]=GetShaderLocation(p.shader,"ormMap");
    p.pbr=GetShaderLocation(p.shader,"usePbr"); p.normal_map=GetShaderLocation(p.shader,"useNormal");
    p.roughness=GetShaderLocation(p.shader,"roughnessFactor"); p.metalness=GetShaderLocation(p.shader,"metalnessFactor");
    p.environment=GetShaderLocation(p.shader,"useEnvironment");
    p.environment_normal=GetShaderLocation(p.shader,"useEnvironmentNormal");
    p.environment_normal_map=GetShaderLocation(p.shader,"environmentNormalMap");
    p.environment_roughness_map=GetShaderLocation(p.shader,"environmentRoughnessMap");
    p.environment_size=GetShaderLocation(p.shader,"environmentSize");
    p.environment_tile=GetShaderLocation(p.shader,"environmentTile");
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

void swat_lighting_material(SwatLighting* light,Material material,bool enabled) {
    if(!light->enabled || !light->prepared) return;
    SwatLightingProgram* p=&light->mesh;
    int zero=0; Vector2 tile={0};
    SetShaderValue(p->shader,p->environment,&zero,SHADER_UNIFORM_INT);
    SetShaderValue(p->shader,p->environment_tile,&tile,SHADER_UNIFORM_VEC2);
    int pbr=enabled && material.maps && material.maps[MATERIAL_MAP_ROUGHNESS].texture.id;
    int normal=pbr && material.maps[MATERIAL_MAP_NORMAL].texture.id;
    SetShaderValue(p->shader,p->pbr,&pbr,SHADER_UNIFORM_INT);
    SetShaderValue(p->shader,p->normal_map,&normal,SHADER_UNIFORM_INT);
    if(pbr) {
        SetShaderValue(p->shader,p->roughness,&material.maps[MATERIAL_MAP_ROUGHNESS].value,SHADER_UNIFORM_FLOAT);
        SetShaderValue(p->shader,p->metalness,&material.maps[MATERIAL_MAP_METALNESS].value,SHADER_UNIFORM_FLOAT);
    }
}

void swat_lighting_surface(SwatLighting* light,Texture2D normal,Texture2D roughness,
                           Vector3 size,Vector2 tile,bool mesh) {
    if(!light || !light->enabled || !light->prepared) return;
    if(!mesh && light->surface_normal==normal.id && light->surface_roughness==roughness.id) return;
    // Uniform/auxiliary texture changes may not alter queued geometry.
    rlDrawRenderBatchActive();
    SwatLightingProgram* p=mesh ? &light->mesh : &light->batch;
    int enabled=roughness.id!=0,use_normal=enabled && normal.id!=0,zero=0,nslot=12,rslot=13;
    SetShaderValue(p->shader,p->environment,&enabled,SHADER_UNIFORM_INT);
    SetShaderValue(p->shader,p->environment_normal,&use_normal,SHADER_UNIFORM_INT);
    SetShaderValue(p->shader,p->pbr,&zero,SHADER_UNIFORM_INT);
    SetShaderValue(p->shader,p->environment_normal_map,&nslot,SHADER_UNIFORM_INT);
    SetShaderValue(p->shader,p->environment_roughness_map,&rslot,SHADER_UNIFORM_INT);
    SetShaderValue(p->shader,p->environment_size,&size,SHADER_UNIFORM_VEC3);
    SetShaderValue(p->shader,p->environment_tile,&tile,SHADER_UNIFORM_VEC2);
    rlActiveTextureSlot(12); if(normal.id) rlEnableTexture(normal.id); else rlDisableTexture();
    rlActiveTextureSlot(13); if(roughness.id) rlEnableTexture(roughness.id); else rlDisableTexture();
    rlActiveTextureSlot(0);
    // Mesh draws share auxiliary units and invalidate a previously cached batch.
    light->surface_normal=mesh ? ~0u : normal.id;
    light->surface_roughness=mesh ? ~0u : roughness.id;
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
        int zero=0; Vector2 tile={0};
        SetShaderValue(s,p->pbr,&zero,SHADER_UNIFORM_INT);
        SetShaderValue(s,p->environment,&zero,SHADER_UNIFORM_INT);
        SetShaderValue(s,p->environment_tile,&tile,SHADER_UNIFORM_VEC2);
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
    light->surface_normal=light->surface_roughness=~0u;
    art->lit=true; art->lighting=light; art_shader(art,light->mesh.shader); BeginShaderMode(light->batch.shader);
}
void swat_lighting_end(SwatLighting* light,SwatEnvironmentArt* art) {
    if(!light->enabled || !light->prepared) return;
    EndShaderMode(); art->lit=false; art->lighting=NULL; art_shader(art,(Shader){rlGetShaderIdDefault(),rlGetShaderLocsDefault()});
    rlActiveTextureSlot(12); rlDisableTexture(); rlActiveTextureSlot(13); rlDisableTexture();
    rlActiveTextureSlot(14); rlDisableTexture(); rlActiveTextureSlot(15); rlDisableTexture(); rlActiveTextureSlot(0);
}
