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
        Vector3 right=Vector3CrossProduct(camera.up,normal);
        if(Vector3LengthSqr(right)<.0001f)right=Vector3CrossProduct((Vector3){1,0,0},normal);
        right=Vector3Normalize(right);Vector3 up=Vector3CrossProduct(normal,right);
        int sides=p->dust?8:3;
        float rotation=p->rotation;
        float c=cosf(rotation),s=sinf(rotation);
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
