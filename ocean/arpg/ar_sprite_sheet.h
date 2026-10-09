#pragma once
#include "raylib.h"
#include "rlgl.h"
#include <math.h>
#include <stdlib.h>

// Import the original PNGs without rewriting them. Generated sheets have wide
// gutters, but not necessarily mathematically equal rows. Detect the gutters
// once, then keep a shared scale and foot pivot across each animation strip.
typedef struct {
    Texture2D texture;
    Rectangle frame[32];
    Vector2 pivot[32];
    float body_height[4];
    int columns,rows;
} ARSpriteSheet;

static inline int ar_sheet_gutter(const int* counts,int length,int nominal,int radius) {
    int lo=nominal-radius,hi=nominal+radius;
    if(lo<1)lo=1;if(hi>length-1)hi=length-1;
    int best=nominal,best_width=-1;
    for(int at=lo;at<=hi;) {
        if(counts[at]>2){at++;continue;}
        int start=at;while(at<=hi && counts[at]<=2)at++;
        int width=at-start;
        if(width>best_width){best_width=width;best=(start+at)/2;}
    }
    return best;
}

static inline void ar_sheet_measure(ARSpriteSheet* s,Image image,int columns,int rows,int animated) {
    s->columns=columns;s->rows=rows;
    Color* pixels=LoadImageColors(image);if(!pixels)return;
    int* scan=(int*)calloc((size_t)(image.width+image.height),sizeof(int));
    if(!scan){UnloadImageColors(pixels);return;}
    int* ys=scan+image.width;
    for(int y=0;y<image.height;y++)for(int x=0;x<image.width;x++)
        if(pixels[y*image.width+x].a>40)ys[y]++;
    int yedge[5]={0};yedge[rows]=image.height;
    for(int row=1;row<rows;row++)
        yedge[row]=ar_sheet_gutter(ys,image.height,row*image.height/rows,image.height/rows*43/100);
    for(int row=0;row<rows;row++) {
        for(int x=0;x<image.width;x++) {
            scan[x]=0;
            for(int y=yedge[row];y<yedge[row+1];y++)if(pixels[y*image.width+x].a>40)scan[x]++;
        }
        int xedge[9]={0};xedge[columns]=image.width;
        for(int col=1;col<columns;col++)
            xedge[col]=ar_sheet_gutter(scan,image.width,col*image.width/columns,image.width/columns/3);
        int row_top=yedge[row+1],bottom[8]={0},tops[8]={0};
        for(int col=0;col<columns;col++) {
            int lx=xedge[col+1],rx=xedge[col],ty=yedge[row+1],by=yedge[row];
            for(int y=yedge[row];y<yedge[row+1];y++)for(int x=xedge[col];x<xedge[col+1];x++)
                if(pixels[y*image.width+x].a>40) {
                    if(x<lx)lx=x;if(x>rx)rx=x;if(y<ty)ty=y;if(y>by)by=y;
                }
            if(rx<lx || by<ty){lx=xedge[col];rx=lx;ty=yedge[row];by=ty;}
            int n=row*columns+col;bottom[col]=by;tops[col]=ty;
            if(ty<row_top)row_top=ty;
            s->frame[n]=(Rectangle){(float)lx,(float)ty,(float)(rx-lx+1),(float)(by-ty+1)};
            s->pivot[n]=(Vector2){(rx-lx+1)*0.5f,(float)(by-ty+1)};
        }
        if(animated) {
            // The idle/walk poses define body height and the shared ground
            // line; attack flames must not resize or vertically kick the body.
            int feet[5],heads[5];
            for(int i=0;i<5;i++){feet[i]=bottom[i];heads[i]=tops[i];}
            for(int a=0;a<5;a++)for(int b=a+1;b<5;b++) {
                if(feet[a]>feet[b]){int v=feet[a];feet[a]=feet[b];feet[b]=v;}
                if(heads[a]>heads[b]){int v=heads[a];heads[a]=heads[b];heads[b]=v;}
            }
            s->body_height[row]=fmaxf(1,feet[2]-heads[2]+1);
            for(int col=0;col<columns;col++) {
                int n=row*columns+col;
                // Retain the artist's cell-center alignment, including wider
                // action poses. Never re-center every pose by its alpha bounds.
                float center=(col+0.5f)*image.width/columns;
                s->frame[n]=(Rectangle){(float)xedge[col],(float)row_top,(float)(xedge[col+1]-xedge[col]),(float)(yedge[row+1]-row_top)};
                s->pivot[n]=(Vector2){center-xedge[col],(float)(feet[2]+1-row_top)};
            }
        }
    }
    free(scan);UnloadImageColors(pixels);
}

static inline ARSpriteSheet ar_sheet_load(const char* path,int columns,int rows,int animated) {
    ARSpriteSheet s={0};Image image=LoadImage(path);
    if(image.data) {
        ar_sheet_measure(&s,image,columns,rows,animated);
        s.texture=LoadTextureFromImage(image);SetTextureFilter(s.texture,TEXTURE_FILTER_BILINEAR);
        UnloadImage(image);
    }
    return s;
}

// Eight views across, idle/two strides/command down. Import the untouched PNG;
// alpha bounds may vary, but neither the cell center nor the ground pivot does.
static inline void ar_sheet_measure_directions(ARSpriteSheet* s,Image image) {
    ar_sheet_measure(s,image,8,4,0);
    float width=image.width/8.0f,height=image.height/4.0f;
    float sizes[8];
    for(int dir=0;dir<8;dir++)sizes[dir]=s->frame[dir].height;
    for(int a=0;a<8;a++)for(int b=a+1;b<8;b++)
        if(sizes[a]>sizes[b]){float swap=sizes[a];sizes[a]=sizes[b];sizes[b]=swap;}
    for(int row=0;row<4;row++)s->body_height[row]=fmaxf(1,(sizes[3]+sizes[4])*.5f);
    for(int dir=0;dir<8;dir++) {
        float ground[3];
        for(int row=0;row<3;row++) {
            Rectangle r=s->frame[row*8+dir];ground[row]=r.y+r.height-row*height;
        }
        for(int a=0;a<3;a++)for(int b=a+1;b<3;b++)
            if(ground[a]>ground[b]){float swap=ground[a];ground[a]=ground[b];ground[b]=swap;}
        for(int row=0;row<4;row++) {
            int n=row*8+dir;Rectangle r=s->frame[n];
            s->pivot[n]=(Vector2){(dir+.5f)*width-r.x,row*height+ground[1]-r.y};
            // A planted command stance has no lifted stride foot. Generated
            // action rows can sit higher in their cells; ground that stance
            // without changing body scale or its horizontal cell-center pivot.
            if(row==3)s->pivot[n].y=r.height;
        }
    }
}

static inline ARSpriteSheet ar_sheet_load_directions(const char* path) {
    ARSpriteSheet s={0};Image image=LoadImage(path);
    if(image.data) {
        ar_sheet_measure_directions(&s,image);
        s.texture=LoadTextureFromImage(image);SetTextureFilter(s.texture,TEXTURE_FILTER_BILINEAR);
        UnloadImage(image);
    }
    return s;
}

static inline void ar_sheet_release(ARSpriteSheet* s) {
    if(s->texture.id)UnloadTexture(s->texture);
    *s=(ARSpriteSheet){0};
}

// Direction is in screen space, not a rotation of a flat sprite. Remember the
// previous heading at rest. The isometric projection compresses vertical motion.
static inline int ar_screen_direction(float vx,float vy,int previous) {
    if(hypotf(vx,vy)<.08f)return previous;
    float angle=atan2f(vx-vy,(vx+vy)*.5f);
    if(angle<0)angle+=2*PI;
    return ((int)floorf(angle/(PI*.25f)+.5f))&7;
}

static inline int ar_direction_frame(int direction,int pose) {
    int row=pose>=5 ? 3 : pose>0 ? 1+((pose-1)&1) : 0;
    return row*8+(direction&7);
}

static inline void ar_sheet_draw(ARSpriteSheet* s,int index,Vector2 feet,float height,float flip,Color tint) {
    if(!s->texture.id || index<0 || index>=s->columns*s->rows)return;
    Rectangle src=s->frame[index];Vector2 pivot=s->pivot[index];
    float scale=height/(s->body_height[index/s->columns]>0 ? s->body_height[index/s->columns] : src.height);
    if(flip<0){src.width=-src.width;pivot.x=fabsf(src.width)-pivot.x;}
    DrawTexturePro(s->texture,src,(Rectangle){feet.x-pivot.x*scale,feet.y-pivot.y*scale,
        fabsf(src.width)*scale,src.height*scale},(Vector2){0,0},0,tint);
}

// Wind bends only the upper canopy; the trunk stays attached to the ground.
static inline void ar_sheet_sway(ARSpriteSheet* s,int index,Vector2 feet,float height,float sway,Color tint) {
    Rectangle src=s->frame[index];float scale=height/src.height;
    Vector2 pivot=s->pivot[index];float x=feet.x-pivot.x*scale,y=feet.y-pivot.y*scale,w=src.width*scale;
    float u=src.x/s->texture.width,v=src.y/s->texture.height,uw=src.width/s->texture.width,vh=src.height/s->texture.height;
    rlSetTexture(s->texture.id);rlBegin(RL_QUADS);rlColor4ub(tint.r,tint.g,tint.b,tint.a);
    rlTexCoord2f(u,v);rlVertex2f(x+sway,y);
    rlTexCoord2f(u,v+vh);rlVertex2f(x,y+height);
    rlTexCoord2f(u+uw,v+vh);rlVertex2f(x+w,y+height);
    rlTexCoord2f(u+uw,v);rlVertex2f(x+w+sway,y);
    rlEnd();rlSetTexture(0);
}

static inline int ar_animation_frame(float gait,float speed,float action_age) {
    if(action_age>=0 && action_age<0.3f)return 5+(int)(action_age*10);
    return speed>0.08f ? 1+((int)(gait*3.2f)%4) : 0;
}
