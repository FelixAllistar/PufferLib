#include "lighting.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Immediate-mode vertices and normals already include rlPushMatrix transforms.
// Mesh vertices do not: separate programs prevent a second transform on batches.
static const char* vertex_source=
    "#version 330\n"
    "in vec3 vertexPosition; in vec2 vertexTexCoord,vertexTexCoord2; in vec3 vertexNormal; in vec4 vertexColor,vertexTangent;\n"
    "uniform mat4 mvp,matModel,matNormal;\n"
    "uniform int useSkinning,skinSets; uniform sampler2D skinPalette,skinInfluences;\n"
    "uniform vec3 environmentSize; uniform vec2 environmentTile;\n"
    "out vec3 position,normal; out vec2 uv,uv2; out vec4 tint,tangent;\n"
    "vec4 item(sampler2D map,int i){int w=textureSize(map,0).x; return texelFetch(map,ivec2(i%w,i/w),0);}\n"
    "mat4 bone(int i){i*=4;return mat4(item(skinPalette,i),item(skinPalette,i+1),item(skinPalette,i+2),item(skinPalette,i+3));}\n"
    "void main(){ vec3 p0=vertexPosition,n0=vertexNormal,t0=vertexTangent.xyz;"
    "if(useSkinning!=0){mat4 blend=mat4(0.0);"
    "if(skinSets==0)blend=bone(0);else for(int s=0;s<skinSets;s++){"
    "int i=(gl_VertexID*skinSets+s)*2;vec4 ids=item(skinInfluences,i),w=item(skinInfluences,i+1);"
    "for(int k=0;k<4;k++)if(w[k]>0.0)blend+=w[k]*bone(int(ids[k]));}"
    "p0=(blend*vec4(p0,1.0)).xyz;mat3 b=mat3(blend);"
    "n0=abs(determinant(b))>1e-14?normalize(transpose(inverse(b))*n0):vec3(0.0); t0=b*t0; }"
    "position=(matModel*vec4(p0,1.0)).xyz;"
    "normal=normalize((matNormal*vec4(n0,0.0)).xyz); uv=vertexTexCoord; uv2=vertexTexCoord2; tint=vertexColor;"
    "if(environmentTile.x>0.0){ vec3 p=vertexPosition*environmentSize;"
    "vec3 a=abs(vertexNormal); uv=(a.y>max(a.x,a.z)?vec2(p.x,p.z):vec2(a.x>=a.z?p.z:p.x,p.y))/environmentTile; }"
    "tangent=vec4((matModel*vec4(t0,0.0)).xyz,vertexTangent.w);"
    "gl_Position=mvp*vec4(p0,1.0); }\n";
// Shared linear daylight environment: the visible sky and reflected radiance
// agree. Roughness blends toward its broad irradiance approximation.
#define SWAT_SKY_GLSL \
    "vec3 skyRadiance(vec3 d,float r){float h=clamp(d.y,0.0,1.0);" \
    "vec3 sky=mix(vec3(0.62,0.68,0.73),vec3(0.25,0.43,0.66),pow(h,0.45));" \
    "float clouds=pow(max(0.0,sin(d.x*9.0+d.z*3.0)*sin(d.z*11.0-d.x*2.0)),4.0);" \
    "sky=mix(sky,vec3(0.76,0.78,0.79),clouds*(1.0-r)*(1.0-r)*smoothstep(0.02,0.28,h)*0.65);" \
    "sky=mix(vec3(0.11,0.105,0.09),sky,smoothstep(-0.06,0.03,d.y));" \
    "return mix(sky,vec3(0.32,0.39,0.46),r*r*0.65);}" \
    "vec3 tone(vec3 x){return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0);}" \
    "\n"
static const char* fragment_source=
    "#version 330\n"
    "in vec3 position,normal; in vec2 uv,uv2; in vec4 tint,tangent; out vec4 finalColor;\n"
    "uniform sampler2D texture0,sunMap,lampMap; uniform vec4 colDiffuse;\n"
    "uniform sampler2D normalMap,ormMap,specularMap; uniform int usePbr,useNormal,useOrm,useSpecGloss; uniform float roughnessFactor,metalnessFactor,normalGreen,normalScale;\n"
    "uniform sampler2D emissionMap; uniform int useEmission;\n"
    "uniform sampler2D environmentNormalMap,environmentRoughnessMap; uniform int useEnvironment,useEnvironmentNormal;\n"
    "uniform mat4 sunMatrix,lampMatrix[6]; uniform vec3 camera;\n"
    "uniform sampler2D iblAtlas,occlusionMap,contactMap,contactDepth; uniform int useIbl,useOcclusion,useContact;"
    "uniform int occlusionUV;uniform float occlusionStrength;uniform vec3 sunDirection,sunEnergy;uniform mat4 contactMatrix;\n"
    "uniform int lampShadows;uniform int rooms; uniform vec3 centers[8],halves[8],origins[8]; uniform vec3 roomDirection[8];uniform float roomPower[8]; uniform float exposure;\n"
    "float visible(sampler2D map,mat4 matrix,vec3 point,vec3 n,vec3 l,int tile){"
    "vec4 clip=matrix*vec4(point,1.0); if(clip.w<=0.0)return 1.0;"
    "vec3 p=clip.xyz/clip.w*0.5+0.5;"
    // Compare at the sampled texel's plane depth. A fixed bias alone produces
    // acne on large/sloping surfaces as the orthographic map covers more area.
    "vec4 cx=matrix*vec4(dFdx(point),0.0),cy=matrix*vec4(dFdy(point),0.0);"
    "vec3 px=(cx.xyz-clip.xyz/clip.w*cx.w)/clip.w*0.5,py=(cy.xyz-clip.xyz/clip.w*cy.w)/clip.w*0.5;"
    "vec2 dx=px.xy,dy=py.xy;float det=dx.x*dy.y-dx.y*dy.x;"
    "vec2 gradient=abs(det)>1e-12?vec2(dy.y*px.z-dx.y*py.z,dx.x*py.z-dy.x*px.z)/det:vec2(0.0);"
    "if(any(lessThan(p,vec3(0.0)))||any(greaterThan(p,vec3(1.0))))return 1.0;"
    "vec2 lower=vec2(0.0),upper=vec2(1.0);"
    "if(tile>=0){vec2 scale=vec2(1.0/6.0,1.0/8.0);lower=vec2(tile%6,tile/6)*scale;upper=lower+scale;"
    "p.xy=lower+p.xy*scale;gradient/=scale;}"
    "float bias=max(0.000025,0.00005*(1.0-max(dot(n,l),0.0)));"
    // Continuous tent weights avoid the blocky jumps of equal-weight snapped
    // PCF as an edge or camera crosses a shadow-map texel.
    "vec2 pixel=1.0/vec2(textureSize(map,0)),texel=p.xy/pixel-0.5,base=floor(texel),fraction=fract(texel); float v=0.0;"
    "for(int x=-1;x<=2;x++)for(int y=-1;y<=2;y++){"
    "vec2 offset=vec2(x,y),sampleUV=(base+offset+0.5)*pixel;"
    "vec2 weight=2.0-abs(offset-fraction);"
    "sampleUV=clamp(sampleUV,lower+pixel*0.5,upper-pixel*0.5);"
    "v+=weight.x*weight.y*step(p.z+dot(gradient,sampleUV-p.xy)-bias,texture(map,sampleUV).r); } return v/16.0; }\n"
    SWAT_SKY_GLSL
    "vec2 envUV(vec3 d){return vec2(atan(d.z,d.x)/6.2831853+0.5,acos(clamp(d.y,-1.0,1.0))/3.14159265); }"
    "vec3 atlas(vec2 uv,float layer){return texture(iblAtlas,vec2(uv.x,(layer+clamp(uv.y,0.00390625,0.99609375))/8.0)).rgb;}"
    "vec3 filteredSky(vec3 d,float r){float lod=r*5.0;return mix(atlas(envUV(d),floor(lod)),atlas(envUV(d),min(5.0,floor(lod)+1.0)),fract(lod));}"
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
    "if(usePbr!=0){ vec4 orm=useOrm!=0?texture(ormMap,uv):vec4(1.0); roughness=clamp(orm.g*roughnessFactor,0.08,1.0);"
    "metalness=clamp(orm.b*metalnessFactor,0.0,1.0); f0=mix(vec3(0.04),albedo,metalness);"
    "if(useSpecGloss!=0){roughness=clamp(1.0-orm.r,0.08,1.0);metalness=0.0;"
    "f0=pow(clamp(texture(specularMap,uv).rgb,0.0,1.0),vec3(2.2));}"
    "if(useNormal!=0){ vec3 t,b;"
    "if(useNormal==2){vec3 dx=dFdx(position),dy=dFdy(position);vec2 tx=dFdx(uv),ty=dFdy(uv);"
    "float det=tx.x*ty.y-tx.y*ty.x; t=abs(det)>1e-12?(dx*ty.y-dy*tx.y)/det:vec3(0.0);"
    "b=abs(det)>1e-12?(dy*tx.x-dx*ty.x)/det:vec3(0.0);t-=n*dot(n,t);"
    "if(dot(t,t)>1e-12){t=normalize(t);b=cross(n,t)*(dot(cross(n,t),b)<0.0?-1.0:1.0);}"
    "else {t=vec3(0.0);b=vec3(0.0);} }"
    "else {t=normalize(tangent.xyz-n*dot(n,tangent.xyz));b=cross(n,t)*tangent.w;}"
    "if(dot(t,t)>1e-12){vec3 mapped=texture(normalMap,uv).xyz*2.0-1.0;mapped.xy*=normalScale;mapped.y*=normalGreen;n=normalize(mat3(t,b,n)*mapped);} } }"
    "vec3 sun=sunDirection;"
    "float sunVisibility=visible(sunMap,sunMatrix,position,n,sun,-1); float direct=max(dot(n,sun),0.0)*sunVisibility;"
    "vec3 illumination=mix(vec3(0.11,0.105,0.09),vec3(0.30,0.37,0.46),n.y*0.5+0.5);"
    "vec3 reflectedDirection=reflect(-v,n),environment=skyRadiance(reflectedDirection,roughness);"
    "float contact=1.0;bool insideRoom=false;"
    "vec3 punctual=sunEnergy*direct;"
    "if(usePbr!=0||useEnvironment!=0)reflection+=sunEnergy*sunVisibility*specular(n,v,sun,f0,roughness);"
    "for(int i=0;i<rooms;i++){ vec3 delta=position-centers[i];"
    "if(abs(delta.x)>halves[i].x+0.12||abs(delta.z)>halves[i].z+0.12||abs(delta.y)>halves[i].y+0.15)continue;"
    "insideRoom=true;"
    "vec3 gap=max(halves[i]-abs(delta),vec3(0.0));"
    // A bounded room approximation darkens ambient near adjacent planes only.
    // The surface's own plane does not occlude itself; punctual lights retain
    // their shadow-map visibility instead of receiving an invented AO shadow.
    "vec3 edge=mix(vec3(0.55),vec3(1.0),smoothstep(vec3(0.0),vec3(0.45),gap));"
    "vec3 axis=abs(normalize(normal));contact=dot(axis,vec3(edge.y*edge.z,edge.x*edge.z,edge.x*edge.y))/(axis.x+axis.y+axis.z);"
    "illumination=mix(vec3(0.06,0.058,0.055),vec3(0.145,0.145,0.14),n.y*0.5+0.5);"
    "environment=mix(vec3(0.075,0.068,0.055),vec3(0.32,0.30,0.26),reflectedDirection.y*0.5+0.5);"
    "vec3 origin=origins[i]; vec3 toLight=origin-position;"
    "float d2=dot(toLight,toLight); vec3 l=normalize(toLight);"
    "vec3 q=-toLight,a=abs(q);int face=a.x>=a.y&&a.x>=a.z?(q.x>=0.0?0:1):(a.y>=a.z?(q.y>=0.0?2:3):(q.z>=0.0?4:5));"
    "float visibility=lampShadows!=0?visible(lampMap,lampMatrix[face],q,n,l,i*6+face):1.0;"
    "float beam=dot(roomDirection[i],roomDirection[i])>0.5?smoothstep(0.05,0.35,dot(-l,roomDirection[i])):1.0;"
    "vec3 radiance=vec3(1.0,0.92,0.80)*visibility*roomPower[i]*beam*1.8/(1.0+0.18*d2);"
    "punctual+=radiance*max(dot(n,l),0.0); if(usePbr!=0||useEnvironment!=0)reflection+=radiance*specular(n,v,l,f0,roughness); }"
    "if(useIbl!=0&&!insideRoom){illumination=atlas(envUV(n),6.0);environment=filteredSky(reflectedDirection,roughness);}"
    // Roughness-aware grazing reflection for painted surfaces as well as metal.
    // This analytic environment is deliberately bounded; it is not a scene probe.
    "float nv=max(dot(n,v),0.0); vec3 fresnel=f0+(max(vec3(1.0-roughness),f0)-f0)*pow(1.0-nv,5.0);"
    "vec3 indirect=(usePbr!=0||useEnvironment!=0)?environment*fresnel:vec3(0.0);"
    "if(useIbl!=0&&(usePbr!=0||useEnvironment!=0)){vec2 brdf=atlas(vec2(clamp(nv,0.001953125,0.998046875),roughness),7.0).rg;indirect=environment*(f0*brdf.x+brdf.y);}"
    "if(useOcclusion!=0)contact*=mix(1.0,texture(occlusionMap,occlusionUV==1?uv2:uv).r,occlusionStrength);"
    "if(useContact!=0){vec4 p=contactMatrix*vec4(position,1.0);vec3 q=p.xyz/p.w*0.5+0.5;"
    "if(p.w>0.0&&all(greaterThanEqual(q.xy,vec2(0.0)))&&all(lessThanEqual(q.xy,vec2(1.0)))){"
    "float tolerance=max(0.00003,2.0*(abs(dFdx(q.z))+abs(dFdy(q.z))));"
    "if(abs(texture(contactDepth,q.xy).r-q.z)<tolerance)contact*=texture(contactMap,q.xy).r;}}"
    "vec3 emission=useEmission!=0?pow(max(texture(emissionMap,uv).rgb,vec3(0.0)),vec3(2.2)):vec3(0.0);"
    "float diffuseEnergy=useSpecGloss!=0?1.0-max(f0.r,max(f0.g,f0.b)):1.0-metalness;"
    "vec3 color=tone((albedo*(illumination*contact+punctual)*diffuseEnergy+reflection+indirect*contact+emission)*exposure);"
    "finalColor=vec4(pow(color,vec3(1.0/2.2)),surface.a); }\n";

static SwatLightingProgram program(void) {
    SwatLightingProgram p={0}; p.shader=LoadShaderFromMemory(vertex_source,fragment_source);
    if(p.shader.id==rlGetShaderIdDefault()) return p;
    p.shader.locs[SHADER_LOC_MATRIX_MODEL]=GetShaderLocation(p.shader,"matModel");
    p.shader.locs[SHADER_LOC_MATRIX_NORMAL]=GetShaderLocation(p.shader,"matNormal");
    p.occlusion_uv=GetShaderLocation(p.shader,"occlusionUV");
    p.shader.locs[SHADER_LOC_VERTEX_TEXCOORD02]=GetShaderLocationAttrib(p.shader,"vertexTexCoord2");
    p.shader.locs[SHADER_LOC_VERTEX_TANGENT]=GetShaderLocationAttrib(p.shader,"vertexTangent");
    p.shader.locs[SHADER_LOC_MAP_SPECULAR]=GetShaderLocation(p.shader,"specularMap");
    p.spec_gloss=GetShaderLocation(p.shader,"useSpecGloss");
    p.shader.locs[SHADER_LOC_MAP_NORMAL]=GetShaderLocation(p.shader,"normalMap");
    p.shader.locs[SHADER_LOC_MAP_ROUGHNESS]=GetShaderLocation(p.shader,"ormMap");
    p.shader.locs[SHADER_LOC_MAP_EMISSION]=GetShaderLocation(p.shader,"emissionMap");
    p.skinning=GetShaderLocation(p.shader,"useSkinning"); p.skin_sets=GetShaderLocation(p.shader,"skinSets");
    p.skin_palette=GetShaderLocation(p.shader,"skinPalette"); p.skin_influences=GetShaderLocation(p.shader,"skinInfluences");
    p.emission=GetShaderLocation(p.shader,"useEmission");
    p.normal_green=GetShaderLocation(p.shader,"normalGreen");
    p.normal_scale=GetShaderLocation(p.shader,"normalScale");
    p.orm=GetShaderLocation(p.shader,"useOrm"); p.pbr=GetShaderLocation(p.shader,"usePbr"); p.normal_map=GetShaderLocation(p.shader,"useNormal");
    p.roughness=GetShaderLocation(p.shader,"roughnessFactor"); p.metalness=GetShaderLocation(p.shader,"metalnessFactor");
    p.environment=GetShaderLocation(p.shader,"useEnvironment");
    p.environment_normal=GetShaderLocation(p.shader,"useEnvironmentNormal");
    p.environment_normal_map=GetShaderLocation(p.shader,"environmentNormalMap");
    p.environment_roughness_map=GetShaderLocation(p.shader,"environmentRoughnessMap");
    p.environment_size=GetShaderLocation(p.shader,"environmentSize");
    p.environment_tile=GetShaderLocation(p.shader,"environmentTile");
    p.camera=GetShaderLocation(p.shader,"camera");
    p.sun_matrix=GetShaderLocation(p.shader,"sunMatrix");
    for(int i=0;i<SWAT_LAMP_FACES;i++) {char name[32];snprintf(name,sizeof(name),"lampMatrix[%d]",i);p.lamp_matrix[i]=GetShaderLocation(p.shader,name);}
    p.lamp_shadows=GetShaderLocation(p.shader,"lampShadows");
    p.sun_map=GetShaderLocation(p.shader,"sunMap"); p.lamp_map=GetShaderLocation(p.shader,"lampMap");
    p.rooms=GetShaderLocation(p.shader,"rooms"); p.centers=GetShaderLocation(p.shader,"centers[0]");
    p.halves=GetShaderLocation(p.shader,"halves[0]"); p.origins=GetShaderLocation(p.shader,"origins[0]"); p.room_power=GetShaderLocation(p.shader,"roomPower[0]");p.room_direction=GetShaderLocation(p.shader,"roomDirection[0]");
    p.exposure=GetShaderLocation(p.shader,"exposure");
    p.ibl=GetShaderLocation(p.shader,"useIbl");p.ibl_atlas=GetShaderLocation(p.shader,"iblAtlas");
    p.sun_direction=GetShaderLocation(p.shader,"sunDirection");p.sun_energy=GetShaderLocation(p.shader,"sunEnergy");
    p.occlusion=GetShaderLocation(p.shader,"useOcclusion");p.occlusion_strength=GetShaderLocation(p.shader,"occlusionStrength");
    p.shader.locs[SHADER_LOC_MAP_OCCLUSION]=GetShaderLocation(p.shader,"occlusionMap");
    p.contact=GetShaderLocation(p.shader,"useContact");p.contact_map=GetShaderLocation(p.shader,"contactMap");
    p.contact_matrix=GetShaderLocation(p.shader,"contactMatrix");p.contact_depth_map=GetShaderLocation(p.shader,"contactDepth");return p;
}

Shader swat_lighting_skin_shader(void) {
    const char* fragment="#version 330\n in vec2 uv,uv2; in vec4 tint; uniform sampler2D texture0; uniform vec4 colDiffuse; out vec4 finalColor; void main(){finalColor=texture(texture0,uv)*tint*colDiffuse;}";
    Shader shader=LoadShaderFromMemory(vertex_source,fragment);
    shader.locs[SHADER_LOC_MATRIX_MODEL]=GetShaderLocation(shader,"matModel");
    shader.locs[SHADER_LOC_MATRIX_NORMAL]=GetShaderLocation(shader,"matNormal");
    return shader;
}

static RenderTexture2D depth_target_size(int width,int height) {
    RenderTexture2D target={0}; target.id=rlLoadFramebuffer();
    if(!target.id) return target;
    target.texture.width=width;target.texture.height=height;
    target.depth=(Texture2D){rlLoadTextureDepth(width,height,false),width,height,1,19};
    rlFramebufferAttach(target.id,target.depth.id,RL_ATTACHMENT_DEPTH,RL_ATTACHMENT_TEXTURE2D,0);
    if(!target.depth.id || !rlFramebufferComplete(target.id)) {
        rlUnloadFramebuffer(target.id); target=(RenderTexture2D){0};
    } else {
        SetTextureFilter(target.depth,TEXTURE_FILTER_POINT);
        SetTextureWrap(target.depth,TEXTURE_WRAP_CLAMP);
    }
    rlDisableFramebuffer(); return target;
}
static RenderTexture2D depth_target(int size) { return depth_target_size(size,size); }

static void environment_load(SwatLighting* light) {
    const char* enabled=getenv("SWAT_IBL");if(enabled && !strcmp(enabled,"0"))return;
    char path[4096];const char* custom=getenv("SWAT_ENVIRONMENT_ASSETS");
    if(custom && *custom)snprintf(path,sizeof(path),"%s/lighting_v1/daylight.bin",custom);
    else {
        snprintf(path,sizeof(path),"%sassets/environment/lighting_v1/daylight.bin",GetApplicationDirectory());
        if(!FileExists(path))snprintf(path,sizeof(path),"ocean/swat/assets/environment/lighting_v1/daylight.bin");
    }
    FILE* file=fopen(path,"rb");if(!file)return;
    uint32_t header[6]={0};float scale=0;Vector3 sun={0},energy={0};
    bool valid=fread(header,sizeof(header),1,file)==1 && header[0]==0x31424953u &&
        header[1]==256 && header[2]==128 && header[3]==6 && header[4]==1024 && header[5]==512 &&
        fread(&scale,sizeof(scale),1,file)==1 && isfinite(scale) && scale>0 &&
        fread(&sun,sizeof(sun),1,file)==1 && fread(&energy,sizeof(energy),1,file)==1;
    const size_t atlas_count=256*128*8,sky_count=1024*512;
    Vector3* pixels=valid ? malloc((atlas_count+sky_count)*sizeof(Vector3)) : NULL;
    valid=valid && pixels && fread(pixels,sizeof(Vector3),atlas_count+sky_count,file)==atlas_count+sky_count && fgetc(file)==EOF;
    fclose(file);
    if(valid) {
        // Reject corrupt non-finite data before sending it to the driver.
        const float* values=(float*)pixels;
        for(size_t i=0;i<(atlas_count+sky_count)*3;i++)if(!isfinite(values[i]) || values[i]<0) { valid=false;break; }
    }
    if(valid && isfinite(sun.x) && isfinite(sun.y) && isfinite(sun.z) &&
        Vector3Length(sun)>.99f && Vector3Length(sun)<1.01f &&
        isfinite(energy.x) && isfinite(energy.y) && isfinite(energy.z) && energy.x>=0 && energy.y>=0 && energy.z>=0) {
        light->environment_atlas=LoadTextureFromImage((Image){pixels,256,128*8,1,PIXELFORMAT_UNCOMPRESSED_R32G32B32});
        light->environment_sky=LoadTextureFromImage((Image){pixels+atlas_count,1024,512,1,PIXELFORMAT_UNCOMPRESSED_R32G32B32});
        if(light->environment_atlas.id && light->environment_sky.id) {
            light->environment_scale=scale;light->sun_direction=sun;light->sun_energy=energy;
            SetTextureFilter(light->environment_atlas,TEXTURE_FILTER_BILINEAR);
            SetTextureFilter(light->environment_sky,TEXTURE_FILTER_BILINEAR);
            // Longitude repeats; latitude clamps at poles/atlas edges.
            rlTextureParameters(light->environment_atlas.id,RL_TEXTURE_WRAP_S,RL_TEXTURE_WRAP_REPEAT);
            rlTextureParameters(light->environment_atlas.id,RL_TEXTURE_WRAP_T,RL_TEXTURE_WRAP_CLAMP);
            rlTextureParameters(light->environment_sky.id,RL_TEXTURE_WRAP_S,RL_TEXTURE_WRAP_REPEAT);
            rlTextureParameters(light->environment_sky.id,RL_TEXTURE_WRAP_T,RL_TEXTURE_WRAP_CLAMP);
            TraceLog(LOG_INFO,"SWAT: CC0 HDR daylight, convolved diffuse / GGX reflections");
        } else {
            if(light->environment_atlas.id)UnloadTexture(light->environment_atlas);
            if(light->environment_sky.id)UnloadTexture(light->environment_sky);
            light->environment_atlas=light->environment_sky=(Texture2D){0};
        }
    }
    free(pixels);
}

static const char* contact_fragment=
    "#version 330\n uniform sampler2D texture0;uniform mat4 inverseProjection,projection;uniform vec2 size;out vec4 finalColor;\n"
    "vec3 point(vec2 uv){float z=texture(texture0,uv).r;vec4 p=inverseProjection*vec4(uv*2.0-1.0,z*2.0-1.0,1.0);return p.xyz/p.w;}"
    "void main(){vec2 uv=gl_FragCoord.xy/size,px=1.0/size;float depth=texture(texture0,uv).r;"
    "if(depth>=0.99999){finalColor=vec4(1.0);return;}vec3 p=point(uv);"
    // Use the nearer difference at silhouettes to avoid normals spanning rooms.
    "vec3 r=point(uv+vec2(px.x,0.0))-p,l=p-point(uv-vec2(px.x,0.0));"
    "vec3 u=point(uv+vec2(0.0,px.y))-p,d=p-point(uv-vec2(0.0,px.y));"
    "vec3 n=normalize(cross(abs(r.z)<abs(l.z)?r:l,abs(u.z)<abs(d.z)?u:d));"
    "if(dot(n,-p)<0.0)n=-n;float radius=0.28,occ=0.0;"
    "float pixels=clamp(radius*abs(projection[1][1])*size.y*0.5/max(-p.z,0.1),2.0,60.0);"
    "for(int i=0;i<8;i++){float angle=(float(i)+0.5)*0.785398163;vec2 dir=vec2(cos(angle),sin(angle));float horizon=0.0;"
    "for(int j=1;j<=4;j++){vec2 q=uv+dir*px*pixels*float(j)/4.0;"
    "if(any(lessThan(q,vec2(0.0)))||any(greaterThan(q,vec2(1.0))))continue;"
    "vec3 delta=point(q)-p;float distance=length(delta);"
    "if(distance>0.01&&distance<radius)horizon=max(horizon,max(0.0,dot(n,delta/distance)-0.08)*(1.0-distance/radius));}occ+=horizon;}"
    "float ao=clamp(1.0-occ*0.16,0.65,1.0);finalColor=vec4(ao,ao,ao,1.0);}";

void swat_lighting_contact(SwatLighting* light,const SwatSim* sim,Camera3D camera,int width,int height,
                           SwatShadowSceneContext draw,void* context) {
    light->contact_ready=false;
    if(!light->enabled || !light->contact_enabled || camera.projection!=CAMERA_PERSPECTIVE || width<2 || height<2)return;
    rlDrawRenderBatchActive();unsigned int previous=rlGetActiveFramebuffer();
    Matrix previous_projection=rlGetMatrixProjection(),previous_view=rlGetMatrixModelview();
    int w=(width+1)/2,h=(height+1)/2;
    if(light->contact_depth.id && (light->contact_depth.depth.width!=w || light->contact_depth.depth.height!=h)) {
        rlUnloadFramebuffer(light->contact_depth.id);light->contact_depth=(RenderTexture2D){0};
        UnloadRenderTexture(light->contact_ao);light->contact_ao=(RenderTexture2D){0};
    }
    if(!light->contact_depth.id) {
        RenderTexture2D* target=&light->contact_depth;target->id=rlLoadFramebuffer();
        target->texture.width=w;target->texture.height=h;
        target->depth=(Texture2D){rlLoadTextureDepth(w,h,false),w,h,1,19};
        rlFramebufferAttach(target->id,target->depth.id,RL_ATTACHMENT_DEPTH,RL_ATTACHMENT_TEXTURE2D,0);
        if(!target->depth.id || !rlFramebufferComplete(target->id)) {rlUnloadFramebuffer(target->id);*target=(RenderTexture2D){0};goto restore;}
        SetTextureFilter(target->depth,TEXTURE_FILTER_POINT);SetTextureWrap(target->depth,TEXTURE_WRAP_CLAMP);
        light->contact_ao=LoadRenderTexture(w,h);
        SetTextureFilter(light->contact_ao.texture,TEXTURE_FILTER_BILINEAR);
    }
    if(!light->contact_depth.id || !light->contact_ao.id)goto restore;
    light->contact_eye=camera.position;
    BeginTextureMode(light->contact_depth);ClearBackground(WHITE);BeginMode3D(camera);
    Matrix projection=rlGetMatrixProjection(),inverse=MatrixInvert(projection);
    light->contact_matrix=MatrixMultiply(rlGetMatrixModelview(),projection);
    draw(context,sim,false);EndMode3D();EndTextureMode();
    Vector2 size={(float)w,(float)h};
    SetShaderValueMatrix(light->contact_shader,light->contact_projection,projection);
    SetShaderValueMatrix(light->contact_shader,light->contact_inverse,inverse);
    SetShaderValue(light->contact_shader,light->contact_size,&size,SHADER_UNIFORM_VEC2);
    BeginTextureMode(light->contact_ao);ClearBackground(WHITE);BeginShaderMode(light->contact_shader);
    DrawTexturePro(light->contact_depth.depth,(Rectangle){0,0,w,h},(Rectangle){0,0,w,h},(Vector2){0},0,WHITE);
    EndShaderMode();EndTextureMode();
    light->contact_ready=true;
restore:
    rlEnableFramebuffer(previous);rlViewport(0,0,width,height);
    rlSetMatrixProjection(previous_projection);rlSetMatrixModelview(previous_view);
}

void swat_lighting_init(SwatLighting* light) {
    if(light->initialized) return;
    light->initialized=true; light->exposure=1.1f; light->shadow_room=-1;
    light->sun_direction=Vector3Normalize((Vector3){-.45f,.82f,-.35f});light->sun_energy=(Vector3){.95f,.88f,.76f};
    const char* mode=getenv("SWAT_LIGHTING"),*exposure=getenv("SWAT_EXPOSURE");
    if(mode && !strcmp(mode,"0")) return;
    if(exposure) { char* end; float v=strtof(exposure,&end); if(end!=exposure && !*end && isfinite(v)) light->exposure=swat_clamp(v,.25f,3); }
    light->batch=program(); light->mesh=program();
    environment_load(light);
    const char* sky_fragment="#version 330\n in vec2 fragTexCoord; out vec4 finalColor; uniform vec3 skyForward,skyRight,skyUp; uniform vec2 skyScale,skySize; uniform float exposure;\n"
        SWAT_SKY_GLSL
        "uniform sampler2D skyMap;uniform int useIbl;uniform float skyMapScale;"
        "void main(){vec2 p=(gl_FragCoord.xy/skySize*2.0-1.0)*skyScale;vec3 d=normalize(skyForward+p.x*skyRight+p.y*skyUp);"
        "vec3 radiance=skyRadiance(d,0.0);if(useIbl!=0)radiance=texture(skyMap,vec2(atan(d.z,d.x)/6.2831853+0.5,acos(clamp(d.y,-1.0,1.0))/3.14159265)).rgb*skyMapScale;"
        "finalColor=vec4(pow(tone(radiance*exposure),vec3(1.0/2.2)),1.0);}";
    light->sky=LoadShaderFromMemory(NULL,sky_fragment);
    light->sky_forward=GetShaderLocation(light->sky,"skyForward"); light->sky_right=GetShaderLocation(light->sky,"skyRight");
    light->sky_up=GetShaderLocation(light->sky,"skyUp"); light->sky_scale=GetShaderLocation(light->sky,"skyScale");
    light->sky_size=GetShaderLocation(light->sky,"skySize");
    light->sky_exposure=GetShaderLocation(light->sky,"exposure");
    light->sky_map=GetShaderLocation(light->sky,"skyMap");light->sky_ibl=GetShaderLocation(light->sky,"useIbl");
    light->sky_map_scale=GetShaderLocation(light->sky,"skyMapScale");
    light->lamp_shadows=true;
    light->sun=depth_target(1536);
    light->lamp=depth_target_size(SWAT_LAMP_SIZE*SWAT_LAMP_FACES,SWAT_LAMP_SIZE*SWAT_MAX_ROOMS);light->lamp_static=depth_target_size(SWAT_LAMP_SIZE*SWAT_LAMP_FACES,SWAT_LAMP_SIZE*SWAT_MAX_ROOMS);
    const char* contact=getenv("SWAT_CONTACT_SHADOWS");
    // Keep the extra geometry pass opt-in until its cost is lower on the 1060.
    if(contact && strcmp(contact,"0")) {
        light->contact_shader=LoadShaderFromMemory(NULL,contact_fragment);
        light->contact_enabled=light->contact_shader.id && light->contact_shader.id!=rlGetShaderIdDefault();
        light->contact_inverse=GetShaderLocation(light->contact_shader,"inverseProjection");
        light->contact_projection=GetShaderLocation(light->contact_shader,"projection");
        light->contact_size=GetShaderLocation(light->contact_shader,"size");
    }
    light->enabled=light->sun.id && light->lamp.id && light->lamp_static.id && light->batch.shader.id!=rlGetShaderIdDefault() &&
        light->mesh.shader.id!=rlGetShaderIdDefault();
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

static void mesh_uniform(int location,const void* value,int type) {
    if(location>=0)rlSetUniform(location,value,type,1);
}
void swat_lighting_material_uv(SwatLighting* light,Material material,bool enabled,float normal_scale,int occlusion_uv) {
    if(!light || !light->enabled || !light->prepared) return;
    SwatLightingProgram* p=&light->mesh;
    int zero=0; Vector2 tile={0};
    // Bind once for the material; Raylib's single-value helper binds again for
    // every uniform, even though all of these values use the same program.
    rlEnableShader(p->shader.id);
    mesh_uniform(p->environment,&zero,SHADER_UNIFORM_INT);
    mesh_uniform(p->environment_tile,&tile,SHADER_UNIFORM_VEC2);
    int orm=enabled && material.maps && material.maps[MATERIAL_MAP_ROUGHNESS].texture.id;
    int spec_gloss=enabled && material.maps && material.maps[MATERIAL_MAP_SPECULAR].texture.id &&
        material.maps[MATERIAL_MAP_ROUGHNESS].texture.id && material.maps[MATERIAL_MAP_ROUGHNESS].value<0;
    int pbr=enabled && material.maps && (orm || material.maps[MATERIAL_MAP_ROUGHNESS].value>0);
    mesh_uniform(p->spec_gloss,&spec_gloss,SHADER_UNIFORM_INT);
    mesh_uniform(p->orm,&orm,SHADER_UNIFORM_INT);
    int normal=pbr && material.maps[MATERIAL_MAP_NORMAL].texture.id;
    if(normal && fabsf(material.maps[MATERIAL_MAP_NORMAL].value)==2) normal=2;
    mesh_uniform(p->pbr,&pbr,SHADER_UNIFORM_INT);
    mesh_uniform(p->normal_map,&normal,SHADER_UNIFORM_INT);
    float green=normal && material.maps[MATERIAL_MAP_NORMAL].value<0 ? -1 : 1;
    mesh_uniform(p->normal_green,&green,SHADER_UNIFORM_FLOAT);
    mesh_uniform(p->normal_scale,&normal_scale,SHADER_UNIFORM_FLOAT);
    int emission=enabled && material.maps && material.maps[MATERIAL_MAP_EMISSION].texture.id;
    mesh_uniform(p->emission,&emission,SHADER_UNIFORM_INT);
    int occlusion=enabled && material.maps && material.maps[MATERIAL_MAP_OCCLUSION].texture.id;
    float strength=occlusion?material.maps[MATERIAL_MAP_OCCLUSION].value:1;
    mesh_uniform(p->occlusion,&occlusion,SHADER_UNIFORM_INT);
    mesh_uniform(p->occlusion_strength,&strength,SHADER_UNIFORM_FLOAT);
    mesh_uniform(p->occlusion_uv,&occlusion_uv,SHADER_UNIFORM_INT);
    mesh_uniform(p->skinning,&zero,SHADER_UNIFORM_INT);
    if(pbr) {
        mesh_uniform(p->roughness,&material.maps[MATERIAL_MAP_ROUGHNESS].value,SHADER_UNIFORM_FLOAT);
        mesh_uniform(p->metalness,&material.maps[MATERIAL_MAP_METALNESS].value,SHADER_UNIFORM_FLOAT);
    }
}

void swat_lighting_material_scaled(SwatLighting* light,Material material,bool enabled,float normal_scale) {
    swat_lighting_material_uv(light,material,enabled,normal_scale,0);
}
void swat_lighting_material(SwatLighting* light,Material material,bool enabled) {
    swat_lighting_material_scaled(light,material,enabled,1);
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
    SetShaderValue(p->shader,p->spec_gloss,&zero,SHADER_UNIFORM_INT);
    SetShaderValue(p->shader,p->occlusion,&zero,SHADER_UNIFORM_INT);
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
    if(light->lamp_static.id) rlUnloadFramebuffer(light->lamp_static.id);
    if(light->batch.shader.id && light->batch.shader.id!=rlGetShaderIdDefault()) UnloadShader(light->batch.shader);
    if(light->mesh.shader.id && light->mesh.shader.id!=rlGetShaderIdDefault()) UnloadShader(light->mesh.shader);
    if(light->sky.id && light->sky.id!=rlGetShaderIdDefault()) UnloadShader(light->sky);
    if(light->environment_atlas.id)UnloadTexture(light->environment_atlas);
    if(light->environment_sky.id)UnloadTexture(light->environment_sky);
    if(light->contact_depth.id)rlUnloadFramebuffer(light->contact_depth.id);
    if(light->contact_ao.id)UnloadRenderTexture(light->contact_ao);
    if(light->contact_shader.id && light->contact_shader.id!=rlGetShaderIdDefault())UnloadShader(light->contact_shader);
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

// Two guard texels on each edge keep the PCF footprint inside each face.
static const Vector3 lamp_dir[6]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
static const Vector3 lamp_up[6]={{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
static float lamp_tan(void){return 1+4.0f/SWAT_LAMP_SIZE;}
bool swat_lighting_face_intersects(const SwatLighting* light,Vector3 center,Vector3 half) {
    if(light->shadow_room<0)return true;
    int face=light->shadow_face;Vector3 d=Vector3Subtract(center,light->shadow_origin);
    float radius=Vector3Length(half)+.5f,z=Vector3DotProduct(d,lamp_dir[face]),t=lamp_tan();
    float edge=z*t+radius*sqrtf(1+t*t);
    return z+radius>=.05f && fabsf(Vector3DotProduct(d,lamp_up[face]))<=edge &&
        fabsf(Vector3DotProduct(d,Vector3CrossProduct(lamp_dir[face],lamp_up[face])))<=edge;
}
static Matrix shadow(RenderTexture2D target,Camera3D camera,const SwatSim* sim,bool cutaway,SwatShadowScene draw,SwatShadowSceneContext contextual,SwatShadowSceneContext actors,void* context,int tile,bool clear) {
    double near=rlGetCullDistanceNear(),far=rlGetCullDistanceFar(); rlSetClipPlanes(.05,120);
    if(tile>=0)target.texture.width=target.texture.height=SWAT_LAMP_SIZE; // Square per-room projection.
    BeginTextureMode(target);
    if(tile>=0) {rlViewport(tile%SWAT_LAMP_FACES*SWAT_LAMP_SIZE,tile/SWAT_LAMP_FACES*SWAT_LAMP_SIZE,SWAT_LAMP_SIZE,SWAT_LAMP_SIZE);rlEnableScissorTest();rlScissor(tile%SWAT_LAMP_FACES*SWAT_LAMP_SIZE,tile/SWAT_LAMP_FACES*SWAT_LAMP_SIZE,SWAT_LAMP_SIZE,SWAT_LAMP_SIZE);}
    if(clear)ClearBackground(WHITE);
    BeginMode3D(camera);
    Matrix matrix=MatrixMultiply(rlGetMatrixModelview(),rlGetMatrixProjection());
    if(contextual) contextual(context,sim,cutaway); else if(draw)draw(sim,cutaway);
    if(actors)actors(context,sim,cutaway);
    EndMode3D();if(tile>=0)rlDisableScissorTest();EndTextureMode();rlSetClipPlanes(near,far);return matrix;
}
bool swat_lighting_room_intersects(const SwatRoom* room,Vector3 center,Vector3 half) {
    // Conservative sphere includes rotating leaves, limbs and authored trim.
    float radius=Vector3Length(half)+.5f;
    float x=fmaxf(0,fabsf(center.x-room->center.x)-room->half.x);
    float y=fmaxf(0,fabsf(center.y-room->center.y)-room->half.y);
    float z=fmaxf(0,fabsf(center.z-room->center.z)-room->half.z);
    return x*x+y*y+z*z<=radius*radius;
}
static uint32_t room_geometry_hash(const SwatWorld* world,int room) {
    uint32_t hash=hash_bytes(2166136261u,&world->rooms[room],sizeof(SwatRoom));
    for(int i=0;i<world->count;i++) {
        const SwatObject* o=&world->objects[i];
        if(!o->active || o->material==SWAT_GLASS || !swat_lighting_room_intersects(&world->rooms[room],
            (Vector3){o->center.x,o->center.y,o->center.z},(Vector3){o->half.x,o->half.y,o->half.z}))continue;
        hash=hash_bytes(hash,&i,sizeof(i));hash=hash_bytes(hash,&o->center,sizeof(o->center));
        hash=hash_bytes(hash,&o->half,sizeof(o->half));hash=hash_bytes(hash,&o->yaw,sizeof(o->yaw));
        hash=hash_bytes(hash,&o->pitch,sizeof(o->pitch));
    }
    return hash;
}
static Vector3 room_light_origin(const SwatWorld* world,int room) {
    b3Pos bulb;if(swat_motel_lamp(world,room,&bulb))return (Vector3){bulb.x,bulb.y,bulb.z};
    const SwatRoom* r=&world->rooms[room];
    Vector3 origin={(float)r->center.x,(float)r->center.y+r->half.y-.18f,(float)r->center.z};
    for(int i=0;i<world->count;i++) {
        const SwatObject* o=&world->objects[i];
        b3Vec3 d=b3SubPos(o->center,r->center);
        if(o->active && o->part==SWAT_PART_LIGHT && fabsf(d.x)<r->half.x && fabsf(d.y)<r->half.y && fabsf(d.z)<r->half.z)
            return (Vector3){(float)o->center.x,(float)o->center.y-o->half.y-.01f,(float)o->center.z};
    }
    return origin;
}
static void prepare(SwatLighting* light,const SwatSim* sim,Vector3 eye,bool cutaway,SwatShadowScene draw,SwatShadowSceneContext contextual,SwatShadowSceneContext actors,void* context) {
    if(!light->enabled) return;
    (void)eye; // Every room owns a stable tile; the camera never selects a winner.
    const SwatWorld* world=&sim->world;
    Vector3 center={11,0,0}; float extent=28;
    if(world->room_count) {
        float x0=1e30f,z0=1e30f,x1=-1e30f,z1=-1e30f;
        for(int i=0;i<world->room_count;i++) {
            const SwatRoom* r=&world->rooms[i];
            x0=fminf(x0,(float)r->center.x-r->half.x); x1=fmaxf(x1,(float)r->center.x+r->half.x);
            z0=fminf(z0,(float)r->center.z-r->half.z); z1=fmaxf(z1,(float)r->center.z+r->half.z);
        }
        center=(Vector3){(x0+x1)*.5f,0,(z0+z1)*.5f}; extent=fmaxf(x1-x0,z1-z0)*1.5f+12;
    }
    uint32_t hash=geometry_hash(world);
    bool reset=!light->prepared || cutaway!=light->cutaway || light->split_shadows!=(actors!=NULL) || sim->tick<light->last_tick;
    bool refresh=reset || sim->tick-light->last_tick>=4;
    if(!refresh && hash==light->geometry)return;
    Camera3D sun={Vector3Add(center,Vector3Scale(light->sun_direction,45)),center,{0,1,0},extent,CAMERA_ORTHOGRAPHIC};
    light->shadow_room=-1;
    light->sun_matrix=shadow(light->sun,sun,sim,cutaway,draw,contextual,actors,context,-1,true);
    for(int room=0;room<world->room_count;room++) {
        uint32_t local=room_geometry_hash(world,room);
        Vector3 origin=room_light_origin(world,room);local=hash_bytes(local,&origin,sizeof(origin));
        bool dirty=reset || !light->room_ready[room] || local!=light->room_geometry[room] ||
            (!actors && sim->actor_count && refresh); // Legacy combined callbacks may include actors.
        light->shadow_room=room;light->shadow_origin=origin;
        float fov=2*atanf(lamp_tan());
        if(dirty) {
            for(int face=0;face<SWAT_LAMP_FACES;face++) {
                light->shadow_face=face;
                Camera3D lamp={origin,Vector3Add(origin,lamp_dir[face]),lamp_up[face],fov/SWAT_RAD,CAMERA_PERSPECTIVE};
                shadow(light->lamp_static,lamp,sim,cutaway,draw,contextual,NULL,context,room*SWAT_LAMP_FACES+face,true);
                light->lamp_matrix[face]=MatrixMultiply(MatrixLookAt((Vector3){0},lamp_dir[face],lamp_up[face]),MatrixPerspective(fov,1,.05,120));
            }
            light->room_geometry[room]=local;light->room_ready[room]=true;light->room_updates++;
        }
        if(dirty || refresh) {
            // Restore the entire room's cached depth before adding moving actors.
            int y=room*SWAT_LAMP_SIZE,w=SWAT_LAMP_SIZE*SWAT_LAMP_FACES;
            rlDrawRenderBatchActive();
            rlBindFramebuffer(RL_READ_FRAMEBUFFER,light->lamp_static.id);
            rlBindFramebuffer(RL_DRAW_FRAMEBUFFER,light->lamp.id);
            rlBlitFramebuffer(0,y,w,y+SWAT_LAMP_SIZE,0,y,w,y+SWAT_LAMP_SIZE,0x00000100);
            rlDisableFramebuffer();
            if(actors)for(int face=0;face<SWAT_LAMP_FACES;face++) {
                light->shadow_face=face;
                Camera3D lamp={origin,Vector3Add(origin,lamp_dir[face]),lamp_up[face],fov/SWAT_RAD,CAMERA_PERSPECTIVE};
                shadow(light->lamp,lamp,sim,cutaway,NULL,NULL,actors,context,room*SWAT_LAMP_FACES+face,false);
            }
        }
    }
    for(int i=world->room_count;i<SWAT_MAX_ROOMS;i++)light->room_ready[i]=false;
    light->shadow_room=-1;light->split_shadows=actors!=NULL;
    light->geometry=hash;if(refresh)light->last_tick=sim->tick;
    light->cutaway=cutaway; light->prepared=true; light->updates++;
}
void swat_lighting_prepare(SwatLighting* light,const SwatSim* sim,Vector3 eye,bool cutaway,SwatShadowScene draw) {
    prepare(light,sim,eye,cutaway,draw,NULL,NULL,NULL);
}
void swat_lighting_prepare_context(SwatLighting* light,const SwatSim* sim,Vector3 eye,bool cutaway,SwatShadowSceneContext draw,void* context) {
    prepare(light,sim,eye,cutaway,NULL,draw,NULL,context);
}
void swat_lighting_prepare_split(SwatLighting* light,const SwatSim* sim,Vector3 eye,bool cutaway,SwatShadowSceneContext geometry,SwatShadowSceneContext actors,void* context) {
    prepare(light,sim,eye,cutaway,NULL,geometry,actors,context);
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
    Vector3 centers[SWAT_MAX_ROOMS],halves[SWAT_MAX_ROOMS],origins[SWAT_MAX_ROOMS],directions[SWAT_MAX_ROOMS]={0};float room_power[SWAT_MAX_ROOMS];
    for(int i=0;i<world->room_count;i++) {
        const SwatRoom* r=&world->rooms[i];
        centers[i]=(Vector3){(float)r->center.x,(float)r->center.y,(float)r->center.z};
        halves[i]=(Vector3){r->half.x,r->half.y,r->half.z}; origins[i]=room_light_origin(world,i);
        if(world->motel && i>=1 && i<=4)directions[i]=(Vector3){0,-.9396926f,.3420201f};
        b3Pos bulb;room_power[i]=!world->motel || i<1 || i>4 || swat_motel_lamp(world,i,&bulb) ? 1 : 0;
        if(world->room_light_off_mask&(1u<<i))room_power[i]=0;
    }
    SwatLightingProgram* programs[]={&light->batch,&light->mesh};
    for(int i=0;i<2;i++) {
        SwatLightingProgram* p=programs[i]; Shader s=p->shader; int sun=14,lamp=15;
        int zero=0,lamp_shadows=light->lamp_shadows; Vector2 tile={0};
        SetShaderValue(s,p->lamp_shadows,&lamp_shadows,SHADER_UNIFORM_INT);
        SetShaderValue(s,p->pbr,&zero,SHADER_UNIFORM_INT);
        SetShaderValue(s,p->spec_gloss,&zero,SHADER_UNIFORM_INT);
        SetShaderValue(s,p->skinning,&zero,SHADER_UNIFORM_INT);
        SetShaderValue(s,p->emission,&zero,SHADER_UNIFORM_INT);
        SetShaderValue(s,p->environment,&zero,SHADER_UNIFORM_INT);
        SetShaderValue(s,p->occlusion,&zero,SHADER_UNIFORM_INT);
        int ibl=light->environment_atlas.id!=0,ibl_slot=9,contact=light->contact_ready,contact_slot=7,depth_slot=8;
        SetShaderValue(s,p->ibl,&ibl,SHADER_UNIFORM_INT);SetShaderValue(s,p->ibl_atlas,&ibl_slot,SHADER_UNIFORM_INT);
        SetShaderValue(s,p->sun_direction,&light->sun_direction,SHADER_UNIFORM_VEC3);
        SetShaderValue(s,p->sun_energy,&light->sun_energy,SHADER_UNIFORM_VEC3);
        SetShaderValue(s,p->contact,&contact,SHADER_UNIFORM_INT);SetShaderValue(s,p->contact_map,&contact_slot,SHADER_UNIFORM_INT);
        SetShaderValue(s,p->contact_depth_map,&depth_slot,SHADER_UNIFORM_INT);
        SetShaderValueMatrix(s,p->contact_matrix,light->contact_matrix);
        SetShaderValue(s,p->environment_tile,&tile,SHADER_UNIFORM_VEC2);
        SetShaderValueMatrix(s,p->sun_matrix,light->sun_matrix);
        for(int room=0;room<SWAT_LAMP_FACES;room++)SetShaderValueMatrix(s,p->lamp_matrix[room],light->lamp_matrix[room]);
        SetShaderValue(s,p->camera,&camera,SHADER_UNIFORM_VEC3);
        SetShaderValue(s,p->sun_map,&sun,SHADER_UNIFORM_INT); SetShaderValue(s,p->lamp_map,&lamp,SHADER_UNIFORM_INT);
        SetShaderValue(s,p->rooms,&world->room_count,SHADER_UNIFORM_INT);
        SetShaderValue(s,p->exposure,&light->exposure,SHADER_UNIFORM_FLOAT);
        if(world->room_count) {
            SetShaderValueV(s,p->centers,centers,SHADER_UNIFORM_VEC3,world->room_count);
            SetShaderValueV(s,p->halves,halves,SHADER_UNIFORM_VEC3,world->room_count);
            SetShaderValueV(s,p->origins,origins,SHADER_UNIFORM_VEC3,world->room_count);
            SetShaderValueV(s,p->room_power,room_power,SHADER_UNIFORM_FLOAT,world->room_count);
            SetShaderValueV(s,p->room_direction,directions,SHADER_UNIFORM_VEC3,world->room_count);
        }
    }
    rlActiveTextureSlot(9);if(light->environment_atlas.id)rlEnableTexture(light->environment_atlas.id);
    rlActiveTextureSlot(7);if(light->contact_ready)rlEnableTexture(light->contact_ao.texture.id);
    rlActiveTextureSlot(8);if(light->contact_ready)rlEnableTexture(light->contact_depth.depth.id);
    rlActiveTextureSlot(14); rlEnableTexture(light->sun.depth.id);
    rlActiveTextureSlot(15); rlEnableTexture(light->lamp.depth.id); rlActiveTextureSlot(0);
    light->surface_normal=light->surface_roughness=~0u;
    art->lit=true; art->lighting=light; art_shader(art,light->mesh.shader); BeginShaderMode(light->batch.shader);
}
void swat_lighting_end(SwatLighting* light,SwatEnvironmentArt* art) {
    if(!light->enabled || !light->prepared) return;
    EndShaderMode(); art->lit=false; art->lighting=NULL; art_shader(art,(Shader){rlGetShaderIdDefault(),rlGetShaderLocsDefault()});
    rlActiveTextureSlot(7);rlDisableTexture();rlActiveTextureSlot(8);rlDisableTexture();rlActiveTextureSlot(9);rlDisableTexture();
    rlActiveTextureSlot(12); rlDisableTexture(); rlActiveTextureSlot(13); rlDisableTexture();
    rlActiveTextureSlot(14); rlDisableTexture(); rlActiveTextureSlot(15); rlDisableTexture(); rlActiveTextureSlot(0);
}

void swat_lighting_sky(SwatLighting* light,Camera3D camera,int width,int height) {
    if(!light->enabled || camera.projection!=CAMERA_PERSPECTIVE || !light->sky.id || light->sky.id==rlGetShaderIdDefault() || height<1) return;
    Vector3 forward=Vector3Normalize(Vector3Subtract(camera.target,camera.position));
    Vector3 right=Vector3Normalize(Vector3CrossProduct(forward,camera.up)),up=Vector3CrossProduct(right,forward);
    float t=tanf(camera.fovy*DEG2RAD*.5f); Vector2 scale={t*width/height,t};
    SetShaderValue(light->sky,light->sky_forward,&forward,SHADER_UNIFORM_VEC3);
    SetShaderValue(light->sky,light->sky_right,&right,SHADER_UNIFORM_VEC3);
    SetShaderValue(light->sky,light->sky_up,&up,SHADER_UNIFORM_VEC3);
    SetShaderValue(light->sky,light->sky_scale,&scale,SHADER_UNIFORM_VEC2);
    Vector2 size={(float)width,(float)height}; SetShaderValue(light->sky,light->sky_size,&size,SHADER_UNIFORM_VEC2);
    SetShaderValue(light->sky,light->sky_exposure,&light->exposure,SHADER_UNIFORM_FLOAT);
    int ibl=light->environment_sky.id!=0;
    SetShaderValue(light->sky,light->sky_ibl,&ibl,SHADER_UNIFORM_INT);
    SetShaderValue(light->sky,light->sky_map_scale,&light->environment_scale,SHADER_UNIFORM_FLOAT);
    // Shader switching flushes Raylib's batch and clears its sampler registry.
    // Register the sky texture after that flush, so the quad retains its map.
    BeginShaderMode(light->sky);
    if(ibl)SetShaderValueTexture(light->sky,light->sky_map,light->environment_sky);
    DrawRectangle(0,0,width,height,WHITE); EndShaderMode();
}
