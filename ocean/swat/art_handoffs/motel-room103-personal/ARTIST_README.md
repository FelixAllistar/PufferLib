# Room 103 personal props: reuse packet v1

**Provisional art/source placement only.** Reuses the existing `env:02_household_clutter/creased_leather_wallet` and `env:02_household_clutter/brass_eyeglasses` designs. No new catalog design, no changes to original GLBs, no new materials, and no engine integration or external upload.

## Proposed placement

Target: occupied Room 103 only, roomX = 2.0, proposed desk support owner **72 = 24 + 48**. Room 101 proposal was shifted +8 m on X. Reference engine thread: 1791474528.653899. Desktop height stays **Y = 0.7603 m**.

Metres, glTF Y-up, unit scale. Positive yaw follows a right-handed rotation around +Y. Both originals have a single identity-transform mesh/root at bottom-center; do not recenter, ground again, or introduce a pivot offset.

- Wallet bottom/root: **(0.8000, 0.7606, -2.0300)**, yaw **+8°**. World measured vertex AABB: **(0.73750031, 0.76059997, -2.08574080)** to **(0.86155254, 0.79687995, -1.97733116)**.
- Glasses bottom/root: **(0.8000, 0.7606, -2.2300)**, yaw **−12°**. World measured vertex AABB: **(0.73301929, 0.76059997, -2.30107737)** to **(0.86510378, 0.77331138, -2.16208458)**.
- Both have a deliberate **0.3 mm** numerical lift above the supplied desktop height. This is not a measured engine support adjustment. Engine may resolve exact visual contact.
- Mesh counts: wallet 1,428 triangles / 5 original materials; glasses 1,400 triangles / 3 original materials. Total 2,828 triangles. Preserve the two independently placeable models; do not merge them.

## Clearance and integration boundaries

The measured pair has **76.344 mm Z-axis AABB separation**. The reference tray original at (0.51, 0.7603, -2.25), unit scale, zero yaw has right bound X = 0.7002533. Thus the glasses have **32.766 mm X-axis AABB separation** and the wallet **37.247 mm X-axis AABB separation** from this unrotated tray. These are conservative vertex-AABB separations, not native engine collision or final clearance approval. Changed tray yaw, scale, root, or offsets invalidate them.

The previews reimport the unchanged originals. Tray/bucket are reference context only; the bucket's preview-only Y=0.7693 is illustrative nesting and is not a prescribed engine placement. A plain default-material preview support represents the supplied Y=0.7603 plane. Neither preview context nor lighting is a runtime deliverable. The previews are Blender renders, not native engine screenshots.

Other supplied attachment centers: folder (0.33, -1.88), clock (0.58, -1.94), TV (0.49, -2.68) in X/Z. The current native folder handoff README specifies folder (.33,.7606,-1.88), yaw 90°, in Room103; its inspected dressing-desk.png shows a folder on the desktop with no visible intersection, though the clock occludes some lettering in projection. This screenshot does not include the proposed wallet/glasses and does not establish numeric clearances. Other attachments are omitted from this preview and clearance claim. Actual desk footprint, TV, clock, corrected folder, tray orientation, and support-destruction behavior still require native verification with the new props.

Proposed support ownership only: bind both render-only pieces to desk 72 and remove with that support. No new collider, loot, navigation, gameplay, interaction, damage, or AI behavior is authorized or supplied. Engine owns final placement, tray clearance, collisions, support attachment, and integration. Do not infer acceptance from this packet.

## Contents and provenance

- `originals/`: the two original GLBs extracted byte-for-byte from the existing private Drive archive.
- `source/`: unchanged original editable batch .blend, procedural build script, complete original texture directory, batch manifest, README and CC0 license. The original source remains a batch source, not a newly authored duplicate. Run neither build nor export to replace the supplied originals for reuse.
- `qa/MEASURED_PLACEMENTS.json`: measurements from fresh GLB reimport and actual transformed mesh vertices.
- `qa/ORIGINAL_BINARY_QA.json`: independent binary structure, hashes and embedded-resource checks.
- `qa/catalog_r008_existing_records.json`: existing catalog identity/provenance and engine-export mappings, copied without editing the catalog.
- `qa/REFERENCE_TRAY_BOUNDS.json`: reference-only tray/bucket reimport measurements.
- `previews/`: fresh detail, top-down tray-clearance and oblique tray-clearance renders.
- `measure_preview.py`: reproducible Blender reimport/measurement/preview setup; no GLB export or engine write.

Original archive: https://drive.google.com/file/d/1wOBJMhV_aYWFGa8sqGbOEfLtvFMo1j5X/view
Original source: `02_household_clutter/household_clutter_02.blend`.
License: CC0-1.0, original procedural artwork, exact batch LICENSE.txt included.

Original GLB hashes:
- Wallet: `adb0116489a06e5892f10a9178695282645cd7c16cafa4cbcd75232dd2b1b546`
- Glasses: `67e9bdd4fa495cbe13c01d36422f17e424d7d7a57c8df1304ce022e9f0e57068`

Existing runtime derivatives have different byte hashes due to prior texture processing, with identity geometry transform recorded by the catalog. This packet retains the requested unchanged source GLBs. Prefer existing runtime asset identities when integrating; do not add duplicate registrations. No runtime derivative is claimed to be byte-identical to the originals.

## Reproducing review images
Run `blender -b -t 4 --python measure_preview.py` from the unpacked packet. For self-contained original-only reimport and measurements, append `-- --original-only`; no other asset download is needed. The full contextual preview script expects the already-existing sibling `motel_guestroom_accessories_v1/exports/` tray and bucket package; those context assets are deliberately not duplicated in this reuse packet. If they are unavailable, use the included original-only image and measurements; do not substitute unverified geometry. The detail render was inspected at 48 samples; contextual renders use 16 samples. The rebuild script uses 16 samples consistently, so exact preview pixel identity is not asserted. No original GLB/source changes are made by this script.
