# Standing F import and skinning fixture

This is a private, original-timing integration fixture, not an approved animation
bank or a gameplay-ready reload. Start with `standing_f_original_timing.glb` and
the measured `bindings.json`. The engine owner implements the character adapter.

## What is delivered

- One clip: `Standing Empty / Coordinated Contact Cleanup F`, 0–6 seconds,
  authored at 24 FPS, non-looping. Sample time 0 for the initial Ready diagnostic;
  no separate idle action is included
- 70 joints, 76 nodes, five skinned meshes/six primitives. The body has up to
  seven nonzero influences and uses both JOINTS/WEIGHTS sets. Both magazines,
  rifle and reconstructed carrier sleeve are included
- All 210 animation channels are LINEAR. Two physical magazine meshes remain
  visible throughout; there are no STEP visibility copies in this fixture
- Y-up meters, identity rig-root transform. The source Blender conversion is
  `(x,y,z) -> (x,z,-y)`. Model-to-engine feet/heading and semantic socket alignment
  are explicitly uncalibrated
- Four untextured materials, no images or external buffer/texture dependencies
- Exact skin-joint/node/name/parent mapping and inverse binds in `bindings.json`;
  original source-time contact/ownership markers in `events.json`
- `native_authoring_reference.mp4` is the existing complete native-source review
  video, not playback of this GLB. Its 145 endpoint-inclusive frames run slightly
  longer than six seconds as a video; the action duration is exactly six seconds

The export preserves the immutable F source hash recorded in the manifest. It
does not include the entire editable checkpoint; supply that separately if
re-exporting. No geometry, pose or source timing was authored anew for this fixture.

## Verified export fidelity

`validation.json` compares standard glTF LINEAR/shortest-path quaternion sampling
and Blender reimport against the native F action. It evaluates all rendered
vertices at 1,782 times: a 240 Hz grid, every source/export input key, and samples
immediately around the listed transfers. It also checks native evaluated skin at
18 control times.

- Maximum native-vs-standard-glTF vertex difference: 0.00450 mm
- Maximum native-vs-Blender-reimport vertex difference: 0.00147 mm
- All joint names and parent relationships match; all rendered bind/weight
  vertices and oriented surface triangles are preserved
- All 24,850 body vertices are represented by 31,687 exported vertices after
  material/UV/normal seam splitting. 444 exported body vertices use over four
  influences. The exported weight sums are normalized equivalents of Blender
  weights, not literal copies of raw source sums
- Six loose rifle vertices, unused by any triangle, are omitted by the exporter;
  all 10,273 rifle surface triangles are preserved
- The read-only structural preflight passes for this one-clip fixture and an
  eight-influence limit. A four-influence profile rejects it as expected

These are numerical export checks. They do not establish engine/ozz/GPU playback,
material/animated-normal parity, full glTF conformance, artistic approval or
contact-clear motion. A complete Khronos validator was not run. The two
`reimport_*.png` images are static Blender reimport sanity views, not runtime tests.

## Protocol v7 integration

The checked engine baseline is `811bc1ed9ed0f33767589696907227bd374d45c0`.
The public contract is in [draft PR2](https://github.com/FelixAllistar/PufferLib/pull/2)
at commit `9f3ed2c8673d2fcdb3e3d45973a2ea012ad7c828`; `ENGINE_CONTRACT.md`
is that documentation snapshot. Its pending-export statements describe the time
of that contract revision; this package's manifest and validation record the
subsequent private fixture export.

Use `swat_pose` and achieved simulation/replica state. Do not let art sockets
replace authoritative eye/muzzle/clearance. The inherited `RifleSocket` and
`Muzzle` bones are marked uncalibrated. Hands/shoulders/feet are candidate rig
bindings, not proven IK or controller contacts.

Carbine empty reload commits at ticks 39/104/156 at 60 Hz: REMOVE, INSERT, then
chamber/completion. F remains six seconds; `events.json` deliberately leaves
gameplay mapping pending. A uniform 2.31x speedup is not the agreed adaptation.
Cancellation preserves already committed seated/ammo/chamber state, so IDLE can
be unseated. A restarted reload begins a fresh timer. Optional magazine-array
indices reorder and are not persistent prop or pouch IDs.

Source markers describe contacts in the baked study; they are not authority
events. For example, source unseating starts at 0.70 s, free release at 0.88 s,
fresh pickup at 1.40 s and fresh seating at 3.65 s. Both magazine transforms are
baked under the asset root. The old magazine's preview fall follows actor-root
movement; an independent cosmetic drop needs a captured world transform and
deduplication. No marker grants ammunition or invents a persistent dropped item.
The game currently retains old rounds; the study's visible drop is not that rule.
Do not loop the clip or reset visual inventory when sampling Ready after reload.

## Quality and rights

This is the exact F checkpoint, including its known limitations:
partial stock-pad support/upper overhang, foregrip palm/cuff contact and wrist
pinching, shallow pickup sleeve contact, unmodified insertion/release hand/digit
contacts, rough reconstructed sleeve, and brisk final grip closure.
See `SOURCE_QUALITY_STATUS.json`. Current animation revisions outside this
checkpoint are not included. No artistic or final animation-bank acceptance is
implied by successful export.

Keep the GLB, authoring source, reference video and related asset data private.
Source character/weapon redistribution rights have not been established for
public GitHub; repository code licensing does not license those assets.

## Reproduction

The source file is `standing_empty_revision_f.editable.blend` with SHA-256
`a7a6d9ae7e13f30ac8de4b6347eb47062344768c04228b054388d9af726156b6`.
The scripts write outputs without saving that source. Blender 4.3.2 was used.

```sh
python check_animation_glb.py standing_f_original_timing.glb --expected-animations 1 --max-influences 8
blender -b --python export_fixture.py -- /path/to/standing_empty_revision_f.editable.blend /path/to/output
blender -b --python capture_parity.py -- /path/to/standing_empty_revision_f.editable.blend standing_f_original_timing.glb /path/to/parity_work
python validate_fixture.py standing_f_original_timing.glb /path/to/parity_work validation.json
```

Numerical comparison uses NumPy and SciPy. Runtime import does not require those
validation dependencies. Check every file against `manifest.json` before import.
