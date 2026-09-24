# Reach v4 art — final prompts

Generated using the built-in image generation tool (not the CLI fallback).
Original source atlases were references; they were not overwritten. Generated
RGBA assets and their alpha channels are retained in this directory.

## companions

Saved asset: [reach-companions-v4.png](reach-companions-v4.png).

Reference images: hearthwild-atlas.png.

```text
Use case: stylized-concept.
Asset type: production animation sprite sheet for the existing Hearthwild isometric companion RTS.
Input images are CHARACTER AND PALETTE REFERENCES ONLY. Make a NEW sheet; do not reproduce the reference layout.
Exactly EIGHT columns and FOUR rows in a regular invisible grid (32 equal cells), wide 2:1 canvas. One complete isolated sprite per cell, generous transparent gutters, no overlaps.
Each ROW depicts ONE identical character across EIGHT animation poses. Camera stays fixed at the same elevated three-quarter view, all facing screen lower-right. Character size, equipment, body proportions, ground baseline (82% of each cell), and lighting must stay consistent across the entire row. Never rotate the character between columns.
Columns in each row: 1 relaxed idle; 2 walk left/front-leg contact; 3 walk passing pose; 4 walk opposite-leg contact; 5 walk opposite passing pose; 6 attack/work wind-up; 7 attack/work extended impact; 8 recovery. Explicitly change limb articulation and body pose—do not duplicate one image or simply move it.
Style: polished hand-painted fantasy RTS sprites, clear chunky readable silhouettes and economical broad forms, matte materials, warm brass/wood and restrained teal accents from the references. LESS tiny surface noise and less glossy contrast than the references. Must read well at 50–65 pixels tall.
Constraints: genuine alpha transparency, no ground tiles, no opaque backdrop, no checkerboard, no labels, no lettering, no grid lines, no watermarks. Keep the entire sprite and extremities within its own cell. This is a usable game texture, not a concept-art presentation.
Rows top to bottom: 1 pale teal leaf-eared Wisp quadruped; 2 rust-orange fox Fang with simple leather harness; 3 squat blue-gray Aegis turtle with brass armored shell and teal rune; 4 gold beetle Porter with woven saddlebags. Preserve those four identifiable designs from reference image 1.
```

## keepers

Saved asset: [reach-keepers-v4.png](reach-keepers-v4.png).

Reference images: hearthwild-atlas.png, hearthwild-frontier-atlas.png.

```text
Use case: stylized-concept.
Asset type: production animation sprite sheet for the existing Hearthwild isometric companion RTS.
Input images are CHARACTER AND PALETTE REFERENCES ONLY. Make a NEW sheet; do not reproduce the reference layout.
Exactly EIGHT columns and FOUR rows in a regular invisible grid (32 equal cells), wide 2:1 canvas. One complete isolated sprite per cell, generous transparent gutters, no overlaps.
Each ROW depicts ONE identical character across EIGHT animation poses. Camera stays fixed at the same elevated three-quarter view, all facing screen lower-right. Character size, equipment, body proportions, ground baseline (82% of each cell), and lighting must stay consistent across the entire row. Never rotate the character between columns.
Columns in each row: 1 relaxed idle; 2 walk left/front-leg contact; 3 walk passing pose; 4 walk opposite-leg contact; 5 walk opposite passing pose; 6 attack/work wind-up; 7 attack/work extended impact; 8 recovery. Explicitly change limb articulation and body pose—do not duplicate one image or simply move it.
Style: polished hand-painted fantasy RTS sprites, clear chunky readable silhouettes and economical broad forms, matte materials, warm brass/wood and restrained teal accents from the references. LESS tiny surface noise and less glossy contrast than the references. Must read well at 50–65 pixels tall.
Constraints: genuine alpha transparency, no ground tiles, no opaque backdrop, no checkerboard, no labels, no lettering, no grid lines, no watermarks. Keep the entire sprite and extremities within its own cell. This is a usable game texture, not a concept-art presentation.
Rows top to bottom: 1 teal-hooded Keeper carrying a warm lantern staff, from reference image 1; 2 friendly brown Burrower mole with large digging claws and brass harness, from reference image 2; 3 friendly orange Ember salamander with compact refinery saddle, from reference image 2; 4 low dark-red thorn-backed enemy quadruped, from reference image 1. Preserve those identifiable designs. Burrower attack poses dig with its claws; Ember attack poses breathe a small orange puff kept inside the cell.
```

## biomes

Saved asset: [reach-biomes-v4.png](reach-biomes-v4.png).

Reference images: hearthwild-atlas.png.

```text
Use case: stylized-concept. Asset type: production environment sprite atlas for Hearthwild, an isometric fantasy companion RTS.
Reference is PALETTE/CAMERA reference only. Create a NEW square 4-column by 4-row regular invisible grid, exactly 16 separate objects, one object centered in each equal cell with generous genuinely transparent gutters and no overlap. Fixed elevated three-quarter isometric view; upper-left soft daylight.
Style: restrained hand-painted game art, broad soft color shapes with clean readable silhouettes and matte surfaces. LESS busy detail than the reference. Natural muted greens, ochre, slate and warm stone. Objects should look good repeated sparsely in a landscape; do not make large noisy clusters or dioramas.
Objects in row-major order:
Row 1: one airy broadleaf green oak tree; one slender birch with ochre-gold leaves and white trunk; one single tall slim dark-green spruce; one low dry-country acacia with sparse sage-green canopy.
Row 2: one broad mossy slate boulder; one pale granite outcrop with flat planes; one warm ochre sandstone outcrop; one small weathered standing stone with a subtle teal rune.
Row 3: one low reed tuft; one low straw-colored dry-grass tuft; one small forest fern; one low patch of tiny pale flowers and clover.
Row 4: one old tree stump; one short fallen mossy log; one low thorn bush; one piece of pale shoreline driftwood.
Trees use the upper two thirds of their cell and rest at 85% height, other objects proportionally smaller. No ground tiles or scene under objects. No UI, text, borders, grid lines, logos, watermarks, baked rectangular shadows, or opaque background. Preserve actual alpha transparency.
```
## Keeper sheet correction (final keeper asset)

Built-in image generation edit, using `reach-keepers-v4.png` as its reference.
The corrected result replaces only the new keeper sheet created in this pass;
the original generated candidate remains in the image generation output folder.

```text
Edit this exact transparent 8-column by 4-row game animation sheet. Preserve the four character identities, row order, hand-painted style, facing direction, relative body sizes, and the 8 animation poses per row. Row 1 keeper, row 2 armored burrowing mole, row 3 ember salamander, row 4 thorn monster. Correct the production defects: there must be generous completely transparent gutters between ALL 32 cells, with no fire, glow, dirt, claw, tail or staff crossing a cell boundary or appearing beside the next character. In row 3, replace the long sideways flame of column 6 with a compact mouth glow fully attached to the salamander inside its own cell; column 7 must contain only its own salamander and no detached fire at its left edge. No large flames on this sheet; gameplay adds them separately. Scale every character uniformly a little smaller within its cell to ensure at least 15% transparent horizontal margins. Equal 8x4 cell layout and consistent shared foot baseline within each row. Walk poses must retain alternating limb positions and attacks must still change body poses. Actual transparent RGBA, no background, no grid, no text, no captions, no watermark. This is a corrected production animation texture, not a contact sheet presentation.
```
