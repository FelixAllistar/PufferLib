# Guest information folder integration

[Delivered package](https://drive.google.com/file/d/1QQx24S7xCB2YmYXIjiW2RPA6jKC7SD8x/view).
All delivered SHA256SUMS verified before import. Runtime GLB SHA256:
`6fbfb585c2ae21fe40b5cb064246a8a57cde53dbb8b2cd7b0ad5d9dd85aeb35b`.

The 432-triangle folder uses four primitives, two materials, 512 px material maps
and independent UV1 AO. The bottom contact anchor is placed at Room 101
(-7.67,.7583,-1.88), yaw 90 degrees, on desk owner 24. Other rooms repeat the
placement every four metres in X, with Y .7606 for their original desktop.
The folder is clear of the clock, hospitality tray and television. Its 0.306 m long
edge lies across the desk; its heading faces the occupant side.

It is decorative supported art, not an invisible extra collider or an interaction.
Removing its actual desk owner hides it in the same frame on host and replicas.
The folder adds no world objects and does not alter movement/cover geometry.
Missing art falls back to an empty tabletop. Each room now has nine attachments.

The repository retains the editable blend, maps, license, authored binding and
QA metadata under `source/`; runtime GLB lives in the environment asset directory.
Delivered helper scripts, duplicate exports and contact-sheet previews are omitted.
Native `dressing-folder.png` records the integrated desk placement.
