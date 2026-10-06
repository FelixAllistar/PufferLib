# Independent static Crouch Ready validation

PASS for the static export and its measured metadata. This is one pose at t=0, with zero stored time span and no authored duration, loop, motion, transition, ADS or gameplay-camera approval.

- GLB: `crouch_ready_planted_shared_n_static.glb`, 3,620,792 bytes, SHA256 `54957d0769b6ecaaa164969f0c0315316f6834714044a2b376364f2a32fd6d66`
- Source: `crouch_ready_n.editable.blend`, SHA256 `b316df0d4c4af329dffe5f5330c502da14d17c0d3af77eecd257262a435445d9`
- Source checkpoint: 18,715,894 bytes, SHA256 `6fb887ef7551d9cf23f45ee1a394609a121fa649bfb5695b7ff1f6f382e2cf10`

## Actual binary and independent Blender checks

A separate NumPy parser reads the actual GLB chunks, view offsets, component types, accessor shapes/strides, all hierarchy links and all stored skin influences. It uses normalized XYZW quaternion FK and full-influence linear blend skinning. No exporter or producer math is imported. A fresh stock Blender 4.3.2 capture supplies native and reimport evaluated references.

The source has 700 curves and 700 keys, all at frame 0. The GLB has 210 TRS channels, each with one explicit STEP key at t=0, 70 skin joints, six primitives and 48,755 seam-split vertices. Maximum key-quaternion norm residual is 3.89e-8. Every exported vertex resolves uniquely to its native source vertex by exact bind position and normalized weights; every oriented source triangle is preserved.

Maximum absolute joint-world matrix error versus fresh native Blender is 5.574e-7. Maximum evaluated vertex error is 8.117e-7 m versus native Blender and 7.005e-7 m versus stock Blender reimport. Direct native-to-reimport error is at most 9.349e-7 m. Reimport keeps 700 single keys at frame 0, every stored normalized influence, and oriented topology.

`Prop_Magazine_B` is explicitly noncollapsed: node 69, skin slot 69, world linear determinant 1.000000775 and singular values within 6e-7 of one. The visible stored magazine state is preserved.

## Immutable geometry, normal and material scope

Actual static accessor definitions and all 39 static arrays match the pinned ordinary forward GLB exactly, including rest POSITION, NORMAL, UV, joint/weight arrays, indices and inverse binds. Nodes, skins, meshes, scene hierarchy and material JSON are identical. A separate fresh native-source comparison verifies exact rest coordinates, all weights, oriented triangle topology, loop normals, UV arrays, material slots and face assignments, bone rest frames and hierarchy. Body POSITION/NORMAL/UV/index arrays also match the pinned Ready empty export identified by the material metadata.

This verifies normal/UV/material identity, not runtime shader appearance. The GLB embeds no texture images or tangents; original texture maps are a separately pinned package. Runtime normal decoding, tangent generation and rendered material appearance remain consumer validation.

## Measured static fit

The armature scene root is identity and the native metre scale is retained. The known Blender-to-glTF basis is (x,y,z) to (x,z,-y). Independent binary skinning gives:

- Body top: 1.359917491 m; vertical extent: 1.355917609 m
- Lowest left/right sole: 3.999882 / 4.000151 mm above the authored plane
- Both source-qualified foot patches still contain 110 native vertices within 8 mm of the minimum; there are no below-plane sole vertices
- Visor surface midpoint height: 1.188157002 m
- Physical rifle forward elevation: −19.956483 degrees
- Visor-to-sightline perpendicular distance: 147.867828 mm; surface-normal/sightline angle: 14.584255 degrees

The visor is the midpoint of original body vertices 23263 and 23516, both entirely Head-weighted. Head is scene node 1 but skin-joint slot 5. Its correctly transformed optical frame exactly matches the midpoint under the independent evaluator. It remains a surface proxy, not an anatomical eye or camera.

The rifle is node 67 and skin slot 67. Its rigid bridge is `Wnode @ IBM[resolved_slot] @ B_bind_mesh`, equivalently `Wnode @ J_joint_local` where `J = IBM @ B`. All 5,346 original rifle vertices replay through this bridge against the fresh native evaluated mesh to 1.21e-7 m. The separately supplied native bridge reference also agrees to 1.38e-8 m. Omitting the inverse bind produces a materially different transform. Converted source bone-frame matrices use the source coordinate-basis conversion and are explicitly distinct from actual exported node transforms.

Fresh independent ray tests against every actual GLB skinned body triangle reproduce localized stock backing: center gap 2.191895 mm; all 29 points in the 3 mm patch hit, with 24 within 5 mm. The full 5 mm patch has 81 hits, 45 within 5 mm. This is localized curved-gear support, not flush or simulated seating. The source's existing cloth, brace, shoulder-root and sleeve-mount intersection limits are retained; this export review does not reclassify them.

The preview support mapping separately selects >0.5 cumulative same-side Foot/Toe weight and a 1 mm patch, while the source QA uses >0.65 and an 8 mm patch. Both namespaces, selections and all native-to-export mappings are checked. The fixed preview transform maps measured rifle heading to engine +X and the documented foot origin to zero at scale one, without baking this transform into the asset. It does not establish controller stance, collision height or camera fit.

## Portable replay

From the extracted fixture package:

    python independent_validation/replay_fixture.py --package . --output replay.json

Requires Python 3 and NumPy. The entrypoint replays the actual binary, checks all native and reimport reference vertices, metadata, bridge, visor, ground, stock rays, and verifies actual static arrays against a separately captured baseline digest manifest. It does not require Blender, the authoring workspace, or producer Python modules. The included reference captures are hash-pinned fresh Blender outputs, not values predicted by the GLB evaluator.

For a fresh source or stock reimport capture, use the separately supplied pinned source and Blender:

    blender -b --factory-startup --python independent_validation/capture_blender.py -- source.blend source_capture
    blender -b --factory-startup --python independent_validation/capture_blender.py -- crouch_ready_planted_shared_n_static.glb reimport_capture

`replay_static.py` additionally accepts `--baseline-glb` and `--baseline-native-capture` for direct baseline comparison. The baseline GLB and native `.blend` remain separately pinned in `SOURCE_IDENTITY_CHECK.json`; neither is required by the small package's NumPy replay. Source/reimport capture JSON states file hashes and Blender version. Capturing never saves either opened source or imported scene.
