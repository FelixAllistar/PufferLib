# Original guest desk cleanup

Source: [verified artist delivery](https://drive.google.com/file/d/1FmLc4aOz9nDKDShmKCae6TmipBeH4tYX/view).

The three guest desks bind source indices 47/71/95 to engine owners 48/72/96.
Only their render model changes. Room101 owner 24 retains W2; service owner 135
retains the original bank. Wallet, glasses, collision and support do not move.
The cleanup model is shared by all three instances and unloads on location
switch; missing art falls back to the original desk.

The package checksums pass. The delivered original matches the checked-in
`motel_v1/desk.glb` SHA256. An independent binary GLB comparison verified every
retained triangle's position, normal, UV and material assignment, and every
embedded image byte. Seven raised HP_wood_dark boxes account for all 84 removed
triangles (580 to 496). The render top is 2.3 mm lower; physical bounds stay
unchanged. See `preservation.json` for the runtime hash and measurements.

The repo retains the runtime, compact editable blend, original package checksum
manifest and provenance. Duplicate source GLBs, previews and helper scripts
remain available in the full Drive delivery. No Python was executed.

Native graphics checks cover owner removal/fallback, unchanged authority,
resource teardown and matched Room103 before/after views. The personal props
remain in both captures, so the removed dark tabletop strokes can be evaluated
independently of their appearance.
