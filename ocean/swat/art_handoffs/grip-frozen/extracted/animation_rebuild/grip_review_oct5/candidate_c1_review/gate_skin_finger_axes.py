import bpy,json,math,importlib.util
import numpy as np
from pathlib import Path
R=Path('/workspace/scratch/eac4961518d4/animation_rebuild');O=R/'grip_review_oct5/candidate_c1_review';spec=importlib.util.spec_from_file_location('m','/workspace/shared/practical_ads_audit_recovery_algorithms.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def unit(v):return v/max(np.linalg.norm(v),1e-20)
def angle(a,b):return float(np.degrees(np.arccos(np.clip(unit(a)@unit(b),-1,1))))
from gate_config import INPUTS,verify_loaded,sha
for label,rel,pin in INPUTS:
 assert sha(rel)==pin
 bpy.ops.wm.open_mainfile(filepath=str(R/rel));verify_loaded(bpy,label);bpy.context.scene.frame_set(0);bpy.context.view_layer.update();b=bpy.data.objects.get('SWAT_Wearer') or bpy.data.objects['Rebuilt SWAT full body'];rig=bpy.data.objects['SWAT_Mixamo_Rig'];b.data.calc_loop_triangles();T=np.array([q.vertices[:] for q in b.data.loop_triangles]);W=[{b.vertex_groups[q.group].name:q.weight for q in v.groups} for v in b.data.vertices];RV=np.array([b.matrix_world@v.co for v in b.data.vertices]);V=np.array(m.evaluated_positions(b,bpy.context.evaluated_depsgraph_get()));g=bpy.data.objects['Rifle 7'];G=np.array(m.evaluated_positions(g,bpy.context.evaluated_depsgraph_get()));GR=np.array([v.co[:] for v in g.data.vertices]);A=np.eye(4);A[:,:3]=np.linalg.lstsq(np.c_[GR,np.ones(len(GR))],G,rcond=None)[0];inv=np.linalg.inv(A);d=json.loads((O/(label+'_static_skin_grip_audit.json')).read_text())
 for side in ['Left','Right']:
  for digit in ['Thumb','Index','Middle','Ring','Pinky']:
   prefix='mixamorig:'+side+'Hand'+digit;weights=np.array([sum(v for k,v in w.items() if k.startswith(prefix)) for w in W]);ti=np.flatnonzero(weights[T].mean(1)>.1);segs=[];axes=[]
   for k in [1,2,3]:
    bone=rig.data.bones[prefix+str(k)];BR=np.array(rig.matrix_world@bone.matrix_local);origin=BR[:3,3];axis=unit(BR[:3,1]);axial=(RV-origin)@axis;ringcenters=[];rings=[]
    for f in [.25,.75]:
     where=f*bone.length;segments=[]
     for i in ti:
      t=T[i];s=axial[t]-where
      if min(s)>0 or max(s)<0:continue
      points=[]
      for a,c in [(0,1),(1,2),(2,0)]:
       if s[a]*s[c]>=0:continue
       u=float(s[a]/(s[a]-s[c]));p=V[t[a]]+u*(V[t[c]]-V[t[a]]);points.append(p)
      if len(points)==2:segments.append(points)
     assert segments, (label,side,digit,k,f, len(ti),float(axial[T[ti]].min()),float(axial[T[ti]].max()),where);lengths=np.array([np.linalg.norm(q[1]-q[0]) for q in segments]);center=np.average(np.array(segments).mean(1),axis=0,weights=lengths);ringcenters.append(center);rings.append({'fraction_along_native_segment':f,'surface_intersection_segments':len(segments),'evaluated_ring_perimeter_mm':float(sum(lengths)*1000),'evaluated_skin_ring_center_rifle_m':(np.r_[center,1]@inv)[:3].tolist()})
    ax=unit(ringcenters[1]-ringcenters[0]);axes.append(ax);segs.append({'segment':k,'skin_ring_axis_rifle':unit(ax@inv[:3,:3]).tolist(),'skin_rings':rings})
   d['hands'][side]['finger_skin_segments'][digit]['evaluated_skin_cross_section_axes']={'method':'Native material rings at 25% and 75% along each rest finger segment, actual triangle/plane intersection edge parameters carried into evaluated skin; perimeter-weighted ring centers define each posed segment axis. Bone rest frames locate material rings only; posed bone axes do not supply the reported orientation. Unsigned axis turns include lateral deviation and are not pure clinical joint angles.','segments':segs,'proximal_to_middle_skin_axis_turn_deg':angle(axes[0],axes[1]),'middle_to_distal_skin_axis_turn_deg':angle(axes[1],axes[2])}
   print(label,side,digit,angle(axes[0],axes[1]),angle(axes[1],axes[2]),flush=True)
 (O/(label+'_static_skin_grip_audit.json')).write_text(json.dumps(d,indent=2)+'\n')

for label,path,pin in INPUTS:assert sha(path)==pin
