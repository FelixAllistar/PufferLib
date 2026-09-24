# Hearthwild sprite atlases

## Lanternlight / build 5

[Fauna and enemy frames](reach-fauna-v5.png): 8×4, 1774×887 RGBA, original
hare, deer, spore toad and slate boar designs. Generated with the **built-in image
generation tool**, using `reach-keepers-v4.png` only as a style reference.
Original output and alpha are preserved; older sheets are untouched.
[Full prompt and inspiration notes](lantern-prompts.md).
[Lantern preview](lantern-preview.png) is captured from the running viewer,
not an image-generated scene or UI.

## Reach build 4

Generated with the **built-in image generation tool**, not the CLI fallback:

- [Companion frames](reach-companions-v4.png): Wisp, Fang, Aegis, Porter; 8×4, 1774×887 RGBA.
- [Keeper/specialist/enemy frames](reach-keepers-v4.png): keeper, Burrower, Ember, thornling; 8×4, 1774×887 RGBA. A second built-in edit corrected an Ember flame crossing a cell gutter.
- [Biome decorations](reach-biomes-v4.png): 16 trees, rocks and understory objects; 1254×1254 RGBA.

The [exact generation and correction prompts](reach-prompts.md) include reference
assets. Original base/expansion sheets remain unchanged. `ar_sprite_sheet.h`
imports the new PNGs without image rewriting, finds transparent gutters, and
uses shared row-scale and foot pivots for the eight-frame strips.

[Reach preview](reach-preview.png) is a running-game capture developed through
normal production. [World atlas preview](reach-map-preview.png) is a runtime
visual-test capture with a scout placed in several regions; it is not AI-generated
UI, a walked route, or evidence of fully simulated remote combat.

## Original assets

Saved asset: [hearthwild-atlas.png](hearthwild-atlas.png).

Generated with the built-in image generation tool (not the CLI fallback), then
copied into this directory. The final image is 1254 × 1254 RGBA with transparent
background. The source alpha is preserved. The renderer derives each sprite's
alpha bounds within its 4×4 atlas cell at load time; it does not modify the image.
Characters, structures, and foliage share one illustrated palette and lighting.

## Final generation prompt

```text
Use case: stylized-concept
Asset type: production-ready transparent sprite atlas for an isometric fantasy homestead / creature-command RTS game.
Primary request: create one cohesive, beautiful, polished 2048 x 2048 RGBA sprite atlas containing EXACTLY 16 separate game sprites in an invisible 4-column by 4-row grid of equal 512 x 512 cells. This is an actual texture for a game, not a presentation board.
Style/medium: finely illustrated hand-painted miniature dioramas, clear bold readable silhouettes, matte materials, soft ambient shading, restrained deep forest greens, warm weathered stone and copper, magical teal accents. Sophisticated cozy frontier fantasy, not generic shiny mobile-game art, not pixel art.
Camera: consistent orthographic three-quarter isometric, viewed down at approximately 35 degrees; all sprites face toward lower right. Upper-left warm lighting.
Subjects in strict row-major order:
Row 1: (1) small hooded adventurer in a teal cloak with a lantern staff; (2) pale turquoise leaf-eared wisp creature with little legs; (3) rust-orange foxlike fang pet; (4) squat blue-gray armored turtle guardian.
Row 2: (5) honey-yellow beetle porter pet carrying woven saddlebags; (6) small hostile thorn-backed red forest creature; (7) bulky crimson stone brute; (8) squat stone-and-brass magical defense turret with teal crystal.
Row 3: (9) short timber-and-stone palisade barricade; (10) copper and oak automatic crystal extractor with small wheel and chute; (11) ominous thorny red monster nest; (12) beautiful large dark-green clustered fir tree.
Row 4: (13) weathered mossy boulder; (14) small cluster of luminous turquoise resource crystals in earth; (15) cozy small timber lodge workshop with copper roof and warm window light, the home base; (16) low leafy fern and tiny white flower cluster.
Composition: one and ONLY one sprite centered in each cell, generous transparent space at every cell edge, each entire object including all extremities inside its own cell, zero overlap. Objects rest on the same baseline at 85% of their cell height. No ground tiles or background scene under sprites. Structures and tree use more of their cell than creatures. Precise 4x4 layout.
Constraints: genuinely transparent background with alpha, not white or checkerboard; no text, no grid lines, no labels, no interface, no logos, no watermark. Fine crisp alpha edges. Original designs.
```

## Frontier expansion

Saved asset: [hearthwild-frontier-atlas.png](hearthwild-frontier-atlas.png).
Generated with the built-in image generation tool, not the CLI fallback, using
the original atlas as a style reference. The 1254×1254 RGBA output retains its
alpha. The renderer reads the four cells directly: Burrower, Ember, Starfire,
bridge. No base asset was replaced.

The [frontier preview](frontier-preview.png) is a capture of the running game,
developed through actual simulation and production, not an image-generated UI.

### Final expansion prompt

```text
Use case: stylized-concept. Asset type: one 2x2 transparent sprite atlas for the Hearthwild isometric fantasy game. Reference image is STYLE REFERENCE ONLY: match its richly hand-painted cozy RTS sprites, readable silhouettes, warm wood/brass/stone and teal magic, isometric 3/4 view. Create exactly four isolated objects in equal square cells, generous transparent gutters, no overlap, all fully inside cells: top left a friendly stout tunneling mole monster with enormous digging claws and rugged brass harness; top right a friendly ember salamander with orange glowing throat and tiny portable smelting saddle; bottom left a large fantasy starfire artillery launcher, an upward-angled rune cannon with luminous teal warhead, stone base and brass machinery; bottom right a short wooden and stone bridge segment. Actual alpha transparency across the background, no backdrop, no grid, no captions, no lettering or watermark. Crisp painted shapes visible at small sizes. This is a new expansion atlas, do not reproduce the reference sheet.
```
