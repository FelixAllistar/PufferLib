// Original picnic table in the front court. Source is already metre/Y-up.
static const SwatMotelInstance picnic_placement={0,{6,-.08f,7.5f},{1,1,1},0,SWAT_WOOD,false,false};
const SwatRoadsidePart* swat_motel_picnic_part(int part){return part>=0&&part<SWAT_MOTEL_PICNIC_PARTS?&picnic_parts[part]:NULL;}
const SwatPicnicComponent* swat_motel_picnic_component(int component){return component>=0&&component<80?&picnic_components[component]:NULL;}
bool swat_motel_picnic(const SwatWorld*w,int owner,SwatMotelInstance*out){
 if(!w->motel||w->count!=SWAT_MOTEL_OBJECTS||owner<SWAT_MOTEL_PICNIC_FIRST||owner>=SWAT_MOTEL_OBJECTS)return false;
 *out=picnic_placement;return true;
}
uint64_t swat_motel_picnic_mask(const SwatWorld*w){
 if(!w->motel||w->count!=SWAT_MOTEL_OBJECTS)return 0;
 uint64_t mask=0;for(int i=0;i<SWAT_MOTEL_PICNIC_PARTS;i++)if(w->objects[SWAT_MOTEL_PICNIC_FIRST+i].active)mask|=UINT64_C(1)<<i;return mask;
}
uint64_t swat_motel_picnic_unstable_roots(const SwatWorld*w){
 if(!w->motel||w->count!=SWAT_MOTEL_OBJECTS)return 0;
 uint64_t mask=0;
 for(int i=0;i<SWAT_MOTEL_PICNIC_PARTS;i++){
  if(picnic_parts[i].parents[0]>=0||!w->objects[SWAT_MOTEL_PICNIC_FIRST+i].active)continue;
  bool attached=false;
  for(int j=i+1;j<SWAT_MOTEL_PICNIC_PARTS&&!attached;j++)if(w->objects[SWAT_MOTEL_PICNIC_FIRST+j].active)
   for(int k=0;k<SWAT_MAX_SUPPORTS&&picnic_parts[j].parents[k]>=0;k++)attached|=picnic_parts[j].parents[k]==i;
  if(!attached)mask|=UINT64_C(1)<<i;
 }
 return mask;
}
static b3Pos picnic_point(b3Vec3 point){return b3OffsetPos(picnic_placement.origin,point);}
static float picnic_health(const SwatRoadsidePart*p){const SwatMaterialDef*m=swat_material(p->material);return m->fracture_health*p->section/m->reference_thickness;}
static bool picnic_supports(const SwatWorld*w,int part,int supports[SWAT_MAX_SUPPORTS]){
 const SwatRoadsidePart*p=&picnic_parts[part];for(int i=0;i<SWAT_MAX_SUPPORTS;i++)supports[i]=p->parents[i]<0?-1:SWAT_MOTEL_PICNIC_FIRST+p->parents[i];
 if(p->parents[0]>=0)return true;
 const SwatObject*floor=&w->objects[0];if(floor->door||floor->fractured||floor->yaw||floor->pitch||floor->max_health!=0||floor->material!=SWAT_CONCRETE||!p->anchor_count)return false;
 for(int i=0;i<p->anchor_count;i++){b3Pos at=picnic_point(p->anchors[i]);if(fabs(at.x-floor->center.x)>floor->half.x||fabs(at.z-floor->center.z)>floor->half.z||fabs(at.y-floor->center.y-floor->half.y)>1e-5)return false;}
 supports[0]=0;return true;
}
static bool picnic_validate(const SwatWorld*w){
 if(w->count<=SWAT_MOTEL_PICNIC_FIRST)return true;
 if(w->count!=SWAT_MOTEL_OBJECTS)return false;
 for(int i=0;i<SWAT_MOTEL_PICNIC_PARTS;i++){
  const SwatRoadsidePart*p=&picnic_parts[i];const SwatObject*o=&w->objects[SWAT_MOTEL_PICNIC_FIRST+i];int supports[SWAT_MAX_SUPPORTS];
  if(!picnic_supports(w,i,supports)||memcmp(supports,o->supports,sizeof(supports))||b3Distance(o->center,picnic_point(p->center))>1e-4||b3Length(b3Sub(o->half,p->half))>1e-5f||o->yaw||o->pitch||o->door||o->fractured||o->wall_group||o->part!=SWAT_PART_FIXTURE||o->material!=p->material||o->structural_thickness!=p->section||o->max_health!=picnic_health(p))return false;
 }
 return true;
}
static void picnic_bind(SwatWorld*w){
 if(w->count!=SWAT_MOTEL_OBJECTS)return;
 for(int i=0;i<SWAT_MOTEL_PICNIC_PARTS;i++){
  SwatObject*o=&w->objects[SWAT_MOTEL_PICNIC_FIRST+i];if(!o->active)continue;const SwatRoadsidePart*p=&picnic_parts[i];swat_world_build_progress(w);
  b3Vec3*vertices=malloc((size_t)p->vertex_count*sizeof(*vertices));assert(vertices);for(int v=0;v<p->vertex_count;v++)vertices[v]=swat_mul(p->vertices[v],100);
  b3MeshDef mesh={.vertices=vertices,.indices=p->indices,.vertexCount=p->vertex_count,.triangleCount=p->triangle_count,.weldVertices=true,.weldTolerance=.0001f,.identifyEdges=true};b3MeshData*full=b3CreateMesh(&mesh,NULL,0);assert(full&&full->triangleCount==p->triangle_count);owned_mesh(w,full);free(vertices);
  b3DestroyShape(o->shape,false);o->query_mesh=(b3Mesh){full,{.01f,.01f,.01f}};b3ShapeDef shape=b3DefaultShapeDef();shape.baseMaterial=swat_physics_material(o->material);
  for(int start=0;start<full->triangleCount;start+=240){int count=full->triangleCount-start;if(count>240)count=240;int32_t selection[240];for(int j=0;j<count;j++)selection[j]=start+j;b3MeshData*subset=b3CreateMeshSubset(full,selection,count);assert(subset);owned_mesh(w,subset);b3ShapeId made=b3CreateMeshShape(o->body,&shape,subset,swat_v(.01f,.01f,.01f));assert(B3_IS_NON_NULL(made));if(!start)o->shape=made;}
 }
}
static void picnic_build(SwatWorld*w){
 assert(w->count==SWAT_MOTEL_PICNIC_FIRST);
 for(int i=0;i<SWAT_MOTEL_PICNIC_PARTS;i++){
  const SwatRoadsidePart*p=&picnic_parts[i];int id=swat_world_box(w,picnic_point(p->center),p->half,p->material,picnic_health(p));assert(id==SWAT_MOTEL_PICNIC_FIRST+i);SwatObject*o=&w->objects[id];o->part=SWAT_PART_FIXTURE;o->structural_thickness=p->section;
  int supports[SWAT_MAX_SUPPORTS],count=0;bool valid=picnic_supports(w,i,supports);assert(valid);(void)valid;while(count<SWAT_MAX_SUPPORTS&&supports[count]>=0)count++;valid=swat_world_attach(w,id,supports,count);assert(valid);(void)valid;
 }
 assert(picnic_validate(w));picnic_bind(w);
}
