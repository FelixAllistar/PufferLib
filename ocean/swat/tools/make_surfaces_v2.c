// Procedural, tileable surface sets for materials_v2 (CC0, generated here).
// Same contract as materials_v1: 512x512 <id>_basecolor (sRGB), _normal
// (linear, OpenGL +Y/+V, PNG row 0 = V=1) and _roughness (R8 linear).
//   make_surfaces_v2 OUT_DIR
#include "raylib.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define N 512
static float height[N*N];
static Color base[N*N],normal_px[N*N],rough[N*N];

static uint32_t hash(uint32_t x,uint32_t y,uint32_t s) {
    uint32_t h=x*374761393u+y*668265263u+s*2246822519u; h=(h^(h>>13))*1274126177u; return h^(h>>16);
}
static float lattice(int x,int y,int period,uint32_t s) {
    x=((x%period)+period)%period; y=((y%period)+period)%period; return (hash((uint32_t)x,(uint32_t)y,s)&65535)/65535.0f;
}
// Periodic value noise: `cells` lattice cells across the tile, so it wraps.
static float noise(float u,float v,int cells,uint32_t s) {
    float x=u*cells,y=v*cells; int ix=(int)floorf(x),iy=(int)floorf(y); float fx=x-ix,fy=y-iy;
    fx=fx*fx*(3-2*fx); fy=fy*fy*(3-2*fy);
    float a=lattice(ix,iy,cells,s),b=lattice(ix+1,iy,cells,s),c=lattice(ix,iy+1,cells,s),d=lattice(ix+1,iy+1,cells,s);
    return (a+(b-a)*fx)+((c+(d-c)*fx)-(a+(b-a)*fx))*fy;
}
static float fbm(float u,float v,int cells,int octaves,uint32_t s) {
    float sum=0,amp=.5f,norm=0;
    for(int o=0;o<octaves;o++){sum+=amp*noise(u,v,cells<<o,s+o*101);norm+=amp;amp*=.5f;}
    return sum/norm;
}
static float lin(float c) { c/=255; return c<=.04045f?c/12.92f:powf((c+.055f)/1.055f,2.4f); }
static unsigned char srgb(float l) {
    l=l<0?0:l>1?1:l; float c=l<=.0031308f?12.92f*l:1.055f*powf(l,1/2.4f)-.055f; return (unsigned char)lrintf(c*255);
}
// Normals from the height field (metres) at `texel` metres per pixel.
static void normals(float texel) {
    for(int y=0;y<N;y++)for(int x=0;x<N;x++) {
        float l=height[y*N+(x+N-1)%N],r=height[y*N+(x+1)%N];
        float up=height[((y+N-1)%N)*N+x],down=height[((y+1)%N)*N+x]; // row 0 is V=1
        float du=(r-l)/(2*texel),dv=(up-down)/(2*texel);
        float nx=-du,ny=-dv,nz=1,len=sqrtf(nx*nx+ny*ny+nz*nz);
        normal_px[y*N+x]=(Color){(unsigned char)lrintf((nx/len*.5f+.5f)*255),(unsigned char)lrintf((ny/len*.5f+.5f)*255),
            (unsigned char)lrintf((nz/len*.5f+.5f)*255),255};
    }
}
static void save(const char* dir,const char* id) {
    const char* kinds[3]={"basecolor","normal","roughness"};Color* data[3]={base,normal_px,rough};
    for(int k=0;k<3;k++) {
        Image im={.data=data[k],.width=N,.height=N,.mipmaps=1,.format=PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        if(!ExportImage(im,TextFormat("%s/%s_%s.png",dir,id,kinds[k]))){fprintf(stderr,"export failed\n");exit(1);}
    }
}

// Painted lap siding, 1.2 m tile: eight 150 mm boards. Each board is 14 mm
// thick at its bottom lip, tapering to 4 mm where the next board covers it,
// so every lip casts a shadow line. Light warm-grey paint over faint grain.
static void siding(const char* dir) {
    const float tile=1.2f,texel=tile/N; const int boards=8;
    for(int y=0;y<N;y++)for(int x=0;x<N;x++) {
        float u=(float)x/N,v=1-(float)(y+.5f)/N;           // v up
        float bv=v*boards;int board=(int)floorf(bv);float f=bv-board; // 0 at bottom lip
        float t=.014f-.010f*f;                              // lap taper
        float lip=f<.03f?(f/.03f):1;                         // rounded bottom lip
        float grain=fbm(u*1.0f,v*.08f,24,3,7+board)*.0007f;  // fine raised grain
        height[y*N+x]=t*lip+grain;
        // Paint: tiny per-board tone, faint grain, slight grime under each lip.
        float tone=.97f+.06f*(hash((uint32_t)board,0,3)&255)/255.0f;
        float streak=fbm(u,v*.05f+board*.37f,96,3,11);
        float weather=fbm(u,v,6,3,23);
        float dirt=f<.10f?.90f+f:1;
        float shade=tone*(.97f+.06f*streak)*(.95f+.10f*weather)*dirt;
        float r=lin(198)*shade,g=lin(194)*shade,b=lin(182)*shade;
        base[y*N+x]=(Color){srgb(r),srgb(g),srgb(b),255};
        unsigned char ro=(unsigned char)lrintf(255*(.55f+.10f*streak));
        rough[y*N+x]=(Color){ro,ro,ro,255};
    }
    normals(texel); save(dir,"siding_painted");
}

// Lawn, 2 m tile: patchy green with drier and lusher areas, blade-scale
// colour/height noise and sparse soil specks.
static void lawn(const char* dir) {
    const float tile=2,texel=tile/N;
    for(int y=0;y<N;y++)for(int x=0;x<N;x++) {
        float u=(float)x/N,v=1-(float)(y+.5f)/N;
        float patch=fbm(u,v,4,4,31),dry=fbm(u,v,3,3,37),blade=fbm(u,v,128,2,41),clump=fbm(u,v,24,3,43);
        height[y*N+x]=.004f*blade+.003f*clump;
        float lush=patch,arid=dry>.58f?(dry-.58f)*2.4f:0;
        // Muted lawn: real grass albedo is darker and less saturated than it looks.
        float r=lin(66)+(lin(104)-lin(66))*arid,g=lin(86)+(lin(104)-lin(86))*arid,b=lin(42)+(lin(56)-lin(42))*arid;
        float shade=.70f+.45f*lush+.28f*(blade-.5f)+.15f*(clump-.5f);
        r*=shade;g*=shade;b*=shade;
        float soil=(hash((uint32_t)x,(uint32_t)y,47)&1023)<6?1:0;
        if(soil){r=lin(88);g=lin(70);b=lin(48);}
        base[y*N+x]=(Color){srgb(r),srgb(g),srgb(b),255};
        unsigned char ro=(unsigned char)lrintf(255*(.86f+.1f*blade));
        rough[y*N+x]=(Color){ro,ro,ro,255};
    }
    normals(texel); save(dir,"grass_lawn");
}
int main(int argc,char** argv) {
    if(argc<2){fprintf(stderr,"usage: %s OUT_DIR\n",argv[0]);return 2;}
    SetTraceLogLevel(LOG_WARNING); siding(argv[1]); lawn(argv[1]); return 0;
}
