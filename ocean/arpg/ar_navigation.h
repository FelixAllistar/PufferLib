#pragma once
#include "ar_geometry.h"
#ifdef AR_GPU_SIM
#define AR_NAV_FN static __host__ __device__ __forceinline__
#else
#define AR_NAV_FN static inline
#endif

AR_NAV_FN int ar_nav_visible(const uint8_t* tiles,float size,float x,float y,float tx,float ty) {
    float d=sqrtf(ar_geometry_dist2(x,y,tx,ty));
    int steps=(int)(d*3)+1;
    for(int i=1;i<=steps;i++) {
        float a=(float)i/steps;
        if(!ar_geometry_floor(tiles,size,x+(tx-x)*a,y+(ty-y)*a))return 0;
    }
    return 1;
}

// Reverse BFS across the current simulation window. Deterministic on CPU/CUDA;
// callers cache waypoints and only search when the direct route is obstructed.
AR_NAV_FN int ar_nav_next(const uint8_t* tiles,float size,float x,float y,float tx,float ty,float* nx,float* ny) {
    float cell=size/AR_DUN_W,half=size*0.5f;
    int sx=(int)floorf((x+half)/cell),sy=(int)floorf((y+half)/cell);
    int gx=(int)floorf((tx+half)/cell),gy=(int)floorf((ty+half)/cell);
    if(sx<0||sx>=AR_DUN_W||sy<0||sy>=AR_DUN_H)return 0;
    gx=(int)ar_geometry_clampf((float)gx,0,AR_DUN_W-1);
    gy=(int)ar_geometry_clampf((float)gy,0,AR_DUN_H-1);
    int16_t parent[AR_DUN_CELLS];uint16_t queue[AR_DUN_CELLS];
    for(int i=0;i<AR_DUN_CELLS;i++)parent[i]=-1;
    int root=-1;float best=1e9f;
    for(int yy=gy-3;yy<=gy+3;yy++)for(int xx=gx-3;xx<=gx+3;xx++) {
        if(xx<0||xx>=AR_DUN_W||yy<0||yy>=AR_DUN_H)continue;
        int i=yy*AR_DUN_W+xx;
        if(tiles[i]==AR_TILE_ROCK||tiles[i]==AR_TILE_DEEP)continue;
        float d=(float)((xx-gx)*(xx-gx)+(yy-gy)*(yy-gy));
        if(d<best){best=d;root=i;}
    }
    if(root<0)return 0;
    int start=sy*AR_DUN_W+sx,head=0,tail=0;
    parent[root]=(int16_t)root;queue[tail++]=(uint16_t)root;
    while(head<tail && parent[start]<0) {
        int at=queue[head++],ax=at%AR_DUN_W,ay=at/AR_DUN_W;
        const int dx[4]={1,0,-1,0},dy[4]={0,1,0,-1};
        for(int d=0;d<4;d++) {
            int xx=ax+dx[d],yy=ay+dy[d];
            if(xx<0||xx>=AR_DUN_W||yy<0||yy>=AR_DUN_H)continue;
            int n=yy*AR_DUN_W+xx;
            if(parent[n]>=0||tiles[n]==AR_TILE_ROCK||tiles[n]==AR_TILE_DEEP)continue;
            parent[n]=(int16_t)at;queue[tail++]=(uint16_t)n;
        }
    }
    if(parent[start]<0)return 0;
    int next=parent[start];
    // Look ahead two grid cells, but never cut a rock/water corner.
    for(int i=0;i<2;i++) {
        int candidate=parent[next];
        float cx=-half+(candidate%AR_DUN_W+0.5f)*cell,cy=-half+(candidate/AR_DUN_W+0.5f)*cell;
        if(!ar_nav_visible(tiles,size,x,y,cx,cy))break;
        next=candidate;
    }
    *nx=-half+(next%AR_DUN_W+0.5f)*cell;
    *ny=-half+(next/AR_DUN_W+0.5f)*cell;
    return 1;
}
#undef AR_NAV_FN
