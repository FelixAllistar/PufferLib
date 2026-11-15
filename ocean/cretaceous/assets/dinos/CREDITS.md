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
| Pachycephalosaurus (`pachy`) | [PBR Pachycephalasaurus (Animated)](https://sketchfab.com/models/6eea5cee4afa4730bf75c6329a43e56d) | Ferocious Industries | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Sauropelta (`sauropelta`) | [Animated Sauropelta (Free)](https://sketchfab.com/models/c6373f12f3954facb8d5fe48055c9161) | AneesAnimates | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Gallimimus (`galli`) | [Blender \| Gallimimus](https://sketchfab.com/models/4993970ad48f41af813e65f202acaf41) | SkinnyDoesModels | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |

Changes: scaled to 3.0 m, 8.5 m, 12 m, 4.5 m, 5.2 m and 6.0 m long; joints without skin weights
removed; root motion removed from locomotion clips; meshes decimated to three
LODs; textures resized and repacked (albedo + AO, normal + roughness + metal).
The tyrannosaur's eat, drink, call, sleep, hurt and death clips and the gallimimus's idle, eat, drink,
call, sleep, hurt and death clips are the velociraptor's (Ferocious Industries), copied onto their
skeletons with `tools/dino_retarget.py`.
The raptor's albedo is warmed in the shader (`render/dino.c` `SPECIES_TINT`).
