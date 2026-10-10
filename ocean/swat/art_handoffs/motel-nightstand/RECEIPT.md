# Room 104 nightstand integration receipt

Original: [Painted Wooden Nightstand](https://polyhaven.com/a/painted_wooden_nightstand), Kirill Sannikov, [Poly Haven CC0 asset license](https://polyhaven.com/license). Primary asset/license pages checked 2026-10-10. [Verified derivative Drive packet](https://drive.google.com/file/d/1cT2woZ9BR-LTw0Cbhwdo7K7XrUzqEbK4/view). The full original four-asset source pack remains [backed up on Drive](https://drive.google.com/file/d/12jZ50cqpWrWQCnmQlAsymlOm1rO5Plyv/view), 50,905,982 bytes, SHA-256 `1115badf9d1620ef50838585217a9f7b9d5e318b7064718c25320c5372af6da8`.

The installed GLB is retained once at `assets/environment/motel_props/painted_wooden_nightstand_floor_centered_2k.glb`: 8,215,320 bytes, SHA-256 `af66187e614c9a11c7d690fcbe35422d45af2890cb3f4644ea631b0d9b3e1d17`. The compact source receipt retains provenance, original source hashes, asset/license references, derivative conversion scripts, QA and two artist-rendered previews without duplicating that GLB or unrelated suitcases. Receipt archive SHA-256: `36317306a74ee2fc4f52ceb67f7c93aeefb5c9f8d0eab72de38968f31db4675a`. Its original supplied checksum list covers the complete upstream derivative packet; excluded upstream files remain on Drive. Python sources are archival and were not executed.

Original geometry, UVs, normals, drawer transform and three embedded 2048px JPEGs remain unchanged. Source glTF is X right, Y up, Z depth. Full transformed size W×D×H: 0.504564583 × 0.508716002 × 0.615520971 m. Root floor pivot is preserved; body/drawer total 503 source vertices and 470 triangles. Scalar roughness 1 and metallic factor 0 are preserved. Source does not bind AO, even though an ARM red channel exists.

Engine placement: room 104 beside the bed, origin `(7.62, .004, -3.15)` m, yaw 0, no rescale. Four measured foot-centres resolve to original carpet triangles on floor owner 81. Owner 1189/asset 49 is appended; original object IDs and the 1189-object pane prefix remain intact. Contact subsets preserve exact original triangles and edge flags. Missing art uses exact collision mesh rather than a solid bounding box. Damage and removal share map/snapshot/replay authority.

WOOD is a coarse gameplay classification with a **24 mm approximate health section**, not a verified construction gauge. Wood species, veneer/composite structure, finish, concealed joinery and drawer-pull substrate remain unknown. The source is not certified watertight; open-mesh ballistic traversal stops conservatively. Exact tabletop exit in the tested ray is 37.836 mm; no blanket shell thickness is claimed. One three-map 2K GPU texture set is shared across model owners (~64 MiB with RGBA8 mipmaps), with explicit final-owner cleanup.

Validation: Linux and native Windows geometry, floor contacts, open leg space, partial damage/removal, encoded map/snapshot replicas, malformed recipe/unsupported state rejection and ordinary canonical-spawn walk/door/aim/fire/reload replay restore. Native original-art/fallback raster matches 153,932 stable collision-ray samples on five sides, preserves native hierarchy bounds and source material maps, and verifies lit shadows, authoritative removal and texture lifetime. Native captures/logs remain under `build/swat/review/nightstand_v1`; the room-scene image was inspected. Existing pane and 27-asset contact checks pass. Front, exterior-charge and inter-room-charge scenario completion checks run on Linux and Windows with exact replay/save/late replica verification.

Reproduce collision data after verifying the installed GLB SHA above (no Python):

```bash
cc -O2 ocean/swat/tools/import_motel_nightstand.c -lm -o /tmp/swat-import-nightstand
(cd ocean/swat && /tmp/swat-import-nightstand > nightstand_data.h)
node ocean/swat/tools/partition_motel_contacts.cjs
make -C ocean/swat motel-nightstand-test motel-contacts-test
```
