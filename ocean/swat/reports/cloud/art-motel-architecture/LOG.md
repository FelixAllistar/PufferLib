# Briar Court motel architecture pass: hill-climb log

Baseline: `reports/cloud/art-motel-exterior/*.jpg` (5.0). Captures below are
`exterior_capture` at 960x540 unless noted.

## References

All downscaled copies in `refs/` are public domain or CC BY (credited here and
in `assets/environment/motel_exterior/README.md`).

| Item | File | Source | Author | License |
| --- | --- | --- | --- | --- |
| Front / end wall | refs/ref_front_truxton_pd.jpg | https://commons.wikimedia.org/wiki/File:Old_motel,_Route_66,_Truxton,_Arizona_LCCN2010630145.tif | Carol M. Highsmith (Library of Congress) | Public domain |
| Front elevation, roof/parapet, block coursing | refs/ref_elev_seligman_pd.jpg | https://commons.wikimedia.org/wiki/File:Old_motel,_Seligman,_Arizona_LCCN2010630159.tif | Carol M. Highsmith (Library of Congress) | Public domain |
| Walkway | refs/ref_walkway_barstow_pd.jpg | https://commons.wikimedia.org/wiki/File:Motel_in_Barstow,_California_LCCN2013633211.tif | Carol M. Highsmith (Library of Congress) | Public domain |
| Office | refs/ref_office_deserthills_ccby4.jpg | https://commons.wikimedia.org/wiki/File:Desert_Hills_Motel_front_Tulsa_OK_2025-10-23_17-20-25_1.jpg | G. Edward Johnson | CC BY 4.0 |
| Rear | refs/ref_rear_palomar_habs_pd.jpg | https://commons.wikimedia.org/wiki/File:Central_building,_rear_elevation_-_Hotel_Palomar_Courts,_3975_Greenwood_Road,_Shreveport,_Caddo_Parish,_LA_HABS_LA-1308-17.tif | HABS LA-1308-17 (Library of Congress) | Public domain |
| Roofline | refs/ref_roof_safari_ccby2.jpg | https://commons.wikimedia.org/wiki/File:Motel_Safari_(15313470878).jpg | Tony Hisgett | CC BY 2.0 |

Looked at but not committed (CC BY-SA, URL only):
https://commons.wikimedia.org/wiki/File:2021_Buckhorn_Baths_Motel_15_motel_building.jpg (desert strip motel, parapet + posts),
https://commons.wikimedia.org/wiki/File:Warm_Mineral_Springs_Motel_rear_wing.jpg (through-wall AC sleeves, walkway).

## Iterations

### Item 1: front / room block (ref: Truxton, Highsmith PD)

Blender view `front` (motel_exterior_preview.py), eye (13.5,1.4,6.5) -> (0,1.7,0), 46 deg.

- iter 0: first build: teal 0.44 m main-roof fascia + coping, 0.48 m flat teal canopy fascia with K gutter, 0.20 m box posts with knee braces, plinth, door/window surrounds, AC collars, pilasters. Reads as a stacked double band; reference canopy is a pale sloped corrugated awning. closer: baseline
- iter 1: canopy fascia -> sloped corrugated sheet (wall 2.80 -> eave 2.60) with shallow eave board; camera matched to the reference. closer: yes (awning reads as a roof, not a band)
- iter 2: main-roof teal fascia -> stucco parapet + galvanized coping (like the reference end wall top); removed floating end-post downspouts (no front gutter on a corrugated awning); posts 0.20 -> 0.17 m. closer: yes
- iter 3: sheet slope wall 2.80 -> 2.98 m. closer: no (invisible from eye height); kept for the roof view but not counted
- iter 4: corrugated sheet bent into a 0.28 m bull-nose at the eave (the reference awning's curved scalloped edge). closer: yes
- iter 5: bull-nose 0.28 -> 0.36 m, wave amplitude 12 -> 16 mm (scallops read); 4 -> 3 segments per wave, arc 6 -> 4 steps (54k -> 37k tris). closer: yes
- iter 6: canopy end closure teal -> cream soffit colour, beam 0.22 -> 0.17 m. closer: marginal
- engine e1: `exterior_capture --eye 13.5,1.4,6.5 --target 0,1.7,0 --fov 46`: overlay loads/draws; chain-link fence in the way, awning blown out white, ochre stripe of the old canopy deck bleeds through the sheet. 
- engine e2: eye moved inside the fence (12,1.45,4.6); sheet albedo .70 -> .50. closer: yes
- engine e3: sheet eave 2.60 -> 2.72 m (clears the old deck edge, no bleed), wall end 3.0 m, albedo .42; the bull-nose now drops to ~2.3 m like the reference awning edge. closer: yes
