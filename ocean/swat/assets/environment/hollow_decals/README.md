# The Hollow surface decals (presentation only)

`tools/blender/hollow_textures.py` generates this set procedurally from
fixed seeds:

```sh
python3 ocean/swat/tools/blender/hollow_textures.py TEX_DIR ocean/swat/assets/environment/hollow_decals
```

| File | Content |
| --- | --- |
| `decals_color.png` | 1024x768 RGBA atlas on a 4x3 grid of 256 px cells. Tiles: `stain_pool`, `stain_smear`, `drips`, `boots`, `mud_splash`, `grime_base`, `drag` (two cells tall), `newsprint_a`, `newsprint_b`. |
| `decals_nr.png` | Flat normal map; alpha is roughness, with a faint sheen on the stains. |
| `atlas.txt` | Generated manifest. |
| `placement.txt` | Hero decals. The upper part is hand-written: drag marks, stains, earth floors and smokehouse soot. The section below its marker is generated: newspaper pasted on the house's inside walls. The grammar is in `ocean/swat/surface_decals.h`. |

- Stains and marks are simple soft shapes, implied and dried, never graphic.
- No photographs are used.
- Newsprint is grey bars, not readable text.
- The decals snap to the authoritative boxes and break with them.

## License

This is original procedural work by the SWAT project, dedicated to the public
domain under CC0 1.0. It uses no third-party textures or fonts beyond the
system DejaVu font, which renders headline shapes in the flyer textures only.
