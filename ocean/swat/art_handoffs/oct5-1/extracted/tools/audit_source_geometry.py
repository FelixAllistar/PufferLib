"""Read-only source representability audit. Does not export or save a Blender file."""
import bpy, sys, hashlib, json, math
import numpy as np
from pathlib import Path
from collections import Counter, defaultdict
src=Path(sys.argv[sys.argv.index('--')+1]).resolve(); out=Path(sys.argv[sys.argv.index('--')+2]).resolve()
before=hashlib.sha256(src.read_bytes()).hexdigest()
assert before=='f85525ba445013f73884677bd8202d7ffff226b04fedee930250c90cbe402770'
bpy.ops.wm.open_mainfile(filepath=str(src)); rig=bpy.data.objects['SWAT_Mixamo_Rig']; action=rig.animation_data.action
fps=bpy.context.scene.render.fps/bpy.context.scene.render.fps_base
counts=Counter(); spans=Counter(); modes=Counter(); groups=defaultdict(list);worst_time={};worst_value={};worst_derivative={};nonlinear=0;bez=0;straight=0
for f in action.fcurves:
 p=list(f.keyframe_points);groups[f.data_path].append(f);counts.update(x.interpolation for x in p);modes['+'.join(sorted(set(x.interpolation for x in p)))]+=1
 for k,(a,b) in enumerate(zip(p,p[1:])):
  mode=a.interpolation;spans[mode]+=1
  if mode!='BEZIER':continue
  bez+=1;x0,y0=map(float,a.co);x1,y1=map(float,b.co);hx0,hy0=map(float,a.handle_right);hx1,hy1=map(float,b.handle_left);d=x1-x0
  tx1=(hx0-x0)/d;tx2=(hx1-x0)/d
  te=max(abs(tx1-1/3),abs(tx2-2/3));nonlinear+=te>0
  record={'path':f.data_path,'component':f.array_index,'frames':[x0,x1]}
  if te>worst_time.get('normalized_error',-1):worst_time=dict(record,normalized_error=te,absolute_frame_error=max(abs(hx0-(x0+d/3)),abs(hx1-(x0+2*d/3))),normalized_x_handles=[tx1,tx2])
  du0=(hy0-y0)/tx1;du1=(y1-hy1)/(1-tx2)
  u=np.arange(1,16)/16.;z=u.copy()
  for _ in range(8):
   X=3*(1-z)**2*z*tx1+3*(1-z)*z*z*tx2+z**3
   DX=3*(1-z)**2*tx1+6*(1-z)*z*(tx2-tx1)+3*z*z*(1-tx2)
   z-=(X-u)/DX
  val=(1-z)**3*y0+3*(1-z)**2*z*hy0+3*(1-z)*z*z*hy1+z**3*y1
  herm=(2*u**3-3*u*u+1)*y0+(u**3-2*u*u+u)*du0+(-2*u**3+3*u*u)*y1+(u**3-u*u)*du1
  ve=float(np.max(abs(val-herm)))
  if ve>worst_value.get('error',-1):worst_value=dict(record,error=ve,at_fraction=float(u[np.argmax(abs(val-herm))]))
  DY=3*(1-z)**2*(hy0-y0)+6*(1-z)*z*(hy1-hy0)+3*z*z*(y1-hy1)
  hd=(6*u*u-6*u)*y0+(3*u*u-4*u+1)*du0+(-6*u*u+6*u)*y1+(3*u*u-2*u)*du1
  de=float(np.max(abs(DY/DX-hd)))*fps/d
  if de>worst_derivative.get('error_per_s',-1):worst_derivative=dict(record,error_per_s=de,at_fraction=float(u[np.argmax(abs(DY/DX-hd))]))
qrows=[];grid_mismatch=[]
for path,fs in groups.items():
 fs.sort(key=lambda f:f.array_index);ts=[tuple(float(k.co.x) for k in f.keyframe_points) for f in fs]
 if len(set(ts))>1:grid_mismatch.append({'path':path,'key_counts':[len(t)for t in ts]})
 if not path.endswith('rotation_quaternion'):continue
 t=sorted(set(x for row in ts for x in row));qs=np.array([[f.evaluate(x) for f in fs]for x in t],float);ns=np.linalg.norm(qs,axis=1);qs/=ns[:,None]
 qrows.append({'path':path,'max_key_norm_deviation':float(np.max(abs(ns-1))),'norm_min':float(ns.min()),'norm_max':float(ns.max()),'minimum_neighbor_dot':float(np.min(np.sum(qs[:-1]*qs[1:],axis=1))),'key_count':len(t)})
rest=[];flags=[]
for b in rig.pose.bones:
 M=np.array(b.bone.matrix_local,float)
 if b.parent:M=np.linalg.inv(np.array(b.parent.bone.matrix_local,float))@M
 A=M[:3,:3];rest.append({'bone':b.name,'orthogonality_max_abs':float(np.max(abs(A.T@A-np.eye(3)))),'determinant':float(np.linalg.det(A))})
 if b.rotation_mode!='QUATERNION' or b.constraints or not b.bone.use_inherit_rotation or b.bone.inherit_scale!='FULL' or not b.bone.use_local_location:flags.append(b.name)
assert hashlib.sha256(src.read_bytes()).hexdigest()==before
result={'source_sha256':before,'source_unchanged':True,'blender':bpy.app.version_string,'action':action.name,'frame_range':list(action.frame_range),'fps':fps,'fcurve_count':len(action.fcurves),'key_modes':dict(counts),'curve_modes':dict(modes),'span_modes':dict(spans),'bezier_spans':bez,'bezier_spans_with_nonexact_time_thirds':nonlinear,'worst_time_handle_residual':worst_time,'worst_scalar_hermite_approximation_error_15_probes_per_bezier_span':worst_value,'worst_scalar_derivative_approximation_error_15_probes_per_bezier_span':worst_derivative,'groups_with_different_component_key_grids':grid_mismatch,'worst_quaternion_key_norm':max(qrows,key=lambda q:q['max_key_norm_deviation']),'quaternion_keys':qrows,'worst_rest_rotation_orthogonality':max(rest,key=lambda q:q['orthogonality_max_abs']),'rest_basis':rest,'unsupported_FK_flags':flags,'rig_matrix_world':np.array(rig.matrix_world,float).tolist(),'method':'Direct saved handle audit. At 15 interior fractions per BEZIER span, invert cubic Bezier x by eight Newton steps, compare y(x) and dy/dt to endpoint-slope Hermite. These probes are measurements, not proven global maxima.'}
out.write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items()if k not in ['quaternion_keys','rest_basis','groups_with_different_component_key_grids']},indent=2));print('mismatched_component_grids',len(grid_mismatch))
