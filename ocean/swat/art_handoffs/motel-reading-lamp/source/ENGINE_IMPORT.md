# Import and ownership

## Geometry and coordinates
Load one LOD at a time; both describe the same original fixture. Metres, identity root, rear mount center at origin. Blender Z-up/-Y outward converts to glTF Y-up/+Z outward. Preserve named children and parent ownership. LOD1 keeps the button and shade cavity, with some edge and metallic-highlight simplification; choose switching distances after game-camera testing, not an asserted universal distance.

ANCHOR_bulb_light is a non-rendering empty inside the shade. Its local -Z points through the opening. This is only a suggested placement/orientation for future engine work; there is no bulb mesh or actual light in either export. No electrical safety or real product engineering is implied.

## Material contract
Basecolor sRGB. Roughmetal linear R=255, G=roughness, B=metalness. Normal is OpenGL +Y tangent space. AO is a separate UV1 atlas, not the R channel of the UV0 roughmetal image. Keep both UV sets and tangents. Three opaque, single-sided material slots across eight named mesh primitives; triangle budget is not a draw-call or performance certification. Engine may combine static metal parts if ownership/anchor/button semantics are preserved. Shade has geometric thickness and a real interior cavity. Surfaces are mostly clean with restrained finish variation; shared analytic marks are not unique object-space wear.

## Mount and engine ownership
Fixed-wall owner only. The mount vignette is a proportion/ownership proposal beside a headboard outside the print, not an approved coordinate or change to Room101. Engine integration owns lighting and shadow cost, switch interaction, bulb emission, collision, parent attachment and removal on wall destruction. No rigid bodies, animation, destruction, wiring, engine scripts, room changes, additional furniture or pinned asset edits are included. Art QA passing does not establish in-engine acceptance.

## Rebuild (Blender 4.3.2; Python numpy + Pillow)
Run from this package folder:

    python make_textures.py
    blender -b -t 4 --python build_prop.py
    python validate_glb.py
    python audit_uv1.py
    blender -b -t 4 --python source_reopen_qa.py
    blender -b -t 4 --python render_export.py
    python write_docs.py

Independent build: blender -b -t 4 --python build_prop.py -- --out /tmp/lamp_rebuild
Then python check_rebuild.py . /tmp/lamp_rebuild
Generate textures independently with python make_textures.py /tmp/lamp_texture_rebuild and compare hashes. Runtime GLBs and generated/baked maps are tested byte-exact; .blend container timestamps and preview renders are excluded from byte equality.

Source uses external relative texture paths; keep textures beside the .blend. Source contains no camera or preview lighting. Rendering imports runtime GLBs afresh and adds disposable preview helpers only. Package script excludes backups, logs, caches and rebuilt temp folders.
