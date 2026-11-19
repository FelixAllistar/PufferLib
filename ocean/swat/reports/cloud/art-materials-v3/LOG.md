# Materials v3 hill-climb log

Loop per item: Blender stand-in (tools/blender/materials_v3_preview.py, view in
materials_v3_views.json) rendered beside the reference, the three biggest
differences noted (silhouette/proportion, then material/colour/value, then
detail/wear), the biggest fixed, re-rendered; non-improving steps reverted.
Then the same comparison in the engine (`make -C ocean/swat materials-capture`,
`xvfb-run -a build/swat/materials_capture OUT NAME:eye:target:fov`).
Parameters live in tools/blender/materials_v3_recipe.json (bake) and
materials_v3.h (tile, macro). References and licences: see README in
assets/environment/materials_v3/.

## 1. Exterior walls (stucco, repair patch)
Reference: "Abandoned Motel 3, Marathon, Texas", TexasExplorer98,
https://www.flickr.com/photos/76792851@N07/15512418618 (CC BY 2.0). A
single-storey painted-stucco strip motel, rear wall, semi-desert verge.
Choice: painted stucco (what the reference has; matches the existing walls).

- iter 1: white_stucco @2 m, target (196,190,176). Diffs: 1 camera sees the front with large openings, ref is the long rear wall; 2 wall far too white/clean; 3 no weathering. Fix camera. closer: n/a (baseline)
- iter 2: camera moved to the rear wall, AgX exposure -0.6. Diffs: 1 wall recedes the wrong way; 2 wall reads flat white paint, ref grey weathered; 3 no stains. closer: yes
- iter 3: building mirrored to recede left; stucco target (178,174,166), contrast 1.4, flatten 0.15, macro 0.14. Diffs: 1 ok; 2 still smooth uniform paint, the scan has no weathering to amplify; 3 none. closer: yes (silhouette)
- iter 4: source -> Poly Haven concrete_wall_003 (white plaster with grey weathering) @3 m. Diffs: 1 ok; 2 stains brown and too contrasty close up (the 'dirty cardboard' risk); 3 stains read large. closer: yes
- iter 5: contrast 0.7, flatten 0.45, saturation 0.55, target (182,178,170). Diffs: 2 stains now grey like the ref, slightly strong close up; 3 relief weak. closer: yes
- iter 6: contrast 0.6, normal 1.4, macro albedo 0.10. Diffs: 2 softer, matches ref's soft blotching; 3 repeat faintly visible at 3 m. closer: yes (slightly)
- iter 7: tile 3 m -> 4 m, macro frequency 0.35 -> 0.22 /m. Diffs: 2 ref value darker relative to sky; 3 ok. closer: yes
- iter 8: target (166,163,156). Diffs: 2 value now close; 3 sheen on stained areas. closer: yes
- iter 9: roughness r*0.6+0.4 (mean 0.62 -> 0.77, matte like ref), repair patch target (146,142,134) (ref patch shows darker grey). closer: no visible change (kept: physically matches)
- iter 10: contrast 0.5. closer: no -> reverted to 0.6. Two non-improving steps: stop.
- engine 1: rear/front captures matched to the ref angle. Diffs: 1 the motel rear faces away from the sun, so the comparable lit wall is the east end wall; 2 sunlit stucco blows out to white (engine exposure is brighter than the Blender AgX stand-in); 3 relief lumpy at grazing sun. Fix value.
- engine 2: stucco target (166,163,156) -> (146,142,134). Diffs: 2 end wall now reads as the ref's sunlit weathered white; 3 normal relief too strong close up. closer: yes
- engine 3: normal 1.4 -> 0.8. closer: yes (calmer close up, wide unchanged). Final.

## 2. Parking-lot asphalt
Reference (wide): "Johnson Park", dankeck, https://www.flickr.com/photos/140641142@N05/53548495024 (CC0 1.0): aged, oxidised low-traffic asphalt, eye ~0.5 m.
Reference (close): "Asphalt Texture", włodi, https://www.flickr.com/photos/19716902@N00/2329018741 (CC BY 2.0).
Base kept calm on purpose: cracks, patches, oil and tyre marks come from the existing decal pass.

- iter 1: asphalt_04 @4 m, target (112,110,106). Diffs: 1 camera 1.4 m vs ref ~0.5 m; 2 pale, smooth, reads as concrete; 3 no aggregate. closer: n/a
- iter 2: camera lowered to 0.5 m; target (100,97,92), normal 1.5. Diffs: 2 value ok but still concrete-smooth; 3 no aggregate. closer: yes
- iter 3: source -> worn_asphalt @3 m. Diffs: 2 aggregate present; 3 stones too large and contrasty (gravel-like). closer: yes
- iter 4: tile 3 -> 2 m (scan scale), contrast 0.7, saturation 0.4. Diffs: 2 ref warmer/browner grey; 3 ok. closer: yes
- iter 5: target (110,105,96). Diffs: 2 ref shows broad lighter/darker oxidation patches; ours uniform. closer: yes
- iter 6: flatten 0.6 -> 0.3, macro (0.18 @0.08/m, roughness 0.08). closer: no visible change in Blender (noise stand-in is weak); kept for the engine
- iter 7: contrast 0.9. closer: no -> reverted. Stop.
- engine 1: lot views from the road edge (eye 0.5 m), close and high. Diffs: 2 lot lighter and greyer than ref; 3 aggregate good, no tiling visible at 2 m thanks to the two-scale blend. Fix value.
- engine 2: target (94,90,82). closer: yes - dark weathered asphalt with visible aggregate; patches/oil from decals. Final.

## 3. Road (authored road, driveway, highway extension) and 4. dry ground / gravel verge
Reference: "Abandoned Motel, Marathon, Texas", TexasExplorer98,
https://www.flickr.com/photos/76792851@N07/15077859854 (CC BY 2.0): two-lane
highway, pale caliche/gravel shoulder, the motel beside it. Secondary for the
verge texture: "Abandoned Motel 3, Marathon, Texas" foreground (item 1).

- iter 1: road asphalt_02 @3 m, gravel gravelly_sand @2.5 m. Diffs: 1 road on the wrong side of frame (camera in the field, ref on the shoulder); 2 gravel flat white-grey, road blue-light; 3 no pebbles. closer: n/a
- iter 2: camera on the shoulder looking down the road; road target (78,76,73); gravel target (160,146,122), contrast 1.3. Diffs: 2 gravel lacks the ref's warm tone; road bluish; 3 road's scanned cracks repeat every 3 m. closer: yes
- iter 3: gravel target (178,160,130), saturation 1.3; road saturation 0.3. Diffs: 2 warm now; 3 verge texture too fine/uniform vs ref pebbles. closer: yes
- iter 4: gravel normal 2.0, contrast 1.6. closer: yes (slightly)
- iter 5: road source -> asphalt_04 (smooth chip seal) @4 m, target (70,68,64): no repeating cracks, reads as a highway. closer: yes
- iter 6: road target (82,80,75). closer: marginal (kept)
- iter 7: gravel tile 2.5 -> 3.5 m: pebbles at the ref's size. closer: yes
- engine 1: road, shoulder and lot-edge views. Diffs: 2 ground blown out yellow-white (the zoning dirt blend at full albedo + gravel); road good. Fix value.
- engine 2: dirt target (146,136,118) sat 0.7, gravel (168,154,128). closer: yes, still too yellow
- engine 3: dirt (140,132,118) sat 0.5, gravel (160,150,130) sat 0.75. closer: yes - pale grey caliche like the ref. Final.
