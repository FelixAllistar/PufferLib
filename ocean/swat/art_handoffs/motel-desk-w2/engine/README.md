# W2 desk and reading-lamp integration

W2 replaces Room101's render owner 24 only. It uses the delivered 544-triangle
mesh, six material primitives, 1.8 m oak veneer maps and 0.3 normal strength.
The former raised black scratches are removed; sparse wear changes roughness
without displacement/overlapping surfaces. The render top is 2.3 mm lower;
original compiled collision, supported bucket/tray placement and authority remain.
The old W1 export is retained as fallback and an explicit comparison:

    SWAT_MOTEL_DESK=w1 ./swat play --mission motel

`desk-w2-detail-before.png`/`after.png` compare the original bank against W2 under
the same native lighting. `desk-w2-room-after.png` shows room context.

## Reading lamps

The accepted 952-triangle LOD0 is mounted beside each guest-room headboard.
Room101 mount is (-4.65,1.25,-3.939), Y-up/metres, with +Z pointing into the room.
The seven earlier decorative attachments remain; each room now has eight.
Original UV0 material maps and independent UV1 AO are retained. It adds no hidden
collision or new physics objects. Loss of the actual supporting board section
removes the lamp, including on replicas.

The bulb anchor is local (0,.08959322,.12970687). It replaces the room's formerly
unrepresented ceiling point light in the existing six-face shadow slot, with a
soft-edged downward beam. This introduces no extra shadow atlas or light count.
The shader explicitly disables its radiance if the wall support is gone, rather
than falling back to an invisible ceiling lamp. The button remains static; no
switch interaction is claimed. The artist corrected the exported anchor rotation; only that JSON field changed,
with binary mesh/material/image data verified identical. Direction is
(0,-.939692624,+.342020136), matching the measured shade opening. The fixture
casts sun/contact/other-room shadows; its own near-field shade occlusion is
represented by the analytic beam to avoid oversized point-shadow artifacts.

## Sources and validation

- [W2 runtime](https://drive.google.com/file/d/16NmeWlztsTJl_PUP0koR92MmPeC3ARRE/view)
- [W2 source](https://drive.google.com/file/d/1CjKCdzxIovI-Uc1R_WWYCZDuC3u6lvbA/view)
- [Reading lamp](https://drive.google.com/file/d/1Mj_qj9xQ6p8gUoKT0ZiQJGaCPCZHAJwh/view)
- Orientation correction: Slack file `F0C7RQX2ANN`, SHA256
  `2da297f5f7b7c207ec16cc4edae183ecbc094e48548183647a95f8a37929352b`.

All delivered payload hashes were verified before copying. The repo retains
runtime GLBs, compact editable blends, needed texture sources and provenance.
Source QA/helper scripts and duplicate previews remain in the complete Drive
packages; no Python was executed.

Headless motel checks exercise all 32 support removals, lamp anchors/removal,
existing routes, penetration/breaching and network reconstruction. Native
render checks cover W2 geometry/materials, lamp UV1 AO, changed illumination
when support disappears, optional edge-strip rendering and missing-art/resource
lifecycle. The normal native Windows player is rebuilt from the same sources.
