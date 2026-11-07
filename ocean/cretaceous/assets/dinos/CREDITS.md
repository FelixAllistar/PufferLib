# Dinosaur models

The game's dinosaurs are converted from these models with `tools/dino_convert.py`
(re-scaled, skeleton pruned, clips resampled to 30 Hz, decimated LODs, textures
re-packed to 2K/1K). The converted files are derivative works under the same
licence. They are not committed; `tools/fetch_dinos.sh` rebuilds them from the
original downloads.

Credits shown in the game must include:

| Species | Model | Author | Licence |
|---|---|---|---|
| Velociraptor (`raptor`) | [PBR Velociraptor (Animated)](https://sketchfab.com/models/8f1744af7b0847a2aabe3df90be802f0) | Ferocious Industries | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Stegosaurus (`stego`) | [PBR Stegasaurus (Animated)](https://sketchfab.com/models/ec254ea1554941fe8a131f62db0faf3d) | Ferocious Industries | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Tyrannosaurus (`trex`) | [Tyrannosaurus rex - Animated, Science-Based](https://animalanimator.com/free-3d-animated-t-rex-reference/) | Animal Animator | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |

Changes: scaled to 3.0 m, 8.5 m and 12 m long; joints without skin weights
removed; root motion removed from locomotion clips; meshes decimated to three
LODs; textures resized and repacked (albedo + AO, normal + roughness + metal).
The raptor's albedo is warmed in the shader (`render/dino.c` `SPECIES_TINT`).
