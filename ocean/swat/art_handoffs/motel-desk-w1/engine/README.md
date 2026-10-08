# Room 101 desk W1: engine integration

The separate oak tabletop candidate is enabled for world owner 24 / source
assembly desk 023 only, under the current Room 101 v4 art. Other desk instances
retain their original render models. Missing W1 falls back to the original desk;
the existing older Room101 comparison modes also retain the original.

The full local render mesh has the same 580 triangles and bounds. Placement is
applied once from the original instance. Collision, damage/removal ownership,
desk-supported bucket/tray and NPC routes are unchanged.

The tabletop's UVs encode a 1.8 m repeat along its long axis. Runtime loading
preserves the supplied 0.3 normal strength, zero metalness, roughness map and
base-colour factors. Surface textures use UV0. No AO atlas is supplied for this
candidate. The original GLB is preserved separately as the fallback.

## Native visual review

`desk-w1-detail-before.png` / `desk-w1-detail-after.png` use the same camera and
game lighting, with only the candidate desk toggled. The room pair shows its
relation to the surrounding bed and furnishings. The oak removes the dense
periodic stripes and reads more clearly as wood. Its lighter colour contrasts
with the original dark base; the old long black scratch geometry is now more
conspicuous. The artist has been asked for a separate W2 finish/wear refinement.

`holder-side.png` and `holder-seated.png` address the art review's roll-holder
placement question. The side view shows clear separation from the toilet tank
and both wall mounts. The mount has been retained; the earlier crowding was
partly caused by the viewing angle.

## Validation and source

- Verified every supplied payload SHA256: 22 runtime and 27 source files.
- Native graphics regression checks original local bounds, 580 triangles,
  material/normal binding, parent removal, immutable world state, resource
  cleanup, missing/corrupt asset behavior and the existing environment suite.
- Rebuilt and captured the normal Windows player through `./swat`.
- Kept the original delivered runtime GLB and compact editable Blender library
  with packed images in the repository. No Python helpers were executed.

The sibling `source` directory preserves the author handoff, material scope,
export validation, source dependency and editable desk library. Complete helper
scripts and Blender preview images remain in the verified Drive packages:

- [Runtime/review delivery](https://drive.google.com/file/d/1pO9p81OlvOAh8-4j5GnUxmF67ifvY8ke/view)
- [Editable source/rebuild delivery](https://drive.google.com/file/d/1ZOIqeCqkt1-H4JvD_vYlci2Ht2x7uuH7/view)

The source scan is Poly Haven's Oak Veneer 01 (Jenelle van Heerden, CC0). Its
original maps and provenance remain in
`art_handoffs/motel-dressing-v1/source/polyhaven_oak_veneer_01`, without another
loose-map copy here. Temporary archives and extracted duplicates are removed
after the engine proof is shared.
