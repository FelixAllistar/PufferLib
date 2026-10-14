// Framed wall support and bounded physical rubble. Included by world.c only.
#ifndef SWAT_WALL_SUPPORT_H
#define SWAT_WALL_SUPPORT_H
#define SWAT_SUPPORT_GROUPS 16
struct SwatWallSupport {
    int count,groups[SWAT_SUPPORT_GROUPS],batch;
    uint32_t dirty,builds;
    bool busy,saved[SWAT_MAX_OBJECTS];
    SwatFracture* before[SWAT_MAX_OBJECTS];
};
struct SwatSupportPlan {
    SwatFracture* cuts[SWAT_MAX_OBJECTS];
    SwatFracturePolygon* lost[SWAT_MAX_OBJECTS];
    int lost_count[SWAT_MAX_OBJECTS];
};
typedef struct SupportNode {
    SwatFracturePolygon polygon;
    b3AABB box;
    int owner,section,parent;
    bool anchor,follower,supported;
} SupportNode;
typedef struct SupportQuery {SupportNode* nodes;int at;bool followers;} SupportQuery;

static bool support_intersection(const SwatFracturePolygon* a,const SwatFracturePolygon* b,SwatFracturePolygon* result) {
    *result=*a;
    for(int e=0;e<b->count && result->count>=3;e++) {
        const float* p=b->vertices[e];const float* q=b->vertices[(e+1)%b->count];
        float dy=q[0]-p[0],dz=q[1]-p[1],length=hypotf(dy,dz);
        if(length<1e-7f)continue;
        SwatCutPlane plane={dz/length,-dy/length,(dz*p[0]-dy*p[1])/length};
        SwatFracturePolygon clipped;if(!swat_polygon_clip(result,plane,-1,&clipped))return false;
        *result=clipped;
    }
    return result->count>=3 && swat_polygon_area(result)>1e-7f;
}
static bool support_contact(const SupportNode* a,const SupportNode* b) {
    SwatFracturePolygon overlap;
    if(support_intersection(&a->polygon,&b->polygon,&overlap) && swat_polygon_area(&overlap)>1e-6f)return true;
    // A shared edge carries material across hidden divisions; corner contact
    // alone does not fasten two pieces together.
    for(int i=0;i<a->polygon.count;i++)for(int j=0;j<b->polygon.count;j++) {
        const float* p=a->polygon.vertices[i];const float* q=a->polygon.vertices[(i+1)%a->polygon.count];
        const float* r=b->polygon.vertices[j];const float* s=b->polygon.vertices[(j+1)%b->polygon.count];
        float y=q[0]-p[0],z=q[1]-p[1],dy=s[0]-r[0],dz=s[1]-r[1],length=hypotf(y,z),other=hypotf(dy,dz);
        if(length<.002f || other<.002f || fabsf(y*dz-z*dy)>1e-4f*length*other)continue;
        if(fabsf(y*(r[1]-p[1])-z*(r[0]-p[0]))>length*.0002f)continue;
        float lo=(y*(r[0]-p[0])+z*(r[1]-p[1]))/length;
        float hi=(y*(s[0]-p[0])+z*(s[1]-p[1]))/length;
        if(fminf(length,fmaxf(lo,hi))-fmaxf(0,fminf(lo,hi))>=.002f)return true;
    }
    return false;
}
static int support_root(SupportNode* n,int at) {
    int root=at;while(n[root].parent!=root)root=n[root].parent;
    while(at!=root){int next=n[at].parent;n[at].parent=root;at=next;}
    return root;
}
static bool support_neighbor(int proxy,uint64_t user,void* context) {
    (void)proxy;SupportQuery* q=context;int a=q->at,b=(int)user;
    if(a==b || q->nodes[b].follower || (!q->followers && q->nodes[a].follower))return true;
    if(!support_contact(&q->nodes[a],&q->nodes[b]))return true;
    int rb=support_root(q->nodes,b);
    if(q->followers){q->nodes[a].supported|=q->nodes[rb].anchor;return true;}
    int ra=support_root(q->nodes,a);if(ra==rb)return true;
    int low=ra<rb?ra:rb,high=ra<rb?rb:ra;
    q->nodes[high].parent=low;q->nodes[low].anchor|=q->nodes[high].anchor;return true;
}
uint32_t swat_world_support_mask(const SwatWorld* w,int group) {
    if(w->support)for(int i=0;i<w->support->count;i++)if(w->support->groups[i]==group)return 1u<<i;
    return 0;
}
void swat_world_support_setup(SwatWorld* w) {
    for(int i=0;i<w->count;i++)if(w->objects[i].support_role!=SWAT_SUPPORT_NONE) {
        if(!w->support){w->support=calloc(1,sizeof(*w->support));assert(w->support);}
        int group=w->objects[i].wall_group;
        if(!swat_world_support_mask(w,group)) {
            assert(w->support->count<SWAT_SUPPORT_GROUPS);
            w->support->groups[w->support->count++]=group;
        }
    }
}
void swat_world_support_enable(SwatWorld* w,int group) {
    for(int i=0;i<w->count;i++) {
        SwatObject* o=&w->objects[i];if(o->wall_group!=group || o->door)continue;
        if(o->part==SWAT_PART_SUPPORT && o->material==SWAT_WOOD && !o->max_health)o->support_role=SWAT_SUPPORT_ANCHOR;
        else if((o->part==SWAT_PART_FRAME || o->part==SWAT_PART_SKIN) && o->max_health>0)o->support_role=SWAT_SUPPORT_MEMBER;
    }
    swat_world_support_setup(w);
}
void swat_world_support_close(SwatSupportPlan* plan) {
    if(!plan)return;
    for(int i=0;i<SWAT_MAX_OBJECTS;i++){swat_world_cut_close(plan->cuts[i]);free(plan->lost[i]);}
    free(plan);
}
static bool support_add_loss(SwatSupportPlan* plan,int owner,const SwatFracturePolygon* lost) {
    int count=plan->lost_count[owner];
    if(count==SWAT_CUT_SECTIONS)count=plan->lost_count[owner]=swat_polygons_join(plan->lost[owner],count);
    if(count==SWAT_CUT_SECTIONS)return true; // Excess fine debris sheds without an extra body.
    if(!plan->lost[owner]){plan->lost[owner]=malloc(SWAT_CUT_SECTIONS*sizeof(*lost));if(!plan->lost[owner])return false;}
    plan->lost[owner][count]=*lost;plan->lost_count[owner]++;return true;
}
static void support_edges(SwatFracture* f) {
    for(int s=0;s<f->section_count;s++) {
        SwatFracturePolygon* p=&f->sections[s];p->exposed_edges=0;
        for(int e=0;e<p->count;e++) {
            const float* a=p->vertices[e];const float* b=p->vertices[(e+1)%p->count];
            float dy=b[0]-a[0],dz=b[1]-a[1],length=hypotf(dy,dz);if(length<1e-7f)continue;
            float y=.5f*(a[0]+b[0])+.00005f*dz/length,z=.5f*(a[1]+b[1])-.00005f*dy/length;bool present=false;
            for(int k=0;k<f->section_count && !present;k++)present=swat_polygon_contains(&f->sections[k],y,z,1e-9f);
            if(!present)p->exposed_edges|=1u<<e;
        }
    }
}
SwatSupportPlan* swat_world_support_prepare(const SwatWorld* w,const SwatObject* candidate,uint32_t groups) {
    SwatSupportPlan* plan=calloc(1,sizeof(*plan));if(!plan)return NULL;
    if(!candidate)candidate=w->objects;
    if(!w->support || !groups)return plan;
    for(int group=0;group<w->support->count;group++)if(groups&(1u<<group)) {
        int id=w->support->groups[group],first=-1,count=0;
        for(int i=0;i<w->count;i++)if(candidate[i].active && candidate[i].wall_group==id && candidate[i].support_role) {
            if(first<0)first=i;
            const SwatObject* o=&candidate[i];
            if(o->support_role==SWAT_SUPPORT_ANCHOR){count++;continue;}
            SwatFracture* f=swat_world_cut_prepare(o,o->cuts?o->cuts->cuts:NULL,o->cuts?o->cuts->count:0);
            if(!f)goto failed;
            plan->cuts[i]=f;count+=f->section_count;
        }
        if(first<0 || !count)continue;
        SupportNode* nodes=calloc((size_t)count,sizeof(*nodes));if(!nodes)goto failed;
        b3Quat basis=swat_rotation(candidate[first].yaw,0);b3Pos origin=candidate[first].center;int n=0;
        for(int i=0;i<w->count;i++) {
            const SwatObject* o=&candidate[i];if(!o->active || o->wall_group!=id || !o->support_role)continue;
            int sections=plan->cuts[i]?plan->cuts[i]->section_count:1;
            b3Vec3 offset=b3InvRotateVector(basis,b3SubPos(o->center,origin));
            for(int s=0;s<sections;s++) {
                SupportNode* node=&nodes[n];node->parent=n++;node->owner=i;node->section=s;
                node->anchor=o->support_role==SWAT_SUPPORT_ANCHOR;node->follower=o->material==SWAT_INSULATION;
                if(plan->cuts[i])node->polygon=plan->cuts[i]->sections[s];else swat_source_polygon(o,&node->polygon);
                node->box=(b3AABB){swat_v(offset.x-o->half.x-.0006f,INFINITY,INFINITY),swat_v(offset.x+o->half.x+.0006f,-INFINITY,-INFINITY)};
                for(int k=0;k<node->polygon.count;k++) {
                    float y=node->polygon.vertices[k][0]+=offset.y,z=node->polygon.vertices[k][1]+=offset.z;
                    node->box.lowerBound.y=fminf(node->box.lowerBound.y,y-.0001f);node->box.upperBound.y=fmaxf(node->box.upperBound.y,y+.0001f);
                    node->box.lowerBound.z=fminf(node->box.lowerBound.z,z-.0001f);node->box.upperBound.z=fmaxf(node->box.upperBound.z,z+.0001f);
                }
            }
        }
        assert(n==count);b3DynamicTree tree=b3DynamicTree_Create(n);SupportQuery query={.nodes=nodes};
        for(int i=0;i<n;i++) {
            query.at=i;
            if(!nodes[i].follower)b3DynamicTree_Query(&tree,nodes[i].box,1,false,support_neighbor,&query);
            b3DynamicTree_CreateProxy(&tree,nodes[i].box,1,(uint64_t)i);
        }
        query.followers=true;
        for(int i=0;i<n;i++) {
            if(nodes[i].follower){query.at=i;b3DynamicTree_Query(&tree,nodes[i].box,1,false,support_neighbor,&query);}
            else nodes[i].supported=nodes[support_root(nodes,i)].anchor;
        }
        b3DynamicTree_Destroy(&tree);
        // Only newly unsupported material spawns. Canonical reconstruction of
        // older cuts can contain islands already shed on an earlier update.
        for(int i=0;i<n;i++)if(!nodes[i].supported && plan->cuts[nodes[i].owner]) {
            int owner=nodes[i].owner;const SwatObject* old=&w->objects[owner];
            SwatFracture* previous=w->support->saved[owner]?w->support->before[owner]:old->cuts;
            SwatFracturePolygon source;swat_source_polygon(old,&source);
            const SwatFracturePolygon* lost=&plan->cuts[owner]->sections[nodes[i].section];
            int before=previous?previous->section_count:1;
            for(int s=0;s<before;s++) {
                SwatFracturePolygon overlap;
                if(support_intersection(lost,previous?&previous->sections[s]:&source,&overlap) && !support_add_loss(plan,owner,&overlap)) {
                    free(nodes);goto failed;
                }
            }
        }
        for(int i=0;i<n;i++)if(plan->cuts[nodes[i].owner] && !nodes[i].supported) {
            SwatFracture* f=plan->cuts[nodes[i].owner];b3DestroyHull(f->pending_hulls[nodes[i].section]);f->pending_hulls[nodes[i].section]=NULL;
        }
        free(nodes);
        for(int i=0;i<w->count;i++)if(plan->cuts[i] && candidate[i].wall_group==id) {
            SwatFracture* f=plan->cuts[i];int kept=0;
            for(int s=0;s<f->section_count;s++)if(f->pending_hulls[s]) {
                f->sections[kept]=f->sections[s];f->pending_hulls[kept++]=f->pending_hulls[s];
            }
            f->section_count=kept;f->support_filtered=true;support_edges(f);
        }
    }
    return plan;
failed:swat_world_support_close(plan);return NULL;
}
bool swat_world_support_active_valid(const SwatSupportPlan* plan,const SwatObject* objects) {
    for(int i=0;i<SWAT_MAX_OBJECTS;i++)if(plan->cuts[i] && objects[i].active && !plan->cuts[i]->section_count)return false;
    return true;
}
void swat_world_support_take(SwatSupportPlan* plan,SwatFracture* cuts[SWAT_MAX_OBJECTS]) {
    for(int i=0;i<SWAT_MAX_OBJECTS;i++)if(plan->cuts[i]){swat_world_cut_close(cuts[i]);cuts[i]=plan->cuts[i];plan->cuts[i]=NULL;}
}
static void rubble_remove(SwatRubble* r) {
    if(r->object.active)b3DestroyBody(r->object.body);
    r->object.active=r->state.active=r->state.awake=false;
    r->object.body=b3_nullBodyId;r->object.shape=b3_nullShapeId;
    swat_world_cut_close(r->object.cuts);r->object.cuts=NULL;
}
bool swat_world_rubble_valid(const SwatWorld* w,const SwatRubbleState* s) {
    if(!s->serial || !s->active)return false;
    if(s->source<0 || s->source>=w->count || w->objects[s->source].support_role!=SWAT_SUPPORT_MEMBER ||
       s->section_count<1 || s->section_count>SWAT_RUBBLE_SECTIONS || !isfinite(s->health) || s->health<=0 || s->health>w->objects[s->source].max_health+.001f)return false;
    SwatDoorState pose={.broken_hinges=1,.position=s->position,.rotation=s->rotation,.velocity=s->velocity,.angular_velocity=s->angular_velocity};
    if(!swat_door_state_valid(&pose))return false;
    SwatFracturePolygon source;swat_source_polygon(&w->objects[s->source],&source);
    for(int j=0;j<s->section_count;j++) {
        const SwatFracturePolygon* p=&s->sections[j];if(p->count<3 || p->count>SWAT_CUT_VERTICES)return false;
        if(p->count<32 && p->exposed_edges>>p->count)return false;
        for(int k=0;k<p->count;k++) {
            const float* a=p->vertices[k];const float* b=p->vertices[(k+1)%p->count];const float* c=p->vertices[(k+2)%p->count];
            if(!isfinite(a[0]) || !isfinite(a[1]) || !swat_polygon_contains(&source,a[0],a[1],.00001f) ||
               (b[0]-a[0])*(c[1]-b[1])-(b[1]-a[1])*(c[0]-b[0])< -1e-7f)return false;
        }
        if(swat_polygon_area(p)<=1e-7f)return false;
    }
    return true;
}
SwatFracture* swat_world_rubble_prepare(const SwatWorld* w,const SwatRubbleState* s) {
    if(!swat_world_rubble_valid(w,s))return NULL;
    SwatFracture* f=swat_world_sections_prepare(&w->objects[s->source],NULL,0,s->sections,s->section_count);
    if(f){f->support_filtered=true;support_edges(f);}return f;
}
bool swat_world_rubble_separated(const SwatObject* source,const SwatFracture* retained,const SwatRubbleState* state) {
    if(!source->active)return true;
    SwatFracturePolygon original;swat_source_polygon(source,&original);
    int count=retained?retained->section_count:1;
    for(int i=0;i<state->section_count;i++)for(int j=0;j<count;j++) {
        SwatFracturePolygon overlap;
        if(support_intersection(&state->sections[i],retained?&retained->sections[j]:&original,&overlap) && swat_polygon_area(&overlap)>1e-5f)return false;
    }
    return true;
}
void swat_world_rubble_restore(SwatWorld* w,int slot,const SwatRubbleState* s,SwatFracture* prepared) {
    assert(w->rubble && slot>=0 && slot<SWAT_RUBBLE_CAPACITY);SwatRubble* r=&w->rubble->pieces[slot];
    if(!s->active){rubble_remove(r);r->state=(SwatRubbleState){.serial=s->serial};r->object.tag=(SwatTag){SWAT_HIT_RUBBLE,slot};r->object.rubble=r;return;}
    if(prepared) {
        rubble_remove(r);r->object=w->objects[s->source];
        r->origin=r->object.center;r->source_yaw=r->object.yaw;
        r->object.tag=(SwatTag){SWAT_HIT_RUBBLE,slot};r->object.motion=NULL;r->object.rubble=r;
        r->object.support_role=SWAT_SUPPORT_NONE;r->object.door=false;r->object.cuts=NULL;
        for(int k=0;k<SWAT_MAX_SUPPORTS;k++)r->object.supports[k]=-1;
        b3BodyDef def=b3DefaultBodyDef();def.type=b3_dynamicBody;def.position=s->position;def.rotation=s->rotation;
        def.linearDamping=.15f;def.angularDamping=.6f;r->object.body=b3CreateBody(w->id,&def);
        r->object.active=true;b3Body_SetUserData(r->object.body,&r->object.tag);
        b3Body_SetBullet(r->object.body,true);b3Body_EnableContactRecycling(r->object.body,false);
        r->object.shape=b3_nullShapeId;r->object.query_mesh=(b3Mesh){0};
        r->object.locked=r->object.trapped=r->object.peek=false;r->object.breach_owner=r->object.wedge_owner=-1;r->object.breach_ticks=0;
        b3ShapeDef shape=b3DefaultShapeDef();shape.baseMaterial=swat_physics_material(r->object.material);
        shape.density=swat_material(r->object.material)->density;
        for(int i=0;i<prepared->section_count;i++) {
            b3ShapeId created=b3CreateHullShape(r->object.body,&shape,prepared->pending_hulls[i]);
            if(!i)r->object.shape=created;
            b3DestroyHull(prepared->pending_hulls[i]);
        }
        free(prepared->pending_hulls);prepared->pending_hulls=NULL;r->object.cuts=prepared;
    }
    r->state=*s;r->object.active=true;r->object.health=s->health;r->object.center=s->position;
    b3Body_SetTransform(r->object.body,s->position,s->rotation);
    b3Body_SetLinearVelocity(r->object.body,s->velocity);b3Body_SetAngularVelocity(r->object.body,s->angular_velocity);b3Body_SetAwake(r->object.body,s->awake);
}
static void support_preserve(SwatWorld* w,int owner) {
    if(!w->support || w->support->busy || w->objects[owner].support_role!=SWAT_SUPPORT_MEMBER || w->support->saved[owner])return;
    w->support->saved[owner]=true;w->support->before[owner]=w->objects[owner].cuts;w->objects[owner].cuts=NULL;
}
static void support_spawn_component(SwatWorld* w,int owner,const SwatFracturePolygon* polygons,int count) {
    if(!count)return;
    if(!w->rubble){w->rubble=calloc(1,sizeof(*w->rubble));if(!w->rubble)return;}
    for(int first=0;first<count;first+=SWAT_RUBBLE_SECTIONS) {
        int n=count-first;if(n>SWAT_RUBBLE_SECTIONS)n=SWAT_RUBBLE_SECTIONS;
        int slot=w->rubble->count;
        if(slot==SWAT_RUBBLE_CAPACITY) {
            slot=0;for(int i=1;i<w->rubble->count;i++) {
                const SwatRubbleState* a=&w->rubble->pieces[i].state;const SwatRubbleState* b=&w->rubble->pieces[slot].state;
                if((!a->active && b->active) || (a->active==b->active && ((!a->awake && b->awake) || (a->awake==b->awake && a->serial<b->serial))))slot=i;
            }
        }
        SwatObject* o=&w->objects[owner];SwatFracturePolygon source;swat_source_polygon(o,&source);float area=0;
        for(int i=0;i<n;i++)area+=swat_polygon_area(&polygons[first+i]);
        if(area<.00001f)continue;
        SwatRubbleState state={.serial=w->rubble->serial+1,.active=true,.awake=true,.source=owner,.section_count=n,
            .health=fminf(o->max_health,fmaxf(1,o->max_health*area/swat_polygon_area(&source))),.position=o->center,.rotation=swat_object_rotation(o)};
        memcpy(state.sections,polygons+first,(size_t)n*sizeof(*polygons));
        for(int i=0;i<n;i++) {
            SwatFracturePolygon* p=&state.sections[i];
            memset(p->vertices+p->count,0,(size_t)(SWAT_CUT_VERTICES-p->count)*sizeof(*p->vertices));
        }
        state.angular_velocity=b3RotateVector(state.rotation,swat_v(0,0,.5f));
        SwatFracture* prepared=swat_world_rubble_prepare(w,&state);if(!prepared)continue;
        if(slot==w->rubble->count)w->rubble->count++;
        w->rubble->serial=state.serial;swat_world_rubble_restore(w,slot,&state,prepared);
    }
}
static void support_spawn(SwatWorld* w,int owner,const SwatFracturePolygon* polygons,int count) {
    if(!count)return;
    SupportNode* nodes=calloc((size_t)count,sizeof(*nodes));if(!nodes)return;
    for(int i=0;i<count;i++){nodes[i].polygon=polygons[i];nodes[i].parent=i;}
    for(int i=0;i<count;i++)for(int j=0;j<i;j++)if(support_contact(&nodes[i],&nodes[j])) {
        int a=support_root(nodes,i),b=support_root(nodes,j);if(a!=b)nodes[a>b?a:b].parent=a<b?a:b;
    }
    SwatFracturePolygon component[SWAT_CUT_SECTIONS];
    for(int root=0;root<count;root++)if(support_root(nodes,root)==root) {
        int n=0;for(int i=0;i<count;i++)if(support_root(nodes,i)==root)component[n++]=polygons[i];
        support_spawn_component(w,owner,component,n);
    }
    free(nodes);
}
void swat_world_support_commit(SwatWorld* w,SwatSupportPlan* plan,bool spawn) {
    if(!plan)return;
    if(w->support){w->support->busy=true;w->support->builds++;}
    for(int i=0;i<w->count;i++)if(plan->cuts[i]) {
        SwatObject* o=&w->objects[i];float before=0,after=0;
        if(o->cuts)for(int s=0;s<o->cuts->section_count;s++)before+=swat_polygon_area(&o->cuts->sections[s]);
        else {SwatFracturePolygon p;swat_source_polygon(o,&p);before=swat_polygon_area(&p);}
        for(int s=0;s<plan->cuts[i]->section_count;s++)after+=swat_polygon_area(&plan->cuts[i]->sections[s]);
        swat_world_cut_apply(w,i,plan->cuts[i]);plan->cuts[i]=NULL;
        if(!o->cuts->section_count)swat_world_damage(w,i,o->max_health);
        else if(before>0)o->health*=fminf(1,after/before);
        if(o->active && o->breach_owner>=0) {
            b3Vec3 mount=b3InvRotateVector(swat_object_rotation(o),b3SubPos(o->breach_position,o->center));
            if(!swat_fracture_contains(o,mount.y,mount.z)){o->breach_owner=-1;o->breach_ticks=24;}
        }
    }
    if(spawn)for(int i=0;i<w->count;i++)support_spawn(w,i,plan->lost[i],plan->lost_count[i]);
    if(w->support) {
        for(int i=0;i<w->count;i++)if(w->support->saved[i]){swat_world_cut_close(w->support->before[i]);w->support->before[i]=NULL;w->support->saved[i]=false;}
        w->support->busy=false;
    }
    w->generation++;swat_world_support_close(plan);
}
static void support_flush(SwatWorld* w) {
    if(!w->support || w->support->busy || w->support->batch || !w->support->dirty)return;
    SwatSupportPlan* plan=swat_world_support_prepare(w,NULL,w->support->dirty);if(!plan)return;
    w->support->dirty=0;swat_world_support_commit(w,plan,true);
}
static void support_changed(SwatWorld* w,int owner) {
    if(!w->support || w->support->busy || !w->objects[owner].support_role)return;
    w->support->dirty|=swat_world_support_mask(w,w->objects[owner].wall_group);support_flush(w);
}
static void support_begin(SwatWorld* w){if(w->support)w->support->batch++;}
static void support_end(SwatWorld* w){if(w->support){assert(w->support->batch>0);w->support->batch--;support_flush(w);}}
void swat_world_support_begin(SwatWorld* w){support_begin(w);}
void swat_world_support_end(SwatWorld* w){support_end(w);}
void swat_world_sync_rubble(SwatWorld* w) {
    support_flush(w);if(!w->rubble)return;
    for(int i=0;i<w->rubble->count;i++) {
        SwatRubble* r=&w->rubble->pieces[i];if(!r->object.active)continue;
        b3WorldTransform pose=b3Body_GetTransform(r->object.body);r->object.center=r->state.position=pose.p;r->state.rotation=pose.q;
        r->state.velocity=b3Body_GetLinearVelocity(r->object.body);r->state.angular_velocity=b3Body_GetAngularVelocity(r->object.body);r->state.awake=b3Body_IsAwake(r->object.body);
        if(pose.p.y< -8 || b3Distance(pose.p,r->origin)>60)rubble_remove(r);
    }
}
bool swat_world_hit_impact(SwatWorld* w,SwatHit hit,float damage,bool door_round) {
    if(hit.kind==SWAT_HIT_WORLD)return swat_world_impact_at(w,hit.index,hit.point,damage,door_round);
    if(hit.kind!=SWAT_HIT_RUBBLE || !w->rubble || hit.index<0 || hit.index>=w->rubble->count || !isfinite(damage) || damage<=0)return false;
    SwatRubble* r=&w->rubble->pieces[hit.index];if(!r->object.active)return false;
    b3Vec3 impulse=swat_mul(hit.normal,-fminf(fminf(4,damage*.045f),b3Body_GetMass(r->object.body)*2));
    b3Vec3 lever=b3SubPos(hit.point,b3Body_GetWorldCenter(r->object.body));
    b3Vec3 angular=b3MulMV(b3Body_GetWorldInverseRotationalInertia(r->object.body),b3Cross(lever,impulse));
    float spin=b3Length(angular);if(spin>6)impulse=swat_mul(impulse,6/spin);
    b3Body_ApplyLinearImpulse(r->object.body,impulse,hit.point,true);
    r->object.health=r->state.health=fmaxf(0,r->state.health-damage);
    if(r->state.health<=0){rubble_remove(r);return true;}
    r->state.velocity=b3Body_GetLinearVelocity(r->object.body);r->state.angular_velocity=b3Body_GetAngularVelocity(r->object.body);r->state.awake=b3Body_IsAwake(r->object.body);return false;
}
#endif
