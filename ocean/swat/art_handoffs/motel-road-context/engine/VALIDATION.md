# Exterior validation — 2026-10-09

Linux and native Windows ground tests pass: 36 measured plant roots land on their
real support, disappearing when support is absent. W_01 explicitly requires
both 1119 and 1120. The existing 740 support/seam rays, finite edges, physical
player and squad crossings, wide navigation, modified-map rejection, replica
reconstruction and reset/close still pass.

Native Windows rendering verifies 40 opaque road-paint triangles, the original
840/2294-triangle grass/seedhead models, and six matched road/plant cameras.
Full and culled images are pixel-identical, including lighting and all shadow
passes. Rendering leaves the simulation world byte-identical. Ground rendering
passes 54 physical-owner bounds, PBR channels, 56,250 ray/raster samples,
independent removal and exact collision fallback.

The new paint and both grass models each pass three-owner texture sharing,
idempotent registration, exact retained pixels after two owners close, removed
map references and final storage release. The full original-versus-shared motel
comparison was interrupted under GPU memory pressure; the targeted new-model
checks and prior completed pool regressions provide this increment's validation.
No new shader or texture-pool behavior is introduced here.

Runtime/source archive payload hashes were independently verified. Three collision
GLBs and placements remain byte-identical. Connected-ground BIN bytes and all
54 mesh names/order/transforms/bindings are unchanged. Both collision importers
regenerate the existing headers unchanged, including all 1,040 rock triangles.
The material factors are 0.65 for gravel and 0.78 for stone; original scan images
are preserved. Revised shrub geometry remains visual, with unchanged cover.

Native review was shared on Slack as F0C80EFCE7P. These are engine captures, not
artist offline renders. Finite scenery edges, sparse plants and material repetition
remain unfinished. No new physical props, cratering, sound assets or tactical
policies are claimed by this increment. Protocol/replay remains version 16.
