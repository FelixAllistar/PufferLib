# Look hill-climb: trees, mountains, biomes (2026-11-19)

Twelve tracks in three rounds, each climbed by a cloud worker from
`BRIEF.md` (render, look, fix the worst fault, keep a change only if it is
clearly better) and reviewed, merged and rendered on a GPU between rounds.
Each track's folder holds its pass log (`LOG.md`) and before/after sheets.

| Round | Track | Passes kept | What it gave |
|---|---|---|---|
| 1 | trunks | 11 of 14 | Bark by species from 2K scans, root flares, flutes, stubs |
| 1 | conifers | 11 of 15 | Dense cones at real size, young, mature and old forms |
| 1 | broadleaf | 14 of 21 | Real sizes and full crowns; cycads and tree ferns |
| 1 | mountains | 11 of 15 | Lowered grass line, scree fans, forest up the gullies, a near-desert |
| 2 | near | 19 of 23 | Trunks in the forest's tone, fuller cycads and shrubs, light through leaves |
| 2 | far | 10 of 13 | Reduced meshes and impostors matched in tone, shade between far crowns |
| 2 | biomes | 11 of 12 | Desert of fixed share and broken up, riverside, stands and clearings, prairie |
| 2 | alpine | 9 of 14 | Fractured rock and talus up close, tree islands at the tree line |
| 3 | crowns | 9 of 15 | Krummholz at the tree line, araucaria limbs, dead araucaria |
| 3 | coast | 7 of 12 | No beach stripes, a strand with palm groves, nothing on wet sand |
| 3 | colour | 10 of 12 | Far forest grey-green not teal, a green per species at every level |
| 3 | dry | 8 of 12 | Red-brown desert apart from straw grass, cracked pans, a dwarf shrub |

Merge fixes made between rounds: bark keeps the near track's canopy shade and
leaves the far track's stand shade (`veg_light.glsl`); the bake version when
two tracks bumped it; no dry country on the beaches and the strand (`view.c`).

Checked on Felix's machine after round 2: the full-island ecosystem (3 seeds,
6 days) matches 5.0 (deaths per run 54, 55, 53 against 61, 55, 49; no
starvation). The quick ecoprobe and camprobe budgets were rewritten there on
the final world.

## Not done

- **Frame time.** On a GTX 1060 (high, 1920x1080, through WSL) the round 2
  merge drew the riverside forest in 38.5 ms against 27.9 ms on 5.0; the
  other views moved by 2 ms or less. Tree meshes went from 512k to 805k
  triangles over the pass. The switch distances (`CRET_VEG_TREE_LOD`,
  `CRET_VEG_LOD`, `CRET_VEG_NEAR`) have not been retuned, and round 3 was not
  timed on a GPU.
- The dwarf shrub reads lime-olive, not grey (it needs a `LEAF_TINT` entry);
  the cheirolep's reduced crown and the krummholz mats want `LOD_TONE` entries.
- The grass sward in the foreground is still a bright lime.
- River mouths are a channel through bare sand; there is no dune grass and no
  driftwood line.
- Above about 440 m the wind-shaped trees read as small dark patches: the
  summits carry little visible tree (`VEG_KRUMM_LO/HI` in `veg_place.h`).
- The camera budget's `short` count went from 0 to 2 frames with the bigger
  trunks.
