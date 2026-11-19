# Interior props v2 — hill-climb log (swat/art-props-interior)

One numbered line per iteration: "prop iter N: change; closer: yes/no".
Blender previews are Cycles CPU (48 spp + denoiser, 960x540) from a camera
matched to the reference photo; in-game iterations use
`build/swat/props_interior_capture` (`make -C ocean/swat props-interior-capture`).
Per-iteration side-by-side sheets live outside git; the committed contact
sheets (reference | first render | final in-game) are `<prop>_sheet.jpg`.

Props were worked by the parent session (drapes, heater, ashtray, all in-game
iterations and placement) and three parallel workers (Blender loops of the
other ten); per-prop provenance notes are in `log/<prop>.prov.md`.

## drapes (pinch-pleat stack drapes on a ceiling traverse track)
Reference: John Margolies, "Fallsview Room 293, $48 day, $296 wk, Ellenville, New York" (LOC LCCN2017713322), public domain — https://commons.wikimedia.org/wiki/File:Fallsview_Room_293,_$48_day,_$296_wk,_Ellenville,_New_York_LCCN2017713322.tif (committed: refs/drapes.jpg). Camera matched: eye height ~1.45 m, ~5 m back, 40° crop of the window wall; light from the window.
- drapes iter 1: two floor-length stacks (7 + 4 folds, 5.5 cm folds), Poly Haven floral_jacquard recoloured rust/brown; closer: baseline (too dark and red, folds too fine and regular)
- drapes iter 2: damask lighter/more orange, 5 + 3 wider folds, 7 cm deep, context wall fixed, ref cropped to the window; closer: yes
- drapes iter 3: rounder fold profile, 9 cm depth, damask repeat 0.32 -> 0.5 m (motif size of the ref paisley); closer: yes (marginal)
- drapes iter 4: lighting matched to the ref (key light from the window, weak room fill) + folds drift along the length; closer: yes
- drapes iter 5: broad crests/narrow creases (|sin|^0.55), 11 cm, wobbling outline; closer: no (outline blobby) -> wobble reverted
- drapes iter 6: outline wobble 1.2 cm -> 3 mm, keep deep creases; closer: yes
- drapes iter 7: paler peach-gold damask (value of the ref); closer: yes
- drapes iter 8: 13 cm folds + 0.75 m pattern repeat; closer: no (pattern streaks) -> reverted
- drapes iter 9: hem flare 5 -> 12 cm (stack kicks out at the carpet like the ref); closer: yes
- drapes iter 10: flare anchored on the corner edge so the narrow stack never enters the party wall; mirrored for yaw = pi mount; closer: same look (fit fix)
- drapes in-game 1: placed on the four facades; read too saturated against the dim yellow rooms -> damask darkened/desaturated; closer: yes
- drapes in-game 2: second colourway (avocado) for 102/104 so rooms differ; gold stays in 101/103; closer: yes
- drapes in-game 3: route check (tools/props_interior_clearance.py over 7 recorded squad/hostile runs) showed officers posting beside the entrance 0.4-0.5 m off the facade, 0.13 m from the wide stack -> stack narrowed to 0.20 m (+hem flare) and folds flattened to 0.12 m deep; all tracks clear at a 0.25 m body radius; still reads as stacked drapes; closer: same look, route-safe
Remaining: folds are smooth tubes, no pinch-pleat header detail at the track; no sheers (the window's own drawn curtain fills the glass).

## wall_heater (electric baseboard heater, enamelled steel)
Reference: HABS CA-2139 photo 11 (LOC https://www.loc.gov/pictures/item/ca2139.photos.315142p/), "Interior of living room showing back wall electrical heater", public domain (committed: refs/heater.jpg). Camera matched to a crop of the heater: eye ~0.95 m, 1.7 m back, 45°.
- heater iter 1: 1.1 m housing, sloped top, tall rounded end caps, thermostat knob, cream enamel with dust map; closer: baseline (wide framing; caps too tall, louvre band missing)
- heater iter 2: sloped upper face became the dark discharge grille with enamel fins, camera cropped to the heater; closer: yes
- heater iter 3: mounted 7 cm off the carpet like the ref (visible wall below), body 0.20 m, flush caps, dark fins; closer: yes
- heater iter 4: top fins removed, grille band slimmer (0.80 H to top), fewer tiny fins; closer: yes
- heater iter 5: recessed dark intake band on the lower front (the ref's dark lower stripe); closer: yes
- heater iter 6: inset grey end caps; closer: no (caps read as separate parts) -> reverted
- heater iter 7: enamel darker than the wall (value of the ref); closer: yes
- heater in-game 1: under each guest window between the drape stacks; in the window's shadow it read almost black -> enamel lightened to warm cream; reads as a baseboard heater at the right height; closer: yes
Remaining: no visible power cord/junction; fins are only suggested.

## ashtray (stamped gold-anodised aluminium motel ashtray)
Reference: "Blue Bird Motel and Restaurant Ashtray - Valparaiso, Indiana" by Shook Photos, Flickr, CC BY 2.0 — https://www.flickr.com/photos/24724221@N07/32572608418 (found via Openverse; committed downscaled: refs/ashtray.jpg, credit in README). Our print is abstract glyph blocks: no real names, text or logos. Camera matched: straight top-down, 22°, key light upper left.
- ashtray iter 1: 13.4 cm pressed dish (lathe), two ribbed rests, two butts, printed arcs, ash stains; closer: baseline (too dark/orange, chunky glyphs, blotchy ash, short rests)
- ashtray iter 2: brighter brass-gold, half-metal, fine glyph rows, sparse ash, longer rests; closer: yes
- ashtray iter 3: slightly darker olive-gold, rests shortened toward the ref proportion, grazing key light; closer: yes
- ashtray iter 4: steep pressed bowl wall (8 mm step) so the bowl edge reads like the ref; closer: yes
- ashtray iter 5: rests rotated 10°; closer: no (ref rests are mirror-symmetric, not rotated) -> reverted
- ashtray iter 6: longer print arcs; closer: no (no visible gain) -> stop after two non-improvements
- ashtray in-game 1: desks of 101/103 (free corner by the wastebasket), reception counter beside the phone, laundry desk; reads as a small gold tin ashtray with butts at desk distance; closer: same
Remaining: print is abstract; butts are simple cylinders.

## dresser
dresser iter 1: first build (walnut carcass and top, 2x3 drawers with chrome bars, tapered legs, framed wall mirror 0.80x0.68 at 0.86 m); camera too close; closer: n/a (baseline)
dresser iter 2: mirror made taller and lowered to sit on the dresser top (0.80x0.80 from 0.80 m) like the photo; camera pulled back along the wall; closer: yes
dresser iter 3: veneer darkened and glossier (photo's lacquered walnut), darker carcass; closer: yes
dresser iter 4: taller slimmer legs (17.5 cm, 10-17 mm taper), thinner 20 mm top, darker and wider 6 cm mirror frame; closer: yes (slight)
dresser iter 5: camera further back to show the legs; now the end panel dominated, unlike the photo; closer: no (reverted)
dresser iter 6: camera moved out into the room so the drawer front is seen obliquely as in the photo; closer: yes
dresser iter 7: drawer fronts flush with the carcass, small chrome bar pulls high on each drawer, glossier top; closer: yes
dresser iter 8: mirror glass darker/rougher (reads like the photo's dim reflection, closer to the engine's non-reflective mirror); closer: yes (slight)
dresser iter 9: mirror 0.92 m tall (photo's tall mirror over a low dresser); closer: yes
dresser iter 10: more saturated orange veneer; closer: no (reverted)
dresser iter 11: darker brown veneer on the drawer fronts/top; closer: yes (slight)
dresser iter 12: splayed legs (6 deg); no visible change; closer: no (reverted)
dresser iter 13: darker carcass so drawer gaps read darker; no visible change; closer: no -> stop, final = iter 11
- dresser in-game 1: east wall beside the bed (final: pillow end in 101/102 after the route check, mid-bed in 103/104) (dead 0.53 m strip, clear of the luggage rack and the 104 nightstand); the metallic mirror picked up the bright sky probe and read as a flat white panel -> baked dim smoky reflection with a diagonal sheen and desilvered edges, non-metal; closer: yes

## mini_fridge
mini_fridge iter 1: first build (woodgrain carcass, laminate top, door, black plinth vent); camera on the wrong side (side face dominant); closer: n/a (baseline)
mini_fridge iter 2: camera moved to the photo's front-right high viewpoint, veneer tinted redder; closer: yes
mini_fridge iter 3: camera higher/closer, warmer mahogany veneer and darker top; closer: yes
mini_fridge iter 4: camera more frontal so the door face dominates as in the photo; closer: yes
mini_fridge iter 5: veneer darkened toward the photo's deep red-brown; closer: yes (slight)
mini_fridge iter 6: door inset between cabinet stiles with a vertical hinge-side seam, chrome maker's plate on the right side; closer: yes
mini_fridge iter 7: two worn paper stickers on the door (as in the photo); closer: yes
mini_fridge iter 8: deeper red veneer, thicker 30 mm top; closer: yes (marginal)
mini_fridge iter 9: vinyl edge band on the top read as silver trim not in the photo; closer: no (reverted)
mini_fridge iter 10: steeper top-down camera; closer: no (reverted) -> stop, final = iter 8
mini_fridge iter 11: rounded (3-segment bevel) cabinet, top and door edges to reach the 500-tri floor (732 tris); look unchanged vs iter 8; closer: same (kept as final)
- mini_fridge in-game 1: west-wall nook between the entrance door's bumper (z -0.95) and the desk (z -1.74); first 102/103/104, final 101/104 (an occupant stands in 102's nook, an officer brushes 103's); reads as a woodgrain under-counter fridge next to the desk, no change needed; closer: same

## coffee_maker
coffee_maker iter 1: first build (rear tower, lid, cone basket, carafe); camera far too close, 9 materials (over the 7 limit); closer: n/a (baseline)
coffee_maker iter 2: camera pulled back to the reference 3/4 framing at 4:3; grille/plate merged into shell/dark -> 7 materials; closer: yes
coffee_maker iter 3: silhouette: reservoir moved from the rear to a LEFT column (as in the reference), lid overhangs the carafe; closer: yes
coffee_maker iter 4: carafe wider (r 7.5 cm) and forward, column narrower (48 mm), camera more frontal; closer: yes
coffee_maker iter 5: lid thinner (38 mm), taller/wider dark filter basket now visible under the lid, darker glass; closer: yes
coffee_maker iter 6: lighter glass + taller coffee volume; coffee hidden inside opaque glass, read as a dark stripe; closer: no (reverted)
coffee_maker iter 7: carafe split into coffee-filled lower body + clear upper glass (opaque, engine-friendly); closer: yes
coffee_maker iter 8: darker upper glass, camera raised to show the lid top like the photo; closer: yes
coffee_maker iter 9: front tab of the basket thinned so the cone's sloped sides read; closer: yes (slight)
coffee_maker iter 10: taller basket (9.8 cm) + darker band; carafe got shorter; closer: no (basket reverted)
coffee_maker iter 11: darker band only; closer: no (no visible change) -> stop
- coffee_maker in-game 1: on top of the fridge (final 101 and 104) (yawed differently); reads as a white drip coffee maker with carafe from the room cameras, no change needed; closer: same

## closet_rod
closet_rod iter 1: first build (shelf, two end plates, mottled rod, plastic hanger, 4 wire hangers, shirt on a hanger); camera too close; closer: n/a (baseline)
closet_rod iter 2: camera pulled back to the photo's frontal framing (1.9 m, 38 deg), finer wallpaper stripes in the preview shell; closer: yes
closet_rod iter 3: rod raised to 1.71 m and thickened to 34 mm so it sits just under the shelf as in the photo; lighter shirt; closer: yes
closet_rod iter 4: end plates cut down to small rod brackets (photo), rod texture darker mottled with light specks; closer: yes
closet_rod iter 5: hangers yawed 50-70 deg; they turned face-on, unlike the photo; closer: no (reverted)
closet_rod iter 6: plastic hanger nearly edge-on (-14 deg), wire bundle 14-35 deg; closer: yes
closet_rod iter 7: plastic hanger body thicker (15 mm) and whiter; closer: yes (slight). Reference upgraded to the LOC 1024 px scan
closet_rod iter 8: camera moved left and lower like the photo (shelf runs in perspective), shelf 13 mm thin; closer: yes
closet_rod iter 9: plaid shirt texture + cloth folds; still reads as a slab, darker; closer: no (reverted)
closet_rod iter 10: pale shirt, softer shoulders; closer: no (reverted)
closet_rod iter 11: shorter tapered shirt; closer: no -> stop, final = iter 8
- closet_rod in-game 1: east wall over the luggage rack (first in 101/102; moved to 102/103 after the route check: the player clears 101's strip south of the rack) (the usual motel arrangement); the pale blue shirt glowed in the dim room -> brown check flannel; closer: yes

## waiting_chairs
waiting_chairs iter 1: first build (two curved seat/back sheets solidified 3.5 cm, square chrome beam on two T-legs, Poly Haven fabric_leather_02 vinyl); vinyl read brown, shells square-cornered, beam too high under black brackets; closer: n/a (baseline)
waiting_chairs iter 2: vinyl graded to faded avocado (desaturated leather luminance x green), round chrome beam just under the seats like the reference; corner rounding attempt pinched the back corners into ears; closer: yes (colour/beam), corners wrong
waiting_chairs iter 3: corners rounded properly on the unrolled sheet (arc-length profile, 10 cm top / 5 cm front radii); finer vinyl grain; closer: yes
waiting_chairs iter 4: camera raised to the reference's slightly-above seat view; vinyl map 256 px (GLB 600 -> 280 KB); closer: yes
waiting_chairs iter 5: rolled rim/piping tube around each shell's edge (the reference's bright outline); 9.7k tris, over budget; closer: yes
waiting_chairs iter 6: grid 12x28 and 6-sided rim (5.4k tris), shells 47 cm wide with a gap between seats, round tube floor feet; closer: yes
waiting_chairs iter 7: thicker front roll (5 cm radius) as in the reference; closer: yes (slight) - final (remaining: reference is perforated painted steel; this is upholstered vinyl for the era; legs read a little tall)
- waiting_chairs in-game 1: east wall of the reception lobby under the clock, facing the counter; the lime vinyl popped out of the dim yellow room -> darker, greyer avocado; closer: yes

## potted_plant
potted_plant iter 1: first build (lathe terracotta pot + dark saucer + soil, 19 baked-texture sword blades, double-sided strips); blades narrow, too upright and clustered, broken UVs drew yellow triangles; closer: n/a (baseline)
potted_plant iter 2: blades rebuilt with per-vertex u/v (margin stripe fixed), lower blades lean/arch outward, pot more bulbous with a small rolled lip; spread far too wide (0.7 m); closer: yes
potted_plant iter 3: lean reduced, blades wider (9-11.5 cm), darker mottled cross-banding and wider yellow margins; closer: yes (slight)
potted_plant iter 4: blades fan mostly sideways like the reference instead of toward/away from the wall; but seen edge-on, and 0.74 m wide; closer: yes (silhouette)
potted_plant iter 5: blades rolled so their flat faces turn toward the viewer as they lean; lean capped (0.55 x 0.40 m footprint); closer: yes
potted_plant iter 6: darker, more saturated leaf green; shallow throwing rings on the pot; closer: yes
potted_plant iter 7: four short blades at the rim, rings softened; closer: yes (slight) - final (remaining: reference blades are more twisted and glossy; leaf tips span 0.55 m wide, the pot 0.37 m)
- potted_plant in-game 1: placed in the reception lobby (see motel_props_interior_v2_data.h); reads at the intended scale from the entrance and counter cameras, no change needed; closer: same

## water_cooler
water_cooler iter 1: first build (enamel box, black recess with grille, tall ribbed bottle 0.52 m, side cup dispenser); far from ref: framing too tight, bottle washed out, recess reads as a black box; closer: n/a (baseline)
water_cooler iter 2: rebuilt cabinet around a tall open front recess with two push faucets (blue cold knob), camera pulled back to the reference's low right-front view; closer: yes
water_cooler iter 3: bottle shorter (0.47 m) and wider (r 0.148), four flat ribs, darker blue; closer: yes
water_cooler iter 4: removed bevels that cut visible seams, recess moved up (0.54-0.89 m), nameplate moved into the recess, bottle shoulder seated on the top; closer: yes
water_cooler iter 5: body UVs from fit to metric box mapping (grime band had landed on every shell piece), bottle darker; closer: no (dark band remained) - kept, cause was geometry
water_cooler iter 6: lower block no longer overlaps the recess columns (coplanar overlap rendered as a black band); cabinet now reads as one shell; closer: yes
water_cooler iter 7: rounded top cap with 4 mm overhang, warmer cream enamel, glossier bottle; closer: yes (slight)
water_cooler iter 8: bottle ribs flattened (narrow grooves between flat bands, like the reference); closer: yes
water_cooler iter 9: faucets raised to the upper third of the recess; closer: yes (slight)
water_cooler iter 10: rounded upper recess corners (4 cm fillet prisms); closer: yes - final (remaining: opaque bottle cannot show the reference's transparency; the reference is a later plastic cooler, the side paper-cup dispenser is an era addition)
- water_cooler in-game 1: placed in the reception lobby (see motel_props_interior_v2_data.h); reads at the intended scale from the entrance and counter cameras, no change needed; closer: same

## brochure_rack
brochure_rack iter 1: first build (oak veneer back/sides, 4 tiers x 4 slots, leaning card stacks with a baked cover atlas); cards pivoted about the wrong point and floated forward, camera too close; closer: n/a (baseline)
brochure_rack iter 2: fixed card lean pivot (stacks rest on each tier behind the lip), camera pulled back to the reference's near-frontal view; closer: yes
brochure_rack iter 3: covers get grey "body copy" lines and pastel stock tints, wood warmer; camera framed on the whole rack; closer: yes
brochure_rack iter 4: dark headline bars on light stock (rack-card look), smaller sun motif, more saturated cherry/orange wood like the reference; closer: yes
brochure_rack iter 5: thinner front lips (4 cm), fuller slots (one empty), thicker stacks and wider cards so the tiers read full; closer: yes
brochure_rack iter 6: diagonal two-tone covers replaced by light stock with a photo panel; closer: no (too white versus the reference's coloured stock) - kept the panel, fixed the tint next
brochure_rack iter 7: 80% of cards on coloured stock (pink, teal, yellow, blue washes); closer: yes - final (remaining: reference lips are rounded and the cards show finer type; all graphics are made-up, no real text)
- brochure_rack in-game 1: placed in the reception lobby (see motel_props_interior_v2_data.h); reads at the intended scale from the entrance and counter cameras, no change needed; closer: same

## laundry_cart
laundry_cart iter 0: first build — grey canvas bin in a box frame, blue towel over the rim; generic 3/4 camera
laundry_cart iter 1: tapered bin (narrow at the floor), laden canvas bulging out, flat X straps on all four faces; camera to portrait framing; closer: yes
laundry_cart iter 2: camera end-on like the reference; canvas hem folded over the rim; closer: yes
laundry_cart iter 3: canvas regraded from grey to cream duck, towel replaced by a white sheet, wider FOV; closer: yes
laundry_cart iter 4: thinner hem, darker galvanised frame, linen heap raised well above the rim; closer: yes
laundry_cart iter 5: fine vertical creases + baked grime + lower bin floor/smaller casters; creases alias into a quilted pattern; closer: no (creases reverted, grime/floor kept)
laundry_cart iter 6: broad slack folds instead; diamond artefacts from coincident double-sided faces; closer: no
laundry_cart iter 7: canvas given a real inner face 4 mm inside instead of a coincident reversed copy; clean soft folds; closer: yes
laundry_cart iter 8: three folded sheets thrown on the heap (soft slabs with drooping edges); closer: yes
laundry_cart iter 9: padded hem removed, canvas laced under a thin steel rim with grommets; closer: yes
laundry_cart iter 10: frame darker and duller (painted grey steel); closer: yes
laundry_cart iter 11: canvas warmer/yellower; reads too yellow; closer: no (reverted)
laundry_cart iter 12: thinner posts and X straps; no visible change at this distance; closer: no (reverted) — stop (two in a row)
- laundry_cart in-game 1: against the laundry east wall north of the service opening (z -6.37..-7.15, opening z -7.37..-8.63 clear), 3 cm off the wall for the canvas bulge; canvas reads as a pale bin from the doorway; closer: yes

## linen_shelf
linen_shelf iter 0: first build — open plywood shelving, 5 boards, towels, sheets, cartons, jug; CC0 terry map came out blue; camera too close
linen_shelf iter 1: camera matched to the HABS bay (frontal, slightly right, ~1.2 m eye); terry map desaturated to white towels; closer: yes
linen_shelf iter 2: plywood regraded darker/greyer (B&W value match), thicker 25 mm boards; closer: yes
linen_shelf iter 3: towels softened (rounded folds, bulging fronts, sag, size jitter); cartons darker; closer: yes
linen_shelf iter 4: baked blotchy grime and scuffs over the plywood; cartons kraft-brown; closer: yes
linen_shelf iter 5: face frame (stiles + deep top rail) like the built-in linen-room unit; shelf boards sag at mid-span; closer: yes
linen_shelf iter 6: towels greyer/higher-contrast weave; no visible change, GLB +100 KB; closer: no (reverted)
linen_shelf iter 7: baked maps embedded as JPEG instead of PNG (1.33 MB -> 0.69 MB); look unchanged; closer: no — stop (two in a row)
- linen_shelf in-game 1: placed in the laundry nook between the north wall and the desk (west wall, facing east); the nook is 0.80 m so the unit is built 0.76 m wide (x scaled), clearing the wall-mounted extinguisher; reads as a stocked linen store from both laundry cameras; closer: yes
