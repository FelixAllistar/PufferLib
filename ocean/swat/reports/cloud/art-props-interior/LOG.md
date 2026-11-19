# Interior props v2 — hill-climb log (swat/art-props-interior)

One numbered line per iteration: "prop iter N: change; closer: yes/no".
Blender previews are Cycles CPU (48 spp + denoiser, 960x540) from a camera
matched to the reference photo; in-game iterations use
`build/swat/props_interior_capture` (`make -C ocean/swat props-interior-capture`).
Per-iteration side-by-side sheets live outside git; the committed contact
sheets (reference | first render | final in-game) are `<prop>_sheet.jpg`.
