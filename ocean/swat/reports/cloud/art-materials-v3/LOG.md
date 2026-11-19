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
