// Native checks exercise the production shader against independently computed
// material inputs, not an offline rendering or a duplicate blend shader.
#include "../motel_zoning.h"
#if defined(_WIN32)
__declspec(dllimport) unsigned char __stdcall glIsTexture(unsigned int texture);
#else
extern unsigned char glIsTexture(unsigned int texture);
#endif
static Texture2D zoning_float_texture(const float* pixels,int width,int height) {
    Image image={(void*)pixels,width,height,1,PIXELFORMAT_UNCOMPRESSED_R32G32B32A32};
    Texture2D t=LoadTextureFromImage(image);assert(t.id);
    SetTextureFilter(t,TEXTURE_FILTER_BILINEAR);SetTextureWrap(t,TEXTURE_WRAP_REPEAT);return t;
}
static float zoning_srgb(float x) {return x<=.04045f?x/12.92f:powf((x+.055f)/1.055f,2.4f);}
static float zoning_mask_weight(const Color* pixels,float x,float z) {
    float u=(x+64)/128,v=(z+48)/100;
    if(u<0||u>1||v<0||v>1)return 0;
    float col=u*512-.5f,row=v*512-.5f;int c=(int)floorf(col),r=(int)floorf(row);
    float value=0;
    for(int j=0;j<2;j++)for(int i=0;i<2;i++) {
        int a=c+i,b=r+j;if(a<0)a=0;if(a>511)a=511;if(b<0)b=0;if(b>511)b=511;
        value+=pixels[b*512+a].r/255.0f*(i?col-c:1-(col-c))*(j?row-r:1-(row-r));
    }
    return value;
}
static Vector3 zoning_texel(const float* pixels,int width,float u,float v) {
    float x=u*width-.5f,y=v*width-.5f;int a=(int)floorf(x),b=(int)floorf(y);Vector3 result={0};
    for(int j=0;j<2;j++)for(int i=0;i<2;i++) {
        int c=((a+i)%width+width)%width,r=((b+j)%width+width)%width;
        float weight=(i?x-a:1-(x-a))*(j?y-b:1-(y-b));const float* p=&pixels[(r*width+c)*4];
        result=Vector3Add(result,Vector3Scale((Vector3){p[0],p[1],p[2]},weight));
    }
    return result;
}
static Color zoning_probe(SwatView* view,Mesh mesh,Material material,Vector3 point,Texture2D mask,Texture2D dirt,bool ground) {
    Camera3D camera={{point.x,point.y+12,point.z},point,{0,0,-1},1,CAMERA_ORTHOGRAPHIC};
    RenderTexture2D target=LoadRenderTexture(33,33);assert(target.id);
    BeginTextureMode(target);ClearBackground(MAGENTA);BeginMode3D(camera);
    swat_lighting_begin(&view->lighting,&view->environment,&sim.world,camera.position);
    material.shader=view->lighting.mesh.shader;
    swat_lighting_material_scaled(&view->lighting,material,true,ground?.65f:1);
    if(ground)swat_lighting_ground(&view->lighting,mask,dirt);
    DrawMesh(mesh,material,MatrixTranslate(point.x,point.y,point.z));
    swat_lighting_material(&view->lighting,(Material){0},false);
    swat_lighting_end(&view->lighting,&view->environment);EndMode3D();EndTextureMode();
    Image image=LoadImageFromTexture(target.texture);Color c=GetImageColor(image,16,16);
    assert(c.r!=MAGENTA.r||c.g!=MAGENTA.g||c.b!=MAGENTA.b);
    UnloadImage(image);UnloadRenderTexture(target);return c;
}
static void zoning_shader_proof(SwatView* view,const Color* mask_pixels) {
    Mesh mesh=GenMeshPlane(2,2,1,1);Material material=LoadMaterialDefault();
    MaterialMap* owned_maps=material.maps;
    MaterialMap maps[MATERIAL_MAP_BRDF+1]={0};material.maps=maps;maps[MATERIAL_MAP_ALBEDO].color=WHITE;
    maps[MATERIAL_MAP_NORMAL].value=2;maps[MATERIAL_MAP_ROUGHNESS].value=1;
    const float gn[]={.70f,.25f,.91f,1},dn[]={.24f,.78f,.88f,1};
    const float gr[]={1,.82f,0,1},dr[]={.21f,.21f,.21f,1};
    float gravel[64],dirt[64];
    for(int i=0;i<16;i++) {int x=i%4,y=i/4;float* g=&gravel[i*4],*d=&dirt[i*4];
        g[0]=.15f+.12f*x;g[1]=.18f+.1f*y;g[2]=.51f-.03f*x;g[3]=1;
        d[0]=.35f+.06f*y;d[1]=.14f+.09f*x;d[2]=.15f+.04f*y;d[3]=1;
    }
    Texture2D gc=zoning_float_texture(gravel,4,4),dc=zoning_float_texture(dirt,4,4);
    Texture2D normal=zoning_float_texture(gn,1,1),dnormal=zoning_float_texture(dn,1,1);
    Texture2D rough=zoning_float_texture(gr,1,1),drough=zoning_float_texture(dr,1,1);
    Vector3 points[]={ {-50,.18f,-35},{-45,.18f,-10},{-28,.18f,20},{35,.18f,-16},{50,.18f,8},
        {17,.18f,49},{0,.18f,0},{0,.18f,30},{0,.18f,40},{-2.8f,.18f,47},{-70,.18f,0},{65,.18f,55},{50,.18f,8} };
    int checked=0,positive=0,zeros=0,max_error=0;
    for(unsigned p=0;p<sizeof(points)/sizeof(*points);p++) {
        bool bottom=p==12;
        Vector3 point=points[p];float weight=bottom?0:zoning_mask_weight(mask_pixels,point.x,point.z);
        if(bottom) {
            for(int i=0;i<mesh.vertexCount;i++)mesh.normals[i*3+1]=-1;
            UpdateMeshBuffer(mesh,2,mesh.normals,mesh.vertexCount*3*sizeof(float),0);
        }
        positive+=weight>.01f;zeros+=weight==0;
        // Preserve original exported world-phase UVs and their flipped V. This
        // asymmetric 4x4 fixture detects scale, phase and either flipped axis.
        for(int i=0;i<mesh.vertexCount;i++) {
            mesh.texcoords[i*2]=(mesh.vertices[i*3]+point.x)*3/8;
            mesh.texcoords[i*2+1]=1-(mesh.vertices[i*3+2]+point.z)*3/8;
        }
        UpdateMeshBuffer(mesh,1,mesh.texcoords,mesh.vertexCount*2*sizeof(float),0);
        Vector3 g=zoning_texel(gravel,4,point.x*3/8,1-point.z*3/8);
        Vector3 d=zoning_texel(dirt,4,point.x/2,1-point.z/2);
        float reference_color[]={powf(zoning_srgb(g.x)*.65f*(1-weight)+zoning_srgb(d.x)*weight,1/2.2f),
            powf(zoning_srgb(g.y)*.65f*(1-weight)+zoning_srgb(d.y)*weight,1/2.2f),
            powf(zoning_srgb(g.z)*.65f*(1-weight)+zoning_srgb(d.z)*weight,1/2.2f),1};
        if(bottom){reference_color[0]=g.x;reference_color[1]=g.y;reference_color[2]=g.z;}
        Vector3 a=Vector3Normalize((Vector3){(gn[0]*2-1)*.65f,(gn[1]*2-1)*.65f,gn[2]*2-1});
        Vector3 b=Vector3Normalize((Vector3){(dn[0]*2-1)*.65f,(dn[1]*2-1)*.65f,dn[2]*2-1});
        Vector3 n=Vector3Normalize(Vector3Add(Vector3Scale(a,1-weight),Vector3Scale(b,weight)));
        float reference_normal[]={n.x*.5f+.5f,n.y*.5f+.5f,n.z*.5f+.5f,1};
        float r=gr[1]*(1-weight)+dr[0]*weight,reference_rough[]={1,r,0,1};
        Texture2D rc=zoning_float_texture(reference_color,1,1),rn=zoning_float_texture(reference_normal,1,1),rr=zoning_float_texture(reference_rough,1,1);
        maps[MATERIAL_MAP_ALBEDO].texture=gc;maps[MATERIAL_MAP_NORMAL].texture=normal;maps[MATERIAL_MAP_ROUGHNESS].texture=rough;
        maps[MATERIAL_MAP_SPECULAR].texture=dnormal;maps[MATERIAL_MAP_EMISSION].texture=drough;
        Color actual=zoning_probe(view,mesh,material,point,view->environment.motel_zoning,dc,true);
        maps[MATERIAL_MAP_ALBEDO].texture=rc;maps[MATERIAL_MAP_NORMAL].texture=rn;maps[MATERIAL_MAP_ROUGHNESS].texture=rr;
        maps[MATERIAL_MAP_SPECULAR].texture=(Texture2D){0};maps[MATERIAL_MAP_EMISSION].texture=(Texture2D){0};
        Color expected=zoning_probe(view,mesh,material,point,(Texture2D){0},(Texture2D){0},false);
        int errors[]={abs(actual.r-expected.r),abs(actual.g-expected.g),abs(actual.b-expected.b)};
        for(int i=0;i<3;i++){if(errors[i]>max_error)max_error=errors[i];if(errors[i]>3)fprintf(stderr,"zoning sample %u weight %.5f actual %u,%u,%u expected %u,%u,%u\n",p,weight,actual.r,actual.g,actual.b,expected.r,expected.g,expected.b);assert(errors[i]<=3);checked++;}
        UnloadTexture(rc);UnloadTexture(rn);UnloadTexture(rr);
    }
    assert(positive>=4 && zeros>=4);
    UnloadTexture(gc);UnloadTexture(dc);UnloadTexture(normal);UnloadTexture(dnormal);UnloadTexture(rough);UnloadTexture(drough);
    // Restore allocation ownership before unloading the empty default material.
    // The per-draw map array above is on the stack and owns no texture.
    UnloadMesh(mesh);
    MemFree(owned_maps);
    printf("PASS zoning production shader: %d RGB channel comparisons, %d blended/%d zero samples, world mask orientation, exact sRGB/factor, dirt 2 m phase, roughness, common-basis normals and unchanged bottom-face material (maximum error %d/255)\n",checked,positive,zeros,max_error);
}
static void zoning_graphics(SwatView* view,const char* directory) {
    SwatEnvironmentArt* art=&view->environment;assert(art->motel_zoning.id && art->motel_dirt.color.id && art->motel_dirt.normal.id && art->motel_dirt.roughness.id);
    assert(art->motel_zoning.width==512 && art->motel_zoning.height==512 && art->motel_zoning.mipmaps==1 && art->motel_zoning.format==PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);
    assert(art->motel_dirt.color.width==1024 && art->motel_dirt.normal.width==1024 && art->motel_dirt.roughness.width==1024);
    int eligible=0;for(int i=0;i<SWAT_GROUND_PARTS;i++)eligible+=swat_motel_zoning_part(i);assert(eligible==12);
    before=sim.world;
    MaterialMap original_maps[3][MATERIAL_MAP_BRDF+1];
    for(int i=0;i<3;i++)memcpy(original_maps[i],art->motel_ground.materials[i].maps,sizeof(original_maps[i]));
    Image mask=LoadImageFromTexture(art->motel_zoning);Color* pixels=LoadImageColors(mask);assert(pixels);
    int protected_samples=0;
    for(float z=5;z<=48;z+=.125f)for(float x=-3;x<=3;x+=.125f){assert(zoning_mask_weight(pixels,x,z)==0);protected_samples++;}
    for(float z=36;z<=44;z+=.125f)for(float x=-56;x<=56;x+=.125f){assert(zoning_mask_weight(pixels,x,z)==0);protected_samples++;}
    Image zero=GenImageColor(1,1,BLACK);Texture2D zero_mask=LoadTextureFromImage(zero);UnloadImage(zero);assert(zero_mask.id);
    Camera3D cameras[]={{{50,1.65f,8},{4,1,-3},{0,1,0},70,CAMERA_PERSPECTIVE},
        {{0,75,8},{0,0,8},{0,0,-1},140,CAMERA_ORTHOGRAPHIC}};
    const char* names[]={"eye","aerial"};
    for(int p=0;p<2;p++) {
        Image image=room101_capture_size(view,cameras[p],true,1440,810);char path[4096];
        snprintf(path,sizeof(path),"%s/zoning-%s.png",directory,names[p]);assert(ExportImage(image,path));
        Texture2D original=art->motel_zoning;art->motel_zoning=zero_mask;
        Image gravel=room101_capture_size(view,cameras[p],true,1440,810);art->motel_zoning=original;
        snprintf(path,sizeof(path),"%s/zoning-%s-gravel.png",directory,names[p]);assert(ExportImage(gravel,path));
        if(p==1) {
            Color* actual=LoadImageColors(image),*base=LoadImageColors(gravel);int changed=0,protected_pixels=0;
            for(int y=0;y<810;y++)for(int x=0;x<1440;x++) {
                Ray ray=GetScreenToWorldRayEx((Vector2){x+.5f,y+.5f},cameras[p],1440,810);
                int i=y*1440+x;bool equal=actual[i].r==base[i].r&&actual[i].g==base[i].g&&actual[i].b==base[i].b;
                changed+=!equal;
                // Same real scene/camera/depth; protected road and drive remain
                // pixel-identical even at this minified whole-level view.
                if((fabsf(ray.position.x)<2.5f&&ray.position.z>25&&ray.position.z<47)||
                    (fabsf(ray.position.x)<54&&ray.position.z>36.5f&&ray.position.z<43.5f)) {assert(equal);protected_pixels++;}
            }
            assert(changed>1000&&protected_pixels>1000);
            printf("PASS zoning native aerial: %d changed soil pixels, %d identical protected road/drive pixels, %d protected bilinear world probes\n",changed,protected_pixels,protected_samples);
            UnloadImageColors(actual);UnloadImageColors(base);
        }
        UnloadImage(image);UnloadImage(gravel);
    }
    zoning_shader_proof(view,pixels);UnloadImageColors(pixels);UnloadImage(mask);UnloadTexture(zero_mask);
    assert(!memcmp(&before,&sim.world,sizeof(before)));
    for(int i=0;i<3;i++)assert(!memcmp(original_maps[i],art->motel_ground.materials[i].maps,sizeof(original_maps[i])));
    // All four location-owned allocations disappear on switching locations.
    unsigned int ids[]={art->motel_zoning.id,art->motel_dirt.color.id,art->motel_dirt.normal.id,art->motel_dirt.roughness.id};
    static SwatWorld other;other=(SwatWorld){0};swat_environment_art_prepare_location(art,&other);
    assert(!art->motel_zoning.id&&!art->motel_dirt.color.id&&!art->motel_dirt.normal.id&&!art->motel_dirt.roughness.id);
    for(int i=0;i<4;i++)assert(!glIsTexture(ids[i]));
    swat_environment_art_prepare_location(art,&sim.world);assert(art->motel_zoning.id&&art->motel_dirt.normal.id);
    puts("PASS zoning ownership: unchanged authority/support geometry, immutable source maps, location switch frees all four additions and reloads them");
}
