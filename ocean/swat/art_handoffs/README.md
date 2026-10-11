# Original art handoffs

These are the saved character, animation, weapon, environment and audio deliveries
from the former SWAT worktree, including editable Blender scenes, reference
measurements, source textures, review captures and original delivery packages.
The player uses the approved exports in [../assets](../assets), so normal builds
do not need to unpack these archives or run authoring tools.

The large original firearm audio archive is stored as four ordered 48 MiB-or-less
parts to keep each tracked file below ordinary Git hosting size limits. Restore
it from this directory when needed:

```sh
cat audio_sources/free-firearm.7z.*.part > free-firearm.7z
```

The assembled archive's SHA-256 is recorded in `audio_sources/free-firearm.sha256`.
Other packages retain their supplied manifests, hashes and license records.

The 2026-10-11 maintenance deliveries are retained as complete source archives:
[tied refuse bag](motel-maintenance-bag/source.zip) and
[open power box](motel-power-box/source.zip). Their adjacent `receipt.json` files
record archive hashes, byte sizes and independent GLB/header, triangle and 1K
embedded-image checks. All supplied packet checksums passed. Actual export
previews were reviewed; the bag's source gloss and cabinet's supplied static
open pose remain intact. The bag is installed beside the rear service trolley
with source geometry, matching collision/fallback, damage/support replication
and native graphics/completion checks. The cabinet is installed above the rear
service-room washers in its original static open pose, with all 21,272 source
triangles, matching native art/fallback coverage, metal-cover game damage,
four geometric rear-rim wall references and support/charge removal replication.
These references are modeled geometry, not certified mounting hardware; source
mixed substrate and gauge remain unknown. Both platforms' four completion,
replay/save and replica routes pass. Author rebuild
scripts are preserved inside the archives as provenance and were not executed.

The artist has also completed the [concrete barrier comparison package](https://drive.google.com/file/d/109cuF_xoMuPFYmEkPbhN7ieRJHuvzE5a/view):
the preserved 60,928-triangle CC0 source and a 9,997-triangle trial at original
1.545 x 0.639 x 0.831 m dimensions. This is an available external handoff,
not yet downloaded, checked locally or installed. The reduced lifting loops
and chipped edges require native placement review before accepting it as LOD.

The artist's latest delivery is a [tape-measure package](https://drive.google.com/file/d/1jcz6vcM6kbD1CZT5t0m9guLtWWLn1qZO/view),
reported as 2,228 triangles, two meshes, one material, three 1K maps and original
8.31 x 4.15 x 8.14 cm dimensions, with source/creator CC0 evidence. Its original
label and the artist's unverified brand-clearance note remain part of that
handoff. It is an available static prop, not yet locally checked or installed.
[Catalog UPDATE06](https://drive.google.com/file/d/1nHq8hZUuOfqhn6pn-skFSI0i2lUsTkGk/view)
records the bag/cabinet as engine-reported installed and the barrier candidates
as awaiting native review. Hose/tap, barrier and tape-measure integration are
engine work; the original exports remain frozen during review.
