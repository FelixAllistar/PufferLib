# Flora: species, communities and food

What grows on the island, where and why, what each plant offers to eat, and
how the renderer draws it. Plants are placed from the world bake's eight
vegetation layers (`terra/terra.c`, `TERRA_VEG_*`); this document covers
what is derived from them at load.

## 1. Taking stock (before)

Eight viewpoints, one in each kind of country, found from the world data by
`tools/floraprobe.c` (`make -C ocean/cretaceous floraprobe`, run from the
repository root; it prints them as a `CRET_SHOT_CAM` string). Each is 1.8 m
over the ground, an adult's eye. The island (seed 1) has no lakes: every
inland water cell is river, so the pond or wetland view is the flattest,
muddiest slow reach of river with horsetails.

```bash
CRET_SHOT_CAM="$(./build/cretaceous/floraprobe)" \
  ./build/cretaceous/cretaceous capture DIR --size 1280 720 --shot 'fl_*'
```

| site | where | layers within 40 m (0..1) |
|---|---|---|
| `fl_beach` | 2128, 4024, 0.6 m | broadleaf 0.23, palm 0.24, fern 0.17 |
| `fl_river` | 2968, 4288, 58 m | conifer 0.55, broadleaf 0.24, fern 0.51, horsetail 0.08 |
| `fl_valley` | 2800, 5728, 30 m | tree fern 0.75, broadleaf 0.48, conifer 0.42, fern 0.59 |
| `fl_lowland` | 2536, 4360, 22 m | broadleaf 0.90, fern 0.66, tree fern 0.15 |
| `fl_upland` | 3736, 3592, 357 m | conifer 0.85, fern 0.56, shrub 0.28, cycad 0.14 |
| `fl_ridge` | 5176, 3616, 392 m | conifer 0.51, shrub 0.31, cycad 0.28, fern 0.35 |
| `fl_clearing` | 3952, 5824, 393 m | conifer 0.54, cycad 0.26, fern 0.38, shrub 0.30 |
| `fl_wetland` | 2680, 3520, 39 m | broadleaf 0.64, fern 0.55, horsetail 0.18 |

![before](flora/sites_before.jpg)

What grows there now, and why it reads as fake:

- **The same few plants everywhere.** Every site from the beach to 390 m has
  the same ground fern (one species, two photo fronds, at different sizes),
  the same shrub and the same sward. A kind's eight variants are four tree
  species, but the ground kinds (fern, horsetail, shrub) are one species
  each, and the four tree species of a kind are picked by a 140 m hash with
  only one habitat rule (metasequoia on wet ground). So the valley floor,
  the lowland forest and the upland forest differ only in the conifer to
  broadleaf share.
- **No open ground.** The bake makes forest the default cover: the probe
  could not find a clearing anywhere inland. The `fl_clearing` view, the
  thinnest canopy ringed by forest, still has a trunk in front of the lens.
  There are no gaps at the 50-200 m scale, so walking anywhere looks the
  same.
- **No understorey structure.** Under the canopy there is one layer, ferns at
  knee height, then trunks. No saplings, no small understorey trees, no
  shade ferns different from the open-ground ones, no mosses or clubmosses.
- **No zonation.** Along the river (`fl_river`, `fl_wetland`) the ferns run
  to the waterline and conifers stand in the river; horsetails are the only
  water-margin plant, and the same grass and sedge grow at every bank. Up
  the slope the mix only shifts from broadleaf to conifer: the dry ridge
  (`fl_ridge`) is conifer forest with ferns, not cycad scrub. The beach goes
  from sand straight into broadleaf forest; palms are thin, and there is no
  strand scrub.
- **Nothing dead.** No standing dead trees; logs and stumps lie about
  (props) but no tree stands broken or dead, and nothing grows on the logs.
- **Nothing in flower or fruit.** No flowers, cones, seeds or fruit on any
  plant and nothing fallen under them, so nothing tells a herbivore (or a
  player) where food is, and the forest reads as plastic greenery.
- **No still water plants.** No water lilies or floating plants; there is no
  still water on this island except slow river reaches.

## 2. The species table

31 species over the eight bake layers plus water lilies, clubmoss and fungi,
in `render/flora.h` (`FLORA`, `FLORA_SLOT`). Each bake layer keeps its eight
variant slots (so its draws, impostor layers and the world file stay as they
were); a slot holds a species, and a species in two slots is the same plant
from two seeds. The world file does not change: the species are derived at
load (section 3). `floraprobe --table` prints this table from the code.

Habitat columns are where a species grows fully (it fades out over a soft
margin beyond); moisture, nearness to river water and canopy shade are 0..1.
"Common" weighs it against the other species of its layer where both suit;
"stands" is how strongly it grows in single-species patches with gaps
(0 mixes evenly, 1 pure stands). Food is kg per plant at scale 1
(section 4).

| species | layer (slots) | modelled on | elevation | moisture | near water | shade | common | stands | food: low / high / fruit / water (kg) | drops |
|---|---|---|---|---|---|---|---|---|---|---|
| araucaria | conifer (0,4) | Araucaria sect. Eutacta (Norfolk-like whorled tiers, big seed cones) | 60-520 m | 0.25-0.75 | 0-0.5 | any | 1 | 0.6 | 0 / 2 / 3 / 0 | cones |
| podocarp | conifer (1,5) | Podocarpus / Prumnopitys (fleshy seed cups) | < 300 m | > 0.4 | any | any | 1 | 0.5 | 0.5 / 4 / 2 / 0 | seeds in red cups |
| umbrella araucaria | conifer (2) | Araucaria sect. Araucaria (monkey-puzzle-like dome) | > 220 m | 0-0.55 | 0-0.3 | any | 0.7 | 0.7 | 0 / 1 / 3 / 0 | cones |
| dawn redwood | conifer (3,7) | Metasequoia / Sequoia-like redwoods of floodplains | < 220 m | > 0.5 | > 0.25 | any | 1 | 0.8 | 0 / 2 / 0.5 / 0 | - |
| cheirolep cypress | conifer (6) | Frenelopsis / Pseudofrenelopsis (Cheirolepidiaceae), small cypress-like trees of dry and salty ground | any | 0-0.42 | 0-0.2 | any | 0.8 | 0.6 | 1 / 2 / 0.5 / 0 | - |
| magnolia | broadleaf (0,4) | Magnoliaceae / Magnoliidae (Archaeanthus: large flowers, cone-like fruit) | < 250 m | > 0.45 | any | any | 1 | 0.5 | 0.5 / 6 / 2 / 0 | cone-like fruit |
| laurel | broadleaf (1,5) | Lauraceae (Mauldinia, small dark drupes), evergreen | < 400 m | > 0.3 | any | any | 1.2 | 0.4 | 0.5 / 5 / 2 / 0 | dark drupes |
| katsura | broadleaf (2) | Trochodendroides / Nyssidium (Cercidiphyllum-like), many-stemmed, of wet valley floors | < 300 m | > 0.55 | > 0.1 | any | 0.8 | 0.6 | 1 / 6 / 0.3 / 0 | - |
| river plane | broadleaf (6) | Platanites / Credneria (Platanaceae), river banks | < 260 m | > 0.5 | > 0.45 | any | 1 | 0.4 | 0.5 / 6 / 0.5 / 0 | - |
| ginkgo | broadleaf (3) | Ginkgo (fan leaves, fleshy yellow seeds), disturbed stream sides | 20-450 m | 0.35-0.9 | 0.1-0.9 | any | 0.35 | 0.7 | 0.3 / 5 / 3 / 0 | yellow seeds |
| chloranth tree | broadleaf (7) | Chloranthaceae (Hedyosmum-like small understorey tree, white berries) | < 350 m | > 0.45 | any | > 0.45 | 0.9 | 0.3 | 2 / 4 / 1 / 0 | berries |
| cycad | cycad (0,2,4,6) | Cycas-like trunked cycad (orange seeds on the crown) | any | 0-0.6 | 0-0.4 | 0-0.7 | 1 | 0.4 | 3 / 1 / 2 / 0 | orange seeds |
| squat cycad | cycad (1,5) | Zamia / Dioon-like squat cycad with a central cone | > 80 m | 0-0.5 | 0-0.3 | 0-0.6 | 0.8 | 0.5 | 3 / 0 / 1 / 0 | - |
| williamsonia | cycad (3,7) | Williamsonia / Williamsoniella (Bennettitales, flower-like cones) | < 600 m | 0.2-0.75 | any | 0-0.7 | 0.45 | 0.5 | 2 / 1 / 1 / 0 | - |
| bracken | fern (0,4) | Dennstaedtiaceae (Pteridium-like), tall fern of open ground | any | 0.15-0.8 | 0-0.5 | 0-0.35 | 1.2 | 0.7 | 1 / 0 / 0 / 0 | - |
| shield fern | fern (1,5) | Dryopteridaceae (Dryopteris-like), low fern of shade | any | > 0.35 | any | > 0.4 | 1.2 | 0.3 | 1 / 0 / 0 / 0 | - |
| forking fern | fern (2,6) | Gleicheniaceae (Gleichenites), sprawling forked thickets on open slopes | > 30 m | 0.1-0.7 | 0-0.3 | 0-0.45 | 0.9 | 0.7 | 0.8 / 0 / 0 / 0 | - |
| royal fern | fern (3,7) | Osmundaceae (Osmunda / Osmundastrum, brown fertile fronds), wet ground | any | > 0.6 | > 0.3 | any | 1 | 0.5 | 1.5 / 0 / 0 / 0.5 | - |
| tree fern | tree fern (0,1,4,5) | Cyatheaceae (Cyathea-like), tall, of wet shaded valleys | < 400 m | > 0.6 | any | > 0.3 | 1 | 0.4 | 1 / 3 / 0 / 0 | - |
| stout tree fern | tree fern (2,3,6,7) | Dicksoniaceae (Dicksonia-like), short and stout, more open ground | any | > 0.45 | any | 0-0.6 | 0.8 | 0.4 | 2 / 2 / 0 / 0 | - |
| giant horsetail | horsetail (0,1,4,5) | Equisetites, stands several metres tall at the water's edge | any | any | > 0.65 | any | 1 | 0.3 | 2 / 0 / 0 / 2 | - |
| field horsetail | horsetail (2,3,6,7) | Equisetum arvense-like low branched form of damp ground | any | > 0.4 | 0-0.75 | any | 1 | 0.4 | 1 / 0 / 0 / 0.5 | - |
| fan palm | palm (0,1,2,4,5,6) | Sabalites (costapalmate fan palm), warm lowlands and coasts | < 40 m | any | any | any | 1 | 0.4 | 0 / 2 / 3 / 0 | palm fruit |
| feather palm | palm (3,7) | a pinnate-leaved palm of the strand (palm fossils: Phoenicites-like leaves) | < 12 m | any | any | any | 0.6 | 0.5 | 1 / 2 / 3 / 0 | palm fruit |
| berry shrub | shrub (0,4) | a fleshy-fruited early angiosperm shrub (cf. Sapindopsis, Chloranthaceae shrubs) | < 500 m | 0.3-0.9 | any | 0.15-0.75 | 1 | 0.4 | 3 / 0 / 2 / 0 | berries |
| flowering herb | shrub (1,5) | an early eudicot herb (Leefructus, Ranunculales-like) of clearings and banks | any | > 0.25 | any | 0-0.3 | 1.1 | 0.6 | 1.5 / 0 / 0.3 / 0 | - |
| gunnera | shrub (2,6) | Gunnera (Gunneraceae: pollen from the Late Cretaceous), huge leaves at streamsides | < 400 m | > 0.55 | > 0.5 | any | 1 | 0.4 | 4 / 0 / 0 / 1 | - |
| evergreen scrub | shrub (3,7) | small-leaved evergreen scrub of dry ridges and the strand | any | 0-0.55 | 0-0.3 | 0-0.6 | 0.9 | 0.4 | 2 / 0 / 0.3 / 0 | - |
| water lily | - | Nymphaeales (Microvictoria, Turonian; Nymphaeaceae pads and flowers) | any | any | any | any | 1 | 0.6 | 0 / 0 / 0.2 / 3 | - |
| clubmoss and moss | - | Selaginella / Lycopodiales mats and moss cushions of damp shade | any | > 0.45 | any | > 0.4 | 1 | 0.5 | 0.2 / 0 / 0 / 0 | - |
| bracket fungi and mushrooms | - | polypores and gilled mushrooms on dead wood (Cretaceous mushrooms in amber: Palaeoagaracites) | any | any | any | any | 1 | 0.0 | 0 / 0 / 0 / 0 | - |

### What each is modelled on

- **araucaria**: *Araucaria* (section *Eutacta*, Norfolk-like); araucarian
  wood, foliage and large seed cones are common in Late Cretaceous floras of
  both hemispheres (e.g. *Araucaria* cones in the Maastrichtian of Patagonia).
- **podocarp**: *Podocarpus* / *Prumnopitys*; podocarp leafy shoots and
  pollen are abundant in Late Cretaceous Gondwanan floras; living podocarps
  bear the seed on a fleshy red receptacle eaten by animals.
- **umbrella araucaria**: *Araucaria* section *Araucaria* (monkey-puzzle);
  the section is known from Jurassic and Cretaceous cones and shoots
  (*Araucaria mirabilis*, Jurassic; Cretaceous shoots of the section).
- **dawn redwood**: *Metasequoia* / *Sequoia*-like Cupressaceae (taxodioids),
  dominant on Late Cretaceous floodplains of the northern continents
  (*Metasequoia* from the Late Cretaceous of Canada and Japan).
- **cheirolep cypress**: *Frenelopsis* / *Pseudofrenelopsis*
  (Cheirolepidiaceae), small scale-leaved cypress-like trees of dry, saline
  and coastal settings through the Cretaceous; *Frenelopsis* persists into
  the Late Cretaceous.
- **magnolia**: *Archaeanthus* (Albian-Cenomanian, Kansas), a magnolialean
  tree with large flowers and cone-like aggregate fruits of follicles.
- **laurel**: Lauraceae; *Mauldinia* flowers and fruits (Cenomanian) and
  abundant lauraceous leaves in the Dakota Formation.
- **katsura**: *Trochodendroides* leaves with *Nyssidium* fruits, a
  *Cercidiphyllum*-like tree, among the commonest Late Cretaceous to
  Paleocene angiosperms of the northern continents, on wet floodplains.
- **river plane**: *Platanites* / *Credneria* leaves and platanoid fruit heads
  (e.g. *Platananthus*); Platanaceae were common streamside trees from the
  mid-Cretaceous.
- **ginkgo**: *Ginkgo* leaves (e.g. *Ginkgo pluripartita*, *Ginkgoites*) occur
  through the Cretaceous; living *Ginkgo* bears fleshy yellow seeds and grows
  on disturbed stream sides.
- **chloranth tree**: Chloranthaceae (pollen *Clavatipollenites*, fossil
  flowers from the Early Cretaceous), among the earliest angiosperms; living
  *Hedyosmum* is a small understorey tree with white fleshy fruit.
- **cycad**: *Cycas*-like cycads; Cretaceous cycad leaves (*Nilssonia*,
  *Pseudoctenis*) and stems are common; *Cycas* bears orange seeds on the
  crown.
- **squat cycad**: a *Zamia* / *Dioon*-like short-stemmed cycad with a central
  seed cone (cycad cones and short stems are known through the Mesozoic).
- **williamsonia**: *Williamsonia* / *Williamsoniella* (Bennettitales), with
  flower-like cones of bracts; bennettitaleans last into the Late Cretaceous.
- **bracken**: Dennstaedtiaceae; the family's lineage dates to the
  Cretaceous; *Pteridium* is the model for a tall fern of open ground.
- **shield fern**: Dryopteridaceae / eupolypod ferns, which diversified in the
  shade of angiosperm forests in the Cretaceous.
- **forking fern**: Gleicheniaceae (*Gleichenites*), among the commonest
  Cretaceous ferns, forming sprawling thickets on open, poor ground.
- **royal fern**: Osmundaceae (*Osmunda*, *Osmundastrum*), known from
  Cretaceous rhizomes and fronds; fertile fronds brown with sporangia.
- **tree fern**: Cyatheaceae (*Cyathea*-like), tall tree ferns of wet forest.
- **stout tree fern**: Dicksoniaceae (*Dicksonia*-like; dicksoniaceous
  fronds such as *Coniopteris* are common Mesozoic fossils).
- **giant horsetail**: *Equisetites*, Mesozoic horsetails with stems up to
  several metres and 10 cm thick.
- **field horsetail**: an *Equisetum arvense*-like low, whorled form.
- **fan palm**: *Sabalites* costapalmate palm leaves (Late Cretaceous, North
  America).
- **feather palm**: pinnate palm leaves (*Phoenicites*-like) appear by the end
  of the Cretaceous; modelled as a shorter feather palm of the strand.
- **berry shrub**: a fleshy-fruited early angiosperm shrub; small drupes and
  berries are known from mid-Cretaceous fruit floras (e.g. Portugal).
- **flowering herb**: *Leefructus* (Early Cretaceous, China), a
  ranunculid eudicot herb; modelled as a flowering herb of clearings.
- **gunnera**: *Gunnera* pollen (*Tricolporites reticulatus*) from the Late
  Cretaceous; living *Gunnera* has huge leaves at streamsides.
- **evergreen scrub**: small-leaved evergreen scrub; dry-ground angiosperm and
  conifer shrubs of Cretaceous coastal and upland settings.
- **water lily**: Nymphaeales (*Microvictoria*, Turonian of New Jersey;
  nymphaealean seeds and leaves from the Early Cretaceous on).
- **clubmoss and moss**: *Selaginella* and Lycopodiales, and mosses, known
  through the Mesozoic.
- **bracket fungi and mushrooms**: gilled mushrooms in Cretaceous amber
  (*Palaeoagaracites*, Burmese amber, about 99 Ma) and wood-decay fungi in
  Cretaceous wood.

## 3. Communities

All of this is derived at load from the world file as it is (no rebake, no
new fields saved): `terra_habitat` (`terra/terra.c`) builds a half-resolution
habitat grid (8 m cells: moisture index, nearness to river water and to the
sea, landform) from the height, water, flow, splat and vegetation layers, in
about 0.4 s on one core. It does not read rain, soil or temperature, which the
web build drops, so native and web clients place the same plants (and the
server's trunk colliders match what every client draws). It costs 4 MB at
2048² (transient peak 16 MB).

How a kind's plants become a community (`render/flora.h`):

- **Species by habitat.** Where the scatter puts a plant of a layer, each of
  the layer's species weighs in by its commonness times its fit to the site
  (elevation, slope, moisture, nearness to water and coast, ridge or hollow,
  canopy shade over the spot), times its stand noise. The dominant few come
  from commonness and fit; rare species (ginkgo, umbrella araucaria,
  williamsonia) have low weights or narrow habitats.
- **Stands and gaps at 50-200 m.** Each species has its own low-frequency
  noise seeded by the world seed (70-124 m scale): species with a high
  "stands" value grow in near-pure patches and are absent between them.
  A canopy-gap noise (150 m) opens clearings where the canopy layers thin to
  nothing; there bracken, flowering herbs, cycads and scrub take over (they
  pick by low shade, and their density rises in the gaps).
- **Structure.** Canopy (conifer and broadleaf layers), understorey (the
  chloranth tree, tree ferns, cycads, shrubs; shade-tolerant species pick by
  canopy shade), ground layer (shield fern and clubmoss and moss under a
  closed canopy, bracken and the forking fern in the open), and dead wood.
- **Along rivers**, from the water out: water lilies on quiet shallow water
  (0.25-1.6 m deep, a level surface; `place_lilies` in `render/veg.c`), giant
  horsetails at the edge, gunnera and royal ferns on the bank (shrub density
  rises there), tree ferns a little further back (their density rises with
  nearness to water but not on the bank itself), river plane, katsura and
  dawn redwood, then the forest. The tall canopy stands back from the
  waterline (conifer and broadleaf density falls by half within the last few
  metres).
- **Up slopes**: lowland magnolia, laurel, podocarp and the understorey
  tree below 250-400 m; araucaria from 60 m up; umbrella araucaria above
  220 m; on dry convex ridges above 120-260 m the canopy opens (by up to
  65%) into cycad scrub (cycad density up to 2.2x) with evergreen scrub and
  the cheirolep cypress.
- **Coasts**: fan palms below 40 m and feather palms on the strand below
  12 m; the canopy thins on the strand (by up to 60%) and the shrub layer
  thickens with evergreen scrub.
- **Dead wood in old stands.** An old-stand noise (180 m) marks about a fifth
  of the forest as old: there standing dead trees appear (`VEG_SNAG`: a dead
  conifer with a broken top and whorl stubs, a broken conifer, a dead
  broadleaf with bare forks, a leaning broken trunk; weathered grey bark), up
  to about 13 per hectare, and logs and stumps are about 2.5 times as common
  as in young stands (the same mean over the island; `render/props_place.c`).
  Bracket fungi grow on stumps and mushrooms beside stumps and logs near the
  camera.
- **Sward**: on moss ground and under forest litter, clubmoss mats and moss
  cushions replace part of the fern and forb tufts.

Stems per hectare within 60 m of each viewpoint (`floraprobe`, after):

| site | what stands there |
|---|---|
| `fl_beach` | fan palm 24, feather palm 11, laurel 12, magnolia 11; ferns thin |
| `fl_river` | dawn redwood 17, podocarp 13, araucaria 6, katsura 5, river plane 3; royal fern 855, forking fern 617; giant horsetail 11, field horsetail 30; gunnera 6, flowering herb 13; standing dead 10 |
| `fl_valley` | tree fern 241, podocarp 24, chloranth tree 15, dawn redwood 12, laurel 11; shield fern 2320, royal fern 703 |
| `fl_lowland` | magnolia 48, chloranth tree 42, laurel 39, ginkgo 12, katsura 6; shield fern 3472 |
| `fl_upland` | araucaria 49, podocarp 10; cycad 13, williamsonia 10; bracken 950, forking fern 787; flowering herb 56, berry shrub 30 |
| `fl_ridge` | cycad 47, squat cycad 43, williamsonia 21; evergreen scrub 147, flowering herb 47; umbrella araucaria 4; bracken 427, forking fern 428 |
| `fl_clearing` | araucaria 33, cheirolep 11; squat cycad 35, williamsonia 25; evergreen scrub 140 |
| `fl_wetland` | magnolia 21, katsura 19, laurel 16; giant horsetail 63, field horsetail 67; royal fern 953; gunnera 15 |

## 4. Food you can see, and the food query

**On the plants** (`add_fruit` and the species generators in
`render/veg_gen.h`): araucarias carry big upright seed cones at their upper
branch ends and pollen cones lower down; podocarps a dark seed on a red
fleshy cup; magnolias big cupped cream flowers with a green cone-like
centre, and red cone-like fruit; laurels clusters of blue-black drupes;
ginkgo pairs of yellow seeds; the river plane seed balls on long stalks;
the chloranth tree white berries; dawn redwood and the cheirolep cypress
small round cones; the trunked cycad a ring of orange seeds in its crown,
the squat cycad a big central cone, williamsonia a ring of cream and red
bracts round its cones; palms branched stalks of drupes (purple-black on the
fan palm, orange on the feather palm); the berry shrub red and orange
berries; the flowering herb yellow or violet flowers; gunnera a red-brown
flower spike; water lilies white or pink flowers and buds. Fruit and
flowers are meshes (balls with painted skins, double-sided petal cards),
not texture, so they hold their shape at 2 m.

**Fallen** (`VEG_DROP`, `place_drops` in `render/veg.c`): under every plant
that bears something to eat (`FLORA[].drop`) lie clusters of what it drops,
inside three quarters of its crown, hashed from the plant's position so they
are always the same: araucaria cones and loose scales, podocarp seeds with
their red cups, magnolia fruit and fallen tepals, laurel drupes, ginkgo
seeds (some squashed), cycad seeds, palm fruit, berries. They are drawn
within 30% of the ground-cover range (27 m on high).

**The query** for the simulation (`world.h`):

```c
CretFood f = cret_food_kinds_at(world, x, z);   // f.low, f.high, f.fruit, f.water in 0..1
```

- `low`: low browse (ferns, horsetails, cycad fronds, herbs, shrubs, under 1.5 m);
  `high`: high browse (tree, tree-fern and palm foliage 1.5-6 m up);
  `fruit`: fruit, seeds and cones, hanging and fallen; `water`: water plants
  (lilies, horsetails and royal ferns in the shallows).
- Each is relative abundance, `1 - exp(-kg_per_m2 / CRET_FOOD_SCALE[k])`
  with scales 0.35, 0.05, 0.03 and 0.08 kg/m², so 0.63 means that much edible
  matter per square metre; invert with `-scale * log(1 - a)` for kg/m².
- It is derived deterministically from the same placement the renderer
  draws: for every bake layer, the scatter's chance of a plant per lattice
  site (layer density, habitat multiplier, dice) over the lattice cell,
  times each species' share there (`flora_shares`) and its food per plant
  (`FLORA[].food`, kg at scale 1), plus lilies by their placement rule. The
  same world gives the same numbers on every machine and thread count.
- The grid is 16 m cells (4 points each, bilinear between cells), 1 MB at
  2048², built on the first call (well under a second with threads); after
  that a call is a few byte lookups, cheap enough for every animal decision.
  It is read-only and thread-safe. `cret_forage` (sim.c) is unchanged.
- At the viewpoints above: lowland forest low 0.85, high 0.81, fruit 0.60;
  upland araucaria forest high browse only 0.04 but fruit 0.28 (cones); the
  dry ridge low 0.50, fruit 0.63 (cycad seeds); the wetland water 0.90; the
  beach little of anything (0.12 / 0.07 / 0.13 / 0).

For diets (ecosystem_next.md section 8): a grazer feeding low reads `low`,
a browser `high`, a frugivore or seed eater `fruit`, and wading herbivores
`water`; the same numbers tell where each species' country is.
