# Hearthwild sprite atlas

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
