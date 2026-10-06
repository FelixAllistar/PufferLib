"""Read-only dump of native static-pose curves, bind matrices and evaluated controls."""
import bpy,json,hashlib,sys
from collections import Counter
from pathlib import Path
source,out=map(Path,sys.argv[sys.argv.index('--')+1:]);h=hashlib.sha256(source.read_bytes()).hexdigest();assert h=='b316df0d4c4af329dffe5f5330c502da14d17c0d3af77eecd257262a435445d9'
bpy.ops.wm.open_mainfile(filepath=str(source.resolve()));rig=bpy.data.objects['SWAT_Mixamo_Rig'];a=rig.animation_data.action
assert a.name=='Crouch Ready / Planted Shared N Low-Ready';assert list(a.frame_range)==[0,0]
curves=[]
for f in a.fcurves:
 assert not f.modifiers and len(f.keyframe_points)==1 and f.keyframe_points[0].co.x==0
 curves.append({'data_path':f.data_path,'component':f.array_index,'extrapolation':f.extrapolation,'keys':[[*k.co,*k.handle_left,*k.handle_right,k.interpolation]for k in f.keyframe_points]})
bones=[]
for b in rig.pose.bones:
 assert b.rotation_mode=='QUATERNION' and not b.constraints and b.bone.use_inherit_rotation and b.bone.inherit_scale=='FULL' and b.bone.use_local_location
 bones.append({'name':b.name,'parent':b.parent.name if b.parent else None,'rest_world_native':[list(row)for row in rig.matrix_world@b.bone.matrix_local],'rest_armature_native':[list(row)for row in b.bone.matrix_local]})
result={'schema':'swat-native-animation-curves/1','source_sha256':h,'action':a.name,'fps':bpy.context.scene.render.fps/bpy.context.scene.render.fps_base,'frame_range':[0,0],'authored_duration_s':None,'rig_world':[list(row)for row in rig.matrix_world],'bones':bones,'curves':curves,'interpolation_counts':dict(Counter(k[-1]for f in curves for k in f['keys'])),'curve_count':len(curves),'key_count':sum(len(f['keys'])for f in curves),'matrix_layout':'row-major arrays','key_layout':['frame','value','left_handle_frame','left_handle_value','right_handle_frame','right_handle_value','outgoing_interpolation']}
out.write_text(json.dumps(result,separators=(',',':'))+'\n');assert hashlib.sha256(source.read_bytes()).hexdigest()==h;print({k:result[k]for k in ['source_sha256','action','curve_count','key_count','interpolation_counts']})
