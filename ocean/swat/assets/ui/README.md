# Gold Element UI asset experiment

Generated with the built-in `image_gen` tool on 2026-10-04. These are raster
assets used by the actual player; labels, button behavior, live camera video,
target selection and progress remain code-rendered.

| Asset | Use |
| --- | --- |
| `panel.png` | Opaque 1536×1024 dark panel skin; nine slices use 96-pixel source corners and 12-pixel output borders. Camera inset, floating scope and interaction cards share it. |
| `icons.png` | Transparent 1254×1254 sheet; top-left camera, top-right cuffs, bottom-left picks, bottom-right compliance. Each equal quadrant is sampled independently. Alpha was checked to span 0–255. |

The originals were inspected and copied into this directory without editing.
The renderer scales/slices them at draw time. Make, CMake and the native Windows
builder copy them into `assets/ui` beside the player; source-root play can load
this directory directly. Missing images retain a plain-panel fallback.

## Final panel prompt

Use case: ui-mockup. Asset type: production raster skin for an in-game tactical camera and interaction panel, to be used as a stretchable nine-slice background in a native first-person SWAT game called Gold Element. Create one isolated rectangular interface panel, landscape aspect around 3:2, filling the image with no surrounding mockup or screenshot. Empty central interior occupies at least 85 percent of panel: very dark charcoal blue (#101b23), matte subtle tactile polymer texture, quiet and even so real-time code-rendered text and video will be clearly readable. Border: restrained slim brushed metal bevel, one fine muted warm gold (#c9a155) inner edge, softly chamfered corners, minute believable wear on outer border only. Contemporary professional equipment aesthetic, clean and high contrast, no neon, no military ornament. Keep the outer 32 pixels appropriate as a nine-slice border; all decorative detail near the edge. No text, letters, numbers, labels, buttons, icons, logos, crosshairs, guns, people, fake screen content, or large highlights. Straight-on flat orthographic raster UI asset, no perspective or drop shadow.

## Final icon prompt

Use case: ui-mockup. Asset type: production transparent PNG icon atlas for the Gold Element SWAT game. Create exactly four distinct readable interface icons in an evenly spaced 2 by 2 square grid, one centered icon inside each equal quadrant. Keep each icon isolated with large transparent gutters and do not let it cross its quadrant. Top left: a compact tactical video camera with one lens. Top right: a recognizable pair of linked handcuffs, both rings visible. Bottom left: two simple lockpick tools beside a small padlock, clearly a quiet lock-opening tool. Bottom right: a simple profile head with three short speech lines for a verbal compliance command. Consistent professionally drawn game HUD illustration style, slightly dimensional matte brushed metal, warm ivory and muted gold silhouettes, clear dark edge, restrained highlights. Icons must be legible at 32 to 48 pixels, simple uncluttered forms, not photoreal props. No text, lettering, numbers, labels, decorative frames, grid lines, drop shadows, full-screen backgrounds or watermarks. Background must be genuinely transparent. Exactly one icon per quadrant; identical visual scale.
