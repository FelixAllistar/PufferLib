"""Export the original-timing Standing F fixture without saving its source file.

Usage: blender -b --python export_fixture.py -- SOURCE.blend OUTPUT_DIRECTORY
"""
import bpy, hashlib, json, sys
from pathlib import Path

args = sys.argv[sys.argv.index('--') + 1:]
source, outdir = Path(args[0]).resolve(), Path(args[1]).resolve()
outdir.mkdir(parents=True, exist_ok=True)
source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
assert source_hash == 'a7a6d9ae7e13f30ac8de4b6347eb47062344768c04228b054388d9af726156b6'
bpy.ops.wm.open_mainfile(filepath=str(source))
rig = bpy.data.objects['SWAT_Mixamo_Rig']
action = rig.animation_data.action
assert action.name == 'Standing Empty / Coordinated Contact Cleanup F'
assert list(action.frame_range) == [0.0, 144.0]
for track in list(rig.animation_data.nla_tracks):
    rig.animation_data.nla_tracks.remove(track)
for other in list(bpy.data.actions):
    if other != action:
        bpy.data.actions.remove(other)
scene = bpy.context.scene
scene.render.fps, scene.render.fps_base = 24, 1.0
scene.frame_start, scene.frame_end = 0, 144
scene.frame_set(0)
names = ['SWAT_Mixamo_Rig', 'Rebuilt SWAT full body', 'Rifle 7',
         'Removed magazine', 'Fresh magazine', 'Rebuilt spare magazine sleeve']
bpy.ops.object.select_all(action='DESELECT')
for name in names:
    obj = bpy.data.objects[name]
    obj.hide_render = obj.hide_viewport = False
    obj.hide_set(False)
    obj.select_set(True)
bpy.context.view_layer.objects.active = rig
settings = dict(export_format='GLB', use_selection=True, export_animations=True,
                export_animation_mode='ACTIONS', export_anim_single_armature=True,
                export_force_sampling=False, export_frame_range=False,
                export_skins=True, export_all_influences=True, export_def_bones=False,
                export_optimize_animation_size=False, export_cameras=False,
                export_lights=False, export_anim_slide_to_zero=True, export_yup=True)
output = outdir / 'standing_f_original_timing.glb'
bpy.ops.export_scene.gltf(filepath=str(output), **settings)
assert hashlib.sha256(source.read_bytes()).hexdigest() == source_hash
report = {'schema': 'swat-import-fixture-export/1', 'status': 'exported_pending_validation',
          'source_file': source.name, 'source_sha256': source_hash,
          'action': action.name, 'source_frames': [0, 144], 'source_fps': 24,
          'duration_seconds': 6, 'loop': False, 'blender_version': bpy.app.version_string,
          'settings': settings, 'selected_objects': names, 'source_unchanged': True,
          'output': output.name, 'bytes': output.stat().st_size,
          'sha256': hashlib.sha256(output.read_bytes()).hexdigest(),
          'asset_quality_approved': False, 'gameplay_timing_adapted': False,
          'engine_runtime_tested': False}
(outdir / 'export_report.json').write_text(json.dumps(report, indent=2))
print('FIXTURE_EXPORTED', json.dumps(report), flush=True)
