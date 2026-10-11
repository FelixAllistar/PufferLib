#ifndef SWAT_IMPACT_EFFECTS_DRAW_H
#define SWAT_IMPACT_EFFECTS_DRAW_H
#include "impact_effects.h"
#include "lighting.h"
#include "raymath.h"
#include "rlgl.h"

static void swat_impact_vertex(Vector3 p,Vector3 normal,const uint8_t color[4],bool edge) {
    rlNormal3f(normal.x,normal.y,normal.z);
    rlColor4ub(color[0],color[1],color[2],edge?0:color[3]);
    rlTexCoord2f(.5f,.5f);rlVertex3f(p.x,p.y,p.z);
}
static void swat_impact_draw(const SwatSoundLog* log,int tick,Camera3D camera,SwatLighting* light) {
    SwatImpactParticle particles[SWAT_IMPACT_EFFECT_PARTICLES];
    b3Pos eye={camera.position.x,camera.position.y,camera.position.z};
    Vector3 forward=Vector3Normalize(Vector3Subtract(camera.target,camera.position));
    int count=swat_impact_sample(log,tick,eye,swat_v(forward.x,forward.y,forward.z),particles);
    if(!count)return;
    swat_lighting_surface(light,(Texture2D){0},(Texture2D){0},(Vector3){0},(Vector2){0},false);
    rlDrawRenderBatchActive();rlDisableDepthMask();rlDisableBackfaceCulling();
    rlSetTexture(rlGetTextureIdDefault());rlBegin(RL_TRIANGLES);
    for(int i=0;i<count;i++) {
        const SwatImpactParticle* p=&particles[i];
        Vector3 center={(float)p->position.x,(float)p->position.y,(float)p->position.z};
        Vector3 normal=Vector3Normalize(Vector3Subtract(camera.position,center));
        if(!p->dust && !p->shard) {
            // Small solid chips tumble in world space. Glass and dust retain
            // their thin billboard shapes; no cube/model draw per fragment.
            float a=p->rotation*.7f,b=p->rotation*.4f;
            normal=(Vector3){sinf(a)*cosf(b),sinf(b),cosf(a)*cosf(b)};
        }
        Vector3 right=Vector3CrossProduct(camera.up,normal);
        if(Vector3LengthSqr(right)<.0001f)right=Vector3CrossProduct((Vector3){1,0,0},normal);
        right=Vector3Normalize(right);Vector3 up=Vector3CrossProduct(normal,right);
        int sides=p->dust?8:3;
        float rotation=p->rotation;
        float c=cosf(rotation),s=sinf(rotation);
        if(!p->dust && !p->shard) {
            const Vector3 local[4]={{1,0,-.35f},{-.5f,.866f,-.35f},{-.5f,-.866f,-.35f},{0,0,.55f}};
            const int faces[4][3]={{0,2,1},{0,1,3},{1,2,3},{2,0,3}};
            Vector3 vertices[4];
            for(int v=0;v<4;v++) {
                float x=local[v].x,y=local[v].y*p->aspect,z=local[v].z;
                Vector3 plane=Vector3Add(Vector3Scale(right,x*c-y*s),Vector3Scale(up,x*s+y*c));
                vertices[v]=Vector3Add(center,Vector3Scale(Vector3Add(plane,Vector3Scale(normal,z)),p->radius));
            }
            for(int f=0;f<4;f++) {
                Vector3 a=vertices[faces[f][0]],b=vertices[faces[f][1]],c=vertices[faces[f][2]];
                Vector3 face=Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b,a),Vector3Subtract(c,a)));
                swat_impact_vertex(a,face,p->color,false);
                swat_impact_vertex(b,face,p->color,false);
                swat_impact_vertex(c,face,p->color,false);
            }
            continue;
        }
        for(int v=0;v<sides;v++) {
            Vector3 corners[2];
            for(int k=0;k<2;k++) {
                float angle=2*SWAT_PI*(v+k)/sides,x=cosf(angle),y=sinf(angle)*p->aspect;
                corners[k]=Vector3Add(center,Vector3Scale(Vector3Add(Vector3Scale(right,x*c-y*s),Vector3Scale(up,x*s+y*c)),p->radius));
            }
            swat_impact_vertex(center,normal,p->color,false);
            swat_impact_vertex(corners[0],normal,p->color,p->dust);
            swat_impact_vertex(corners[1],normal,p->color,p->dust);
        }
    }
    rlEnd();rlSetTexture(0);rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();rlEnableDepthMask();
}
#endif
