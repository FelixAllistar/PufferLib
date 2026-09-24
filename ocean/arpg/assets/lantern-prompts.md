# Lanternlight art and rendering notes

Mode: built-in image generation, not CLI/API fallback.
Saved asset: [reach-fauna-v5.png](reach-fauna-v5.png), 1774×887 RGBA.
Style reference: [reach-keepers-v4.png](reach-keepers-v4.png).
The original generated file is copied without pixel modifications. The runtime
importer detects gutters and shares row scale/foot pivots across all eight poses.
The actual output's hare is tawny/cream rather than uniformly cream; the deer
action strip is a crouch/leap sequence rather than grazing.

## Exact prompt

```text
Create an original production-ready transparent RGBA sprite sheet for an isometric woodland fantasy game. Image 1 is ONLY style reference: match its richly painted compact readable game sprites, three-quarter view facing right, not its characters. New sheet exactly 8 equal columns by 4 equal rows, 32 isolated full-body sprites, generous 15 percent transparent gutters, no text/grid/background/shadows. Row 1: a cream woodland hare with long ears. Row 2: a small tawny antlered deer. Row 3: hostile squat violet mushroom-backed toad, luminous amber eyes, crimson fungal accents. Row 4: hostile heavy slate-armored boar with amber cracks and red tusks. In each row columns 1 idle, 2-5 four clearly different sequential walking poses, 6 anticipation crouch, 7 forward lunge (hare/deer grazing), 8 recovery. Lock size, foot baseline, cell center, identity across each row. No equipment on animals. All feet and ears entirely inside their cells, no adjacent-cell overlap. Real alpha transparency. Landscape 2:1 canvas.
```

## Online inspiration, not imported artwork

- [Factorio FFF #42: Shadow troubles](https://www.factorio.com/blog/post/fff-42):
  separate object and shadow passes avoid coupling light direction to sprite
  mirroring. Hearthwild projects its own sprites into ground silhouettes.
- [Factorio FFF #324: Animated trees](https://www.factorio.com/blog/post/fff-324):
  small environmental motion makes a static landscape feel less lifeless.
  Hearthwild uses a much simpler trunk-anchored canopy shear, not Factorio's
  normal-map-driven leaf shader or assets.

The light layer is original raylib rendering code, not copied shader code. It
contains colored radial falloff over ambient presets, composed below the HUD.
Projected sun/moon silhouettes and local lights are independent; this is not
physically based lighting, shadow-mapped point lights, or a new visibility system.
