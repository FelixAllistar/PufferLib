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
and native graphics/completion checks. The cabinet remains a received source
candidate. Author rebuild
scripts are preserved inside the archives as provenance and were not executed.
