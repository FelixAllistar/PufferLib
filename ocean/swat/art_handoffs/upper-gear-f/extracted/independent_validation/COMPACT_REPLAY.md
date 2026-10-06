# Portable F review and scope

The final GLB SHA-256 is 5dd68cf3010b4ba24ec719fb949e1d8f62053c5761142c37da41079848f00835. It passed independent actual-byte and stock Blender 4.3.2 checks against all 47,599 directly evaluated exported vertices at static neutral plus 361 movement times. That completed dense evidence is recorded in ACTUAL_BINARY_PARITY.json and STOCK_REIMPORT_PARITY.json; their approximately 80 MB dense reference arrays remain outside the portable package.

The portable references total 10,237,649 bytes. They retain complete bind positions, all skin weights, topology, material IDs, corner UVs/normals and all 72 joint matrices at every one of the 361 movement sample times. They retain direct Blender-evaluated vertex arrays only at 13 controls: 0, 0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 2.25, 2.5, 2.75 and 3 seconds, plus the separate static neutral reference.

Compact replay reconstructs all-time linear blend skinning from independently captured Blender joint matrices, native rest matrices, bind positions and all normalized native influences. It compares that algebraic reference with every GLB vertex at all 361 times. It separately compares the algebraic reference to the retained direct evaluated controls, with maximum residual below 0.000494 mm. COMPACT_BINARY_REPLAY.json and COMPACT_REIMPORT_REPLAY.json explicitly state this reference mode and the control times. They do not claim that the omitted dense direct arrays are included or freshly replayed.

The source was independently checked to contain only ordinary Armature skin deformation, with no live constraints/drivers. The new carrier inherit-scale behavior is verified by the captured world matrices and actual GLB FK. This does not approve broad animation, continuous collision, hands newly fitted to the rifle, ADS, camera placement, fastening forces or engine runtime behavior.

Run from the package root, using Python 3 and NumPy:

```sh
python independent_validation/replay_f.py --glb swat_upper_gear_remake_f_v1.glb --source-capture independent_validation/source_capture_compact --output compact_binary_replay.json
python independent_validation/check_reimport_f.py --glb swat_upper_gear_remake_f_v1.glb --capture independent_validation/reimport_capture_compact --output compact_reimport_replay.json
python independent_validation/check_metadata_f.py --package . --source-capture independent_validation/source_capture_compact --output metadata_replay.json
python independent_validation/check_correspondence_and_fit_f.py --package . --source-capture independent_validation/source_capture_compact --output correspondence_fit_replay.json
```

Final metadata checks pass the original-to-F and glTF node/skin/vertex index namespaces, all 42,339 retained wearer triangles and original polygon/material groups, exact timing and attachment matrices, and source UVs after the standard V flip. The preview support centroid uses one point per unique F support vertex, averaging seam copies first; its ground origin uses the lower actual exported sole minimum. Physical rifle and visor-surface proxy frames are distinct from anatomical eyes or a camera.

To regenerate the dense references, use capture_f_blender.py with the pinned source movement blend or this GLB and an output prefix. To derive the compact subset, place the two dense captures beside make_compact_captures.py and run it. Neither capture script saves an opened source scene.
