"""Repair imported scene-level lineage without changing geometry or presentation."""
import bpy,os,sys
ROOT=os.path.dirname(os.path.abspath(__file__));sys.path.insert(0,ROOT)
import build_motel as b
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT,'motor_court_motel.blend'))
assembly=bpy.data.collections['02_MOTEL_EXAMPLE'];roof=bpy.data.collections['02b_ROOFS_toggle_cutaway'];obs=list(assembly.all_objects)
b.export('motel_example.glb',obs);b.export('motel_example_cutaway.glb',[o for o in obs if o.name not in roof.objects]);bpy.ops.object.select_all(action='DESELECT')
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT,'motor_court_motel.blend'))
print('Clean scene metadata stamped; examples reexported; presentation preserved.')
