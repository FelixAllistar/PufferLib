"""Independent actual-binary physical landmarks, visor, ground and stock rays.
NumPy only. Uses pinned measured source landmark definitions, never a producer matrix.
"""
import json,argparse
from pathlib import Path
import numpy as np
from replay_static import GLB,C,mat,source_parity

def unit(v):return v/np.linalg.norm(v)
def elevation(v):return float(np.degrees(np.arcsin(np.clip(unit(v)[1],-1,1))))
def angle(a,b):return float(np.degrees(np.arccos(np.clip(unit(a)@unit(b),-1,1))))
def ray(origin,direction,tri,maxdist=.15):
 e1=tri[:,1]-tri[:,0];e2=tri[:,2]-tri[:,0];p=np.cross(direction,e2);det=np.einsum('ij,ij->i',e1,p);valid=abs(det)>1e-12;inv=np.zeros(len(det));inv[valid]=1/det[valid];s=origin-tri[:,0];u=np.einsum('ij,ij->i',s,p)*inv;q=np.cross(s,e1);v=q@direction*inv;t=np.einsum('ij,ij->i',e2,q)*inv;mask=valid&(u>=-1e-10)&(v>=-1e-10)&(u+v<=1+1e-10)&(t>=0)&(t<=maxdist)
 return float(t[mask].min()) if mask.any() else None

def inspect(g,prefix,refs):
 report,z,meta,maps,worlds=source_parity(g,prefix);W=g.world();root=g.names.index('SWAT_Mixamo_Rig');assert np.array_equal(W[root],np.eye(4));rifleslot=g.joint_names.index('Prop_Rifle');riflenode=g.joints[rifleslot];headslot=g.joint_names.index('mixamorig:Head');headnode=g.joints[headslot];rifle=refs['rifle']['data'];optic=refs['optical']['data'];expect=refs['measurements']['data']
 # Construct bridge from calibrated rigid axes and stock origin in original native mesh coordinates.
 landmarks={p['id']:np.array(p['position_native_rifle']) for p in rifle['landmarks']};E=np.array([[0.,0,-1,0],[-1,0,0,0],[0,1,0,0],[0,0,0,1.]]);E[:3,3]=landmarks['stock_contact'];B=C@E;J=g.ibm[rifleslot]@B;M=W[riflenode]@g.ibm[rifleslot]@B;D=W[riflenode]@g.ibm[rifleslot]
 rmi=meta['mesh_names'].index('Rifle 7');rigid=np.c_[z[f'p{rmi}'],np.ones(len(z[f'p{rmi}']))]@np.linalg.inv(E).T;rigid_actual=(rigid@M.T)[:,:3];rigid_source=z[f'eval{rmi}']@C[:3,:3].T;rigid_error=float(np.linalg.norm(rigid_actual-rigid_source,axis=1).max());assert rigid_error<1e-5
 ni=list(z['bone_names']).index('Prop_Rifle');native=C@z['bone_world'][ni]@np.linalg.inv(z['bone_rest'][ni])@E;assert abs(M-native).max()<1e-5
 points={n:(D@C@np.r_[p,1])[:3] for n,p in landmarks.items()};point_errors={n:float(np.linalg.norm(p-C[:3,:3]@expect['rifle']['world_landmarks'][n])) for n,p in points.items()};assert max(point_errors.values())<1e-5
 mi=meta['mesh_names'].index('Rebuilt SWAT full body');body={};triangles=[]
 for pr in g.primitives:
  if pr['name']!='Rebuilt SWAT full body':continue
  key=(pr['node'],pr['primitive']);p=worlds[key];triangles.extend(p[pr['tris']])
  for vi,pt in zip(maps[key],p):
   if int(vi) in body:assert np.linalg.norm(body[int(vi)]-pt)<1e-10
   body[int(vi)]=pt
 xyz=np.array(list(body.values()));bounds=np.array([xyz.min(0),xyz.max(0)]);assert abs(bounds[1,1]-expect['body_top_world_z_m'])<1e-5;feet={}
 for side in ['Left','Right']:
  ids=[i for i,w in enumerate(z[f'w{mi}']) if sum(v for name,v in zip(z['bone_names'],w) if name.startswith(tuple('mixamorig:'+side+k for k in ['Foot','ToeBase','Toe_End'])))>.65];p=np.array([body[i] for i in ids]);low=p[:,1].min();patch=p[p[:,1]<=low+.008];feet[side]={'native_subset_vertices':len(ids),'sole_min_y_m':float(low),'native_patch_vertices_within8mm':len(patch),'below_floor_vertices':int((p[:,1]<0).sum()),'patch_centroid_model_m':patch.mean(0).tolist()};assert len(patch)==110 and abs(low-expect['soles'][side]['minimum_z_m'])<1e-5
 lens={side:body[row['center_vertex_id']] for side,row in optic['landmarks'].items()};mid=(lens['left']+lens['right'])*.5;OB=C@np.array(optic['optical_frame_in_source_bind_mesh']);OF=W[headnode]@g.ibm[headslot]@OB;oe=float(np.linalg.norm(OF[:3,3]-mid));assert oe<1e-7
 for side,row in optic['landmarks'].items():
  vi=row['center_vertex_id'];w=z[f'w{mi}'][vi];assert w[list(z['bone_names']).index('mixamorig:Head')]==1 and np.count_nonzero(w)==1
 line=unit(points['front_sight']-points['rear_sight']);delta=mid-points['rear_sight'];axial=float(delta@line);perp=float(np.linalg.norm(delta-axial*line));normal=OF[:3,0]
 rear=D@C;direction=unit(rear[:3,:3]@np.array([0.,1,0]));patches={};tri=np.array(triangles)
 for radius in [0,3,5]:
  gaps=[];count=0
  for x in range(-radius,radius+1):
   for zc in range(-radius,radius+1):
    if x*x+zc*zc>radius*radius:continue
    count+=1;p=(rear@np.r_[landmarks['stock_contact']+np.array([x*.001,0,zc*.001]),1])[:3];d=ray(p,direction,tri)
    if d is not None:gaps.append(d*1000)
  patches[str(radius)]={'samples':count,'hits':len(gaps),'min_gap_mm':min(gaps),'max_gap_mm':max(gaps),'median_gap_mm':float(np.median(gaps)),'within5mm':sum(d<=5 for d in gaps)}
 assert patches['3']['hits']==29 and patches['3']['within5mm']==24 and abs(patches['0']['min_gap_mm']-2.1919214632362127)<.01
 return {'schema':'independent-crouch-static-measurement/1','status':'PASS','glb_sha256':g.sha,'units':'metres, glTF model +Y up','root':{'node':root,'identity_exact':True,'native_scale_preserved':True,'native_to_gltf_basis':'(x,y,z) -> (x,z,-y)'},'body':{'bounds_model_xyz_m':bounds.tolist(),'body_top_y_m':float(bounds[1,1]),'vertical_extent_m':float(bounds[1,1]-bounds[0,1]),'hips_model_xyz_m':W[g.names.index('mixamorig:Hips'),:3,3].tolist(),'source_top_error_m':float(abs(bounds[1,1]-expect['body_top_world_z_m']))},'soles':feet,'rifle':{'node':riflenode,'skin_joint_slot':rifleslot,'bridge_formula':'Wnode @ IBM[resolved skin slot] @ B_bind_mesh','B_bind_mesh_column_major':B.T.ravel().tolist(),'joint_local_attachment_column_major':J.T.ravel().tolist(),'native_rifle_vertices':len(rigid),'bridge_source_evaluated_all_vertex_max_error_m':rigid_error,'source_matrix_error_max':float(abs(M-native).max()),'two_correct_forms_error_max':float(abs(M-W[riflenode]@J).max()),'missing_inverse_bind_matrix_error_max':float(abs(W[riflenode]@B-M).max()),'landmark_native_error_m':point_errors,'landmarks_model_xyz_m':{n:p.tolist() for n,p in points.items()},'physical_forward_elevation_deg':elevation(M[:3,0]),'rear_front_sight_elevation_deg':elevation(line)},'visor':{'head_node':headnode,'head_skin_joint_slot':headslot,'classification':optic['classification'],'source_vertex_ids':[23263,23516],'midpoint_model_xyz_m':mid.tolist(),'midpoint_to_attachment_error_m':oe,'source_lens_error_max_m':max(float(np.linalg.norm(lens[side]-C[:3,:3]@p)) for side,p in zip(['left','right'],expect['visor']['actual_world_points'])),'surface_normal_elevation_deg':elevation(normal),'perpendicular_to_sightline_m':perp,'axial_coordinate_on_sightline_m':axial,'normal_to_sightline_angle_deg':angle(normal,line),'not_eye_relief_or_ADS':True},'stock_axial_rays':{'method':'Fresh NumPy two-sided Moller-Trumbore ray tests against all actual GLB skinned body triangles, native stock-pad grid and rearward axis, 150mm search','patches_mm':patches,'classification':'Localized support on curved shoulder gear; not flush seating or force simulation'},'limits':['Single static frame only; no invented loop, motion, temporal duration or physical balance','Authored sole clearance remains about4mm','Optical surface proxy is not anatomical eye or gameplay camera; no ADS approval','Source intersections and material limitations remain inherited as listed in pinned source QUALITY.md; this is transport validation']}

def main():
 p=argparse.ArgumentParser();p.add_argument('--glb',type=Path,required=True);p.add_argument('--source-capture',type=Path,required=True);p.add_argument('--references',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();r=inspect(GLB(a.glb),a.source_capture,json.loads(a.references.read_text()));a.output.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({k:v for k,v in r.items() if k in ['status','body','soles','visor','stock_axial_rays']},indent=2))
if __name__=='__main__':main()
