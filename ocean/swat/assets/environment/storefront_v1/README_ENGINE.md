# Morrow Block runtime import

The 39 original module GLBs retain their source hashes. `site.glb` contains the
eight original sidewalk, apron and street meshes extracted from the assembled
example, with only the data those meshes reference. The geometry is unchanged.
The author README describes the complete editable Blender package; this runtime
directory contains only module/site GLBs, licences and source manifests.

`tools/import_storefront.py` reproduces the import from the full batch-16 package.
`engine_import.json` records the source archive, example and extracted site hashes.
Source manifest paths describe the original package; the runtime flattens the
module filenames into this directory. Original material/normal maps are embedded.

Five leaves are reset from the example's open display angles to their closed
hinge positions. The engine supplies doors, static triangle collision, room
volumes, occupants and staging/extraction. This is a playable engine adaptation;
source Blender physics/render-only metadata remains unchanged.
