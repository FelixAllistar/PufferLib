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
