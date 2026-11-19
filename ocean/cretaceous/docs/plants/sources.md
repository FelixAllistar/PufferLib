# Plant sources: what is out there, and what we use

A survey of commercially usable foliage scans for the plant atlas (November
2026). Every licence below was read on the source's own licence page or API,
not taken from a third party. Rules: CC0 preferred; CC-BY only with a credit
line; never NC, ND, "editorial", or anything whose licence could not be
confirmed. Nothing comes from a `_PLACEHOLDER_RIPS_do_not_ship` folder.

The plant meshes are procedural (`render/veg_gen.h`). The scans only supply
the leaf, frond and bark pictures, and now their materials: normal maps and,
where the scan has one, a translucency (Scattering) map. Assets are fetched,
never committed: `tools/fetch_foliage.sh` (credits in
`assets/foliage/SOURCES.md`).

Map key: C colour, A alpha, N normal (OpenGL), R roughness, D displacement,
T translucency (ambientCG calls it Scattering).

## Licences

| Source | Licence | Where confirmed | Verdict |
|---|---|---|---|
| Poly Haven | CC0, every asset | https://polyhaven.com/license | use |
| ambientCG | CC0 1.0, every asset; names video games explicitly | https://ambientcg.com/license | use |
| Smithsonian 3D Open Access | CC0 per item (`usage.access` in the Open Access API) | api.si.edu/openaccess | only one relevant item (below) |
| Sketchfab | per model (`license` field of the model's API record) | api.sketchfab.com/v3/models/<uid> | CC0 ones possible; CC-BY with credit |
| ShareTextures | "custom CC0" that forbids violent content | sharetextures.com/p/license | reject: not CC0, and this is a predator game |
| FreePBR | free only for non-commercial use | freepbr.com/about | reject |
| textures.com, Megascans / Fab | proprietary licences | | reject |
| Quaternius, Kenney | CC0 but low-poly stylised art | | reject (wrong look) |

## What we use now

| Candidate | Link | Licence | Resolution fetched (max offered) | Maps fetched | What it replaces | Verdict |
|---|---|---|---|---|---|---|
| Poly Haven fern_02 | https://polyhaven.com/a/fern_02 | CC0 | 4k (8k) | C A N | was 2k C A: ground fern fronds, tree-fern pinnae | **use**, now 4k with normals; fronds 250-290 x 1024 px (were 128-288 x 512) |
| Poly Haven jacaranda_tree, leaves | https://polyhaven.com/a/jacaranda_tree | CC0 | 2k (8k) | C A N | new: tree-fern pinnae (with the fern_02 fronds) | **use**. The tree is Cenozoic, but one bipinnate leaf on a card reads as a fern pinna with real pinnules |
| Poly Haven fir_tree_01, twig | https://polyhaven.com/a/fir_tree_01 | CC0 | 2k (8k) | C A N | was ambientCG LeafSet019 (scale-leaf, Thuja-like): Metasequoia sprays and twig clusters | **use**. Flat two-ranked needle sprays are much closer to Metasequoia than scale leaves; Pinaceae are Cretaceous. LeafSet019 stays as the fallback |
| Poly Haven grass_medium_02 | https://polyhaven.com/a/grass_medium_02 | CC0 | 2k (4k) | C A N | was ambientCG Foliage001/006 strands: tall grass and sedge blades | **use**. Real single blades, green and dry, with normals. Foliage001/006 stay for cycad pinnae, palm segments and reeds |
| ambientCG LeafSet022 | https://ambientcg.com/view?id=LeafSet022 | CC0 | 2K (8K) | C A N **T** | was 1K C A: laurel-like twigs | **use**, now with normals and its Scattering scan |
| ambientCG LeafSet025 | https://ambientcg.com/view?id=LeafSet025 | CC0 | 2K (8K) | C A N **T** | was 1K C A: gunnera leaf, flowering herbs | **use**, with Scattering |
| ambientCG LeafSet001, 002, 004, 010, 013, 014, 018, 019, 020, 024; Foliage001, 006 | ambientcg.com/view?id=<id> | CC0 | 2K (4K-8K) | C A N | were 1K C A: the same uses as before (`assets/foliage/SOURCES.md`) | **use**, upgraded to 2K with normals |
| Poly Haven metasequoia_bark, palm_bark, bark_brown_02, japanese_cedar_bark | polyhaven.com/a/<id> | CC0 | 2k (8k), packed to 512 | C N AO D | were C only: trunk bark | **use**, with normal maps; AO and furrows (D) baked into the colour |
| Poly Haven pine_bark, bark_platanus, chinese_hackberry_bark | polyhaven.com/a/<id> | CC0 | 2k (8k), packed to 512 | C N AO D | araucaria plates, plane, smooth grey broadleaf bark | **use** (trunks hill-climb, 2026-11-19) |
| Poly Haven gray_rocks | https://polyhaven.com/a/gray_rocks | CC0 | 2k (8k) | C N R AO D | was Poly Haven rocks_ground_01 (pale pebbles in soil): the scree ground material, `tools/fetch_textures.py` | **use** (alpine hill-climb, 2026-11-19): angular grey blocks read as talus at 2 m; the shader mixes a second copy at twice the size so the blocks vary |

Why 2K and not 4K for most: the atlas layer is 512 texels and a leaf stamps
into it at 40-500 texels, so a 2K scan already gives every sprite at least
twice the texels it lands on (it is box-filtered down, which keeps it sharp).
The fern fronds fill a whole 512 layer, so they come from the 4k scan.

## Looked at and not used (yet)

| Candidate | Link | Licence | Max res, maps | Would replace | Verdict |
|---|---|---|---|---|---|
| Poly Haven island_tree_01 leaves | polyhaven.com/a/island_tree_01 | CC0 | 8k, C A N R D | laurel-like twigs | **rejected after a look**: its normal map carries the mesh's leaf curl (strong saturated gradients across each leaf), which tilts a flat card wrongly. LeafSet022 does the same job with a Scattering map |
| Poly Haven shrub_02 | polyhaven.com/a/shrub_02 | CC0 | 4k, C A N R D | cycad pinnae | **reject**: the diffuse is UV-stretched outside the leaves and the leaves are mottled grey-green |
| Poly Haven fir_sapling | polyhaven.com/a/fir_sapling | CC0 | 8k | conifer sprays | reject: the twig sheet is mostly one large shape, few clean sprays |
| Poly Haven pine_tree_01 / pine_sapling twig | polyhaven.com/a/pine_tree_01 | CC0 | 8k, C A N R D | pine needle clusters | maybe: the sheet mixes needle sprigs with big opaque bark areas, needs hand-picked crops. The game has no pines yet |
| Poly Haven moss_01 | polyhaven.com/a/moss_01 | CC0 | 4k, C A N R D | painted moss and clubmoss sprays (`paint_club`) | maybe, next: real sprigs for the sward's clubmoss variants |
| ambientCG Foliage007, Foliage008 | ambientcg.com/view?id=Foliage007 | CC0 | 8K / 4K, C A N R D **T** | grass and strap blades | maybe: the only blade scans with Scattering; worth a try as sedge and reed blades |
| ambientCG LeafSet023, 026, 029 | ambientcg.com | CC0 | 8K, C A N R D **T** | broadleaf / ground cover | maybe: have Scattering; LeafSet023 (heart-shaped, light green) could replace LeafSet004 |
| ambientCG autumn sets (LeafSet006-008, 011, 012, 015, 021, 027, 028, 030) | ambientcg.com | CC0 | up to 16K | leaf litter | reject for live foliage (autumn colour reads temperate) |
| ambientCG LeafSet016 (oak) | ambientcg.com | CC0 | 8K | broadleaf | reject: lobed oak outline is Cenozoic |
| Poly Haven calathea_orbifolia_01, anthurium_botany_01 | polyhaven.com | CC0 | 8k, C A N R D | large understory leaves | maybe: Zingiberales and Araceae are Late Cretaceous, but the calathea stripes look ornamental |
| Poly Haven chinese_cedar_bark | polyhaven.com/a/chinese_cedar_bark | CC0 | 8k, C N R D | araucaria / conifer bark | maybe: Cunninghamia, a Cretaceous lineage |
| Poly Haven forrest_ground_03 | polyhaven.com/a/forrest_ground_03 | CC0 | 8k | conifer forest floor | out of scope here (terrain, `cret/bedrock`) |
| Poly Haven periwinkle_plant | polyhaven.com | CC0 | 4k, the only Poly Haven translucency map | ground cover | reject: pink flowers look modern |
| Sketchfab ffish.asia / floraZia scans: Metasequoia spray, Osmunda, Dicranopteris, Ginkgo, Equisetum arvense, Selaginella | sketchfab.com/3d-models/cc0-dawn-redwood-m-glyptostroboides-9583e7aa90fb42a7a44ba83c196f8bd2 (and siblings) | CC0 (API `license.slug = cc0`) | 0.3-1.4 M faces, no leaf maps | exact Cretaceous genera: Metasequoia, ginkgo, horsetail, royal and forked ferns | **best next step**, but each needs a bake from geometry to a card (colour, alpha, normal), a Sketchfab login to download, and the Metasequoia and Ginkgo scans are autumn-coloured |
| Sketchfab CC-BY cycad, palm, tree fern, horsetail scans | e.g. "Cycas Revoluta" by Hervé Rey | CC BY 4.0, credit: "Cycas Revoluta" by Hervé Rey (https://sketchfab.com/herve.rey.421), CC BY 4.0 | whole plants | cycad and palm fronds | maybe, with credit: no CC0 cycad or palm leaf scan exists on any CC0 site |
| Smithsonian Cyathea furfuracea (dried tree-fern trunk) | n2t.net/ark:/65665/3ad622612-2779-4d68-83f3-01855a8e56c1 | CC0 (Open Access record) | 150k tris, 4096 texture | tree-fern trunk texture | maybe: would need a tiling strip baked from it |
| ShareTextures, FreePBR, textures.com, Megascans | | not free licences | | | reject |

## 3D plant models as the close level of detail

Considered and not done. The CC0 plant models (Poly Haven fern_02: 6,232
triangles; the scanned Sketchfab ferns: 0.4-0.9 million) are themselves
alpha-tested leaf cards on a few stems, the same construction as our
procedural plants, so as a near level of detail they would add the
silhouette of one fixed specimen, not detail: every fern in a stand would
be the same plant. Ours vary per seed, sway in the wind, bend under
animals (trample map), take the impostor bake and the cross-fade between
levels; a model would need all of that rebuilt for it. The better use of
those scans is what this branch does: their leaf textures and normal maps
on our cards. The Sketchfab scans of exact Cretaceous genera are worth
baking into new cards later (see above).

No cycad, palm leaf, ginkgo, horsetail, araucaria or tree-fern pinna exists
as a CC0 leaf scan on any of these sites; those layers keep their stand-ins
(blade scans for cycad pinnae and palms, painted ginkgo fans, horsetails and
araucaria ropes, now with painted normals).
