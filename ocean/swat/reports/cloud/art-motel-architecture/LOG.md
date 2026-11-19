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

### Item 2: walkway (ref: Motel in Barstow, Highsmith PD)

Blender view `walkway`, eye (9,1.5,8.5) -> (-5,1.7,0), 48 deg.

- iter 0: front-pass state at the old walkway camera. Reference: pale columns with curved brackets at the beam, a meter per unit, dark asphalt.
- iter 1: camera matched to the reference (farther, shallower). closer: yes (comparable)
- iter 2: preview ground value .16 -> .045 (asphalt); diagonal pipe braces -> 0.42 m solid quarter-round corbel brackets on both sides of each post. closer: yes
- iter 3: per-room socket meter + disconnect + conduit to the soffit between door casing and window jamb. closer: yes (the reference's per-unit meters)
- iter 4: post capital (two-step block) and base fillet. closer: marginal (too small at this distance)
- iter 5: post sleeves/capitals/corbels teal -> cream (pale columns as in the reference). closer: yes
- iter 6: beam teal -> cream. closer: no (beam is in shade); reverted, two weak iterations in a row -> stop.
- engine e1: same camera; picnic table blocks the foreground.
- engine e2: eye (6.5,1.5,6.0) -> (-6,1.7,0.3), plus the old ext2_gallery view: pale corbel columns, exposed rafters and the bull-nose read as a real covered walkway. closer: yes

### Item 3: office (ref: Desert Hills Motel office, G. Edward Johnson, CC BY 4.0)

Blender view `office`, eye (-15,1.5,6.5) -> (-8.5,2.3,-0.5), 60 deg (wide like the reference).

- iter 0: small front gable (ridge 4.05 m) perched on the canopy over the office bay; the office still reads as just another bay.
- iter 1: full-depth front-gable office roof (x -12.3..-8.0, z -6.3..+2.45) in red standing-seam metal with seams every 0.5 m, ridge cap, maroon eave fascia; office bay parapet, cooler and corrugated awning removed under it; camera matched. closer: yes (a distinct building like the reference)
- iter 2: ridge 3.85 -> 3.55 m (21 deg pitch, the reference's low roof), eave fascia 0.30 -> 0.40 m. closer: yes
- iter 3: maroon tie band across the gable front at eave level (the porte-cochere fascia that wraps the reference's front), OFFICE panel moved down into the gable. closer: yes
- iter 4: maroon trim around the lobby glazing + dark sill strip. closer: slightly
- engine e1: same camera + the old ext4 view: roof/fascia read orange-red (too light) and the glazing trim is missing (the lobby glass draws on its own path, never reaching the overlay hook).
- engine e2: metal roof albedo .30 -> .17 (dark maroon like the reference), trim re-owned by the office walkway slab (tag 4). closer: yes

### Item 4: rear wall (ref: Hotel Palomar Courts rear elevation, HABS LA-1308-17, PD)

Blender view `rear`, eye (-2,1.5,-15.5) -> (-2,1.8,-6), 52 deg (straight-on like the HABS elevation).

- iter 0: block coursing (8x16 in running bond, 11 mm joints), small high obscure bathroom windows with aluminium frames and precast sills, vent stack, cleanout, hose bib, eave conduit, scupper + conductor head + rectangular downspouts, laundry awning/sign/dryer hoods. The LAUNDRY lettering on the east-facing annex wall is mirrored; joints too dark for a painted block wall.
- iter 1: text orientation fixed for any wall frame (reads left-to-right from outside); mortar .20 -> .30 grey-brown (painted block, like the reference's whitewashed wall). closer: yes
- iter 2: parapet coping galvanized -> dark teal with a 0.07 m overhang and skirt: a crisp dark roof edge like the reference's eave line. closer: yes
- iter 3: electric service mast + weatherhead above the rear meter on 38; gas meter set with risers and regulator on 62 (the reference's utility clutter). closer: slightly
- engine e1: same camera: rear wall in shade, windows read as blank grey panels.
- engine e2: obscure glass albedo .36 -> .10 (reads as dark glass in shade), plus a close view of the VEXO section. closer: yes

### Item 5: end wall (ref: Truxton, Highsmith PD; its stone end wall with meter, lamp and quoined corners)

Blender view `endwall`, eye (14.5,1.5,3.5) -> (8,1.7,-2.5), 55 deg.

- iter 0: old rear-corner camera: east end wall is a blank stucco plane over a heavy 0.32 m dark plinth.
- iter 1: camera moved to the reference's front-corner framing; end-wall plinth 0.32 m dark -> 0.20 m light concrete. closer: yes
- iter 2: socket meter + conduit to the disconnect, jelly-jar lamp near the front corner, hose bib (the reference's end-wall clutter). closer: yes
- iter 3: alternating stucco quoins (0.42/0.28 m, 0.23 m courses) on both corners of the end walls (the reference's stone corners in the motel's stucco). closer: yes
- engine e1: eye (12.2,1.5,3.5) -> (8,1.7,-2.5), 60 deg: quoins, meter and lamp read; plinth is subtle. closer: yes

### Item 6: roofline (ref: Motel Safari, Tucumcari, Tony Hisgett, CC BY 2.0)

Blender view `roof`, eye (-2,1.6,-20) -> (-2,2.7,-4), 38 deg (ground-level elevation like the reference).

- iter 0: elevated 3/4 view: four swamp coolers on stands, two TV masts, roof laps/patches, old condensing units and vent sets; the roadside sign fills the frame and the view does not match the reference's street-level roofline.
- iter 1: camera matched (ground level, behind the row); added per-room plumbing vent stacks with boots, exhaust-fan caps and J-vents. closer: no (all of it hidden behind the parapet)
- iter 2: vent stacks 0.42 -> 0.75 m (reference stacks stand well proud of the roof). closer: yes
- iter 3: stacks black iron -> galvanized, 90 -> 110 mm (light stacks like the reference). closer: yes
- engine e1: same camera + the old ext7 roofline view: coolers, stacks, J-vents and antennas break the roofline; awning very bright from above.
- engine e2: corrugated sheet albedo .42 -> .34 (the bloom on the awning calms down, corrugation still reads). closer: yes
