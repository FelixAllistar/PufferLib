// Offline HDR environment convolution. Build/run via `make lighting-bake`.
// The player loads the small baked atlas; no sampling work runs during play.
#include "raylib.h"
#include "raymath.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_HDR
#include "vendor/stb_image.h"

#define W 256
#define H 128
#define LEVELS 6
#define SAMPLES 256
static Image source;
static Vector3 sun;
static float scale;
static float luminance(Vector3 v) { return .2126f*v.x+.7152f*v.y+.0722f*v.z; }
static Vector3 direction(float u,float v) {
    float phi=(u-.5f)*2*PI,theta=v*PI;
    return (Vector3){sinf(theta)*cosf(phi),cosf(theta),sinf(theta)*sinf(phi)};
}
static Vector3 pixel(int x,int y) {
    x=(x+source.width)%source.width; y=y<0?0:y>=source.height?source.height-1:y;
    return ((Vector3*)source.data)[y*source.width+x];
}
static Vector3 sample(Vector3 d) {
    float x=(atan2f(d.z,d.x)/(2*PI)+.5f)*source.width-.5f;
    float y=acosf(Clamp(d.y,-1,1))/PI*source.height-.5f;
    int ix=(int)floorf(x),iy=(int)floorf(y); float a=x-ix,b=y-iy;
    Vector3 p=Vector3Lerp(Vector3Lerp(pixel(ix,iy),pixel(ix+1,iy),a),
        Vector3Lerp(pixel(ix,iy+1),pixel(ix+1,iy+1),a),b);
    // Direct solar energy is represented by the shadowed directional light.
    // Remove its narrow disc from ambient/specular convolution to avoid leaks.
    if(Vector3DotProduct(d,sun)>cosf(1.25f*DEG2RAD)) {
        Vector3 tangent=Vector3Normalize(Vector3CrossProduct(sun,(Vector3){0,1,0}));
        Vector3 edge=Vector3Normalize(Vector3Add(sun,Vector3Scale(tangent,.04f)));
        float ex=(atan2f(edge.z,edge.x)/(2*PI)+.5f)*source.width;
        float ey=acosf(Clamp(edge.y,-1,1))/PI*source.height;
        p=pixel((int)ex,(int)ey);
    }
    return Vector3Scale(p,scale);
}
static float radical(uint32_t bits) {
    bits=(bits<<16)|(bits>>16); bits=((bits&0x55555555u)<<1)|((bits&0xAAAAAAAAu)>>1);
    bits=((bits&0x33333333u)<<2)|((bits&0xCCCCCCCCu)>>2);
    bits=((bits&0x0F0F0F0Fu)<<4)|((bits&0xF0F0F0F0u)>>4);
    bits=((bits&0x00FF00FFu)<<8)|((bits&0xFF00FF00u)>>8);
    return bits*2.3283064365386963e-10f;
}
static Vector3 basis(Vector3 n,Vector3 h) {
    Vector3 up=fabsf(n.y)<.99f?(Vector3){0,1,0}:(Vector3){1,0,0};
    Vector3 t=Vector3Normalize(Vector3CrossProduct(up,n)),b=Vector3CrossProduct(n,t);
    return Vector3Add(Vector3Add(Vector3Scale(t,h.x),Vector3Scale(b,h.y)),Vector3Scale(n,h.z));
}
static Vector3 ggx(float u,float v,float roughness) {
    float a=roughness*roughness,c=sqrtf((1-v)/(1+(a*a-1)*v)),s=sqrtf(fmaxf(0,1-c*c));
    return (Vector3){cosf(2*PI*u)*s,sinf(2*PI*u)*s,c};
}
static float visibility(float cosine,float roughness) {
    float k=roughness*roughness*.5f;
    return cosine/(cosine*(1-k)+k);
}
int main(int argc,char** argv) {
    if(argc!=3) { fprintf(stderr,"usage: lighting_bake source.hdr output.bin\n");return 2; }
    int channels=0;source.data=stbi_loadf(argv[1],&source.width,&source.height,&channels,3);
    if(!source.data) { fprintf(stderr,"HDR decode failed: %s\n",stbi_failure_reason());return 1; }
    double sum=0,weight=0; float peak=0;
    for(int y=0;y<source.height/2;y++) for(int x=0;x<source.width;x++) {
        float l=luminance(pixel(x,y)),w=sinf(PI*(y+.5f)/source.height);
        if(l>peak) { peak=l; sun=direction((x+.5f)/source.width,(y+.5f)/source.height); }
        sum+=l*w;weight+=w;
    }
    // Explicit exposure calibration, shared by background and convolution.
    scale=.45f/(sum/weight);
    Vector3 solar={0};
    for(int y=0;y<source.height/2;y++) for(int x=0;x<source.width;x++) {
        Vector3 d=direction((x+.5f)/source.width,(y+.5f)/source.height);
        if(Vector3DotProduct(d,sun)>cosf(1.25f*DEG2RAD)) {
            float solid=2*PI*PI/(source.width*source.height)*sinf(PI*(y+.5f)/source.height);
            solar=Vector3Add(solar,Vector3Scale(pixel(x,y),solid*scale));
        }
    }
    Vector3* atlas=calloc(W*H*(LEVELS+2),sizeof(Vector3)); if(!atlas)return 1;
    for(int page=0;page<LEVELS+2;page++) {
        for(int y=0;y<H;y++) for(int x=0;x<W;x++) {
            Vector3 n=direction((x+.5f)/W,(y+.5f)/H),total={0}; float w=0;
            float roughness=(float)page/(LEVELS-1);
            for(uint32_t i=0;i<SAMPLES;i++) {
                float u=(i+.5f)/SAMPLES,v=radical(i);
                if(page==LEVELS+1) {
                    float nv=(x+.5f)/W,r=(y+.5f)/H;
                    Vector3 view={sqrtf(1-nv*nv),0,nv},h=ggx(u,v,r);
                    float vh=fmaxf(0,Vector3DotProduct(view,h));
                    Vector3 l=Vector3Subtract(Vector3Scale(h,2*vh),view);
                    if(l.z>0) { float g=visibility(nv,r)*visibility(l.z,r)*vh/(fmaxf(h.z,1e-5f)*nv),f=powf(1-vh,5);
                        total.x+=(1-f)*g;total.y+=f*g; }
                    w=SAMPLES;
                } else if(page==LEVELS) {
                    float s=sqrtf(v); Vector3 d=basis(n,(Vector3){cosf(2*PI*u)*s,sinf(2*PI*u)*s,sqrtf(1-v)});
                    total=Vector3Add(total,sample(d));w++;
                } else {
                    Vector3 h=basis(n,ggx(u,v,roughness));
                    Vector3 l=Vector3Subtract(Vector3Scale(h,2*Vector3DotProduct(n,h)),n);
                    float nl=fmaxf(0,Vector3DotProduct(n,l));
                    if(nl>0) { total=Vector3Add(total,Vector3Scale(sample(l),nl));w+=nl; }
                }
            }
            atlas[(page*H+y)*W+x]=Vector3Scale(total,1/fmaxf(w,1e-5f));
        }
        printf("baked layer %d/%d\n",page+1,LEVELS+2);
    }
    FILE* out=fopen(argv[2],"wb"); if(!out)return 1;
    uint32_t header[]={0x31424953u,W,H,LEVELS,source.width,source.height};
    int ok=fwrite(header,sizeof(header),1,out)==1 && fwrite(&scale,sizeof(scale),1,out)==1 &&
        fwrite(&sun,sizeof(sun),1,out)==1 && fwrite(&solar,sizeof(solar),1,out)==1 &&
        fwrite(atlas,sizeof(Vector3),W*H*(LEVELS+2),out)==W*H*(LEVELS+2) &&
        fwrite(source.data,sizeof(Vector3),source.width*source.height,out)==source.width*source.height;
    ok=fclose(out)==0 && ok;
    printf("scale=%.8g sun=(%.5f %.5f %.5f) irradiance=(%.5f %.5f %.5f)\n",scale,sun.x,sun.y,sun.z,solar.x,solar.y,solar.z);
    free(atlas);stbi_image_free(source.data);return ok?0:1;
}
