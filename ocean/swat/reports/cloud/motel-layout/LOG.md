# Briar Court layout pass: hill-climb log

Plan and record: `ocean/swat/MOTEL_LAYOUT.md`. Branch `swat/motel-layout`.

## References

All committed copies in `refs/` are US government works (Historic American
Buildings Survey / Historic American Engineering Record, Library of Congress
Prints & Photographs), public domain, downscaled.

| File | What | Source | Author | License |
| --- | --- | --- | --- | --- |
| refs/ref_siteplan_luna_lodge_haer_nm13_pd.jpg | Luna Lodge existing site plan (sheet 4) | https://www.loc.gov/item/nm0300/ | HAER NM-13 (NPS, delineated 2006) | Public domain |
| refs/ref_axo_luna_lodge_haer_nm13_pd.jpg | Luna Lodge in 2006, axonometric (sheet 3) | same | HAER NM-13 | Public domain |
| refs/ref_evolution_luna_lodge_haer_nm13_pd.jpg | Evolution 1949-1954 (sheet 2) | same | HAER NM-13 | Public domain |
| refs/ref_court_luna_lodge_haer_nm13_photo19_pd.jpg | Court view along a wing (photo 19) | same | HAER NM-13 | Public domain |
| refs/ref_street_luna_lodge_haer_nm13_photo13_pd.jpg | From Central Avenue, sign at the street (photo 13) | same | HAER NM-13 | Public domain |
| refs/ref_siteplan_lillys_auto_camp_habs_ca2094_pd.jpg | Lilly's Auto Camp site plan (sheet 1) | https://www.loc.gov/item/ca0920/ | HABS CA-2094 | Public domain |

Scale of the Luna plan: the west wing dimension 251'-2" (76.6 m) spans about
415 px on the 1024 px sheet, 5.42 px/m; `tools/motel_plan_compare.sh` rotates
the plan so its street (East Central Ave) is at the bottom and rescales it to
the 8 px/m of `tools/motel_site_plan.py`.

## Plan hill-climb (top-down, 8 px/m, reference | plan)

Sheets: `plan_iter1.jpg` .. `plan_iter6.jpg`, `plan_iter7_8_rejected.jpg`,
summary `plan_sheet_ref_before_after.jpg` (Luna | 5.0 map | plan iter 6).

Before (5.0 map): a 20 m strip (office + 4 rooms) in a 48 x 48 m asphalt
square, 34 m from the road; 3 biggest differences to Luna: (1) massing: one
short strip in an empty plain vs. wings wrapping a court; (2) circulation and
parking: no defined aisles, 4 m / 3 m stalls scattered over the plain vs. one
row along each wing and a central island; (3) street: the court and sign
should meet the street, here a 34 m apron.

- iter 1: U court: rear wing extended east by 2 closed bays (105-106), east
  and west wings of 5 closed bays (z 1.83..21.83) facing the court; 2.7 x 5.5
  stalls (7 along the rear wing centred on the parked sedan, 5 per side), 7 m
  aisles, central island, corner planters, sign moved to the driveway. Closer:
  yes (reads as a court). Differences: (1) wings stop 14 m short of the road and
  the court is square (28 x 20 m) where Luna's is long; (2) big asphalt apron
  behind and beside the building; (3) entrance is a 6 m driveway, not an open court mouth.
- iter 2: side wings 5 -> 6 bays (to z 25.83, past the lot edge): the arms of
  the U reach the lot's street edge. Closer: yes. Differences: (1) rear asphalt
  apron (z -24..-10) still an empty plain; (2) court proportions; (3) side rows.
- iter 3: rear yard beyond a 7 m service drive and the strips behind the
  wings painted as gravel (the paved area becomes court + service drive).
  Closer: yes (the building now sits on its lot). Differences: (1) the island is
  short and off the court axis; (2) court mouth; (3) office not at the street.
- iter 4: island 2.4 x 8.3 m on the court axis from the cross aisle to the
  mouth, splitting in and out like Luna's concrete island; side rows trimmed to
  5 stalls so none sits past the lot edge. Closer: yes (marginal).
- iter 5: sign from the driveway (z 27) to the street edge (z 31.6), facing
  the road, like Luna's sign at Central Avenue. Closer: yes (small).
- iter 6: west wing one bay longer toward the rear (z -2.17), closing the
  west corner against the office line like Luna's wing-to-rear-wing corner; the
  vending nook stays open between them. 19 guest rooms. Closer: yes (marginal).
- iter 7: rear row widened into the corner planters (11 stalls). Not closer:
  the corner stalls cannot back out (side rows block them) and Luna has no dead
  stalls. Reverted.
- iter 8: wings 1 m closer each (court 26.3 m, nearer Luna's 24 m). Not
  closer in a usable way: the aisles drop to 6 m (< 7 m) and the side rows
  run under the walkways. Reverted. Two non-improving iterations: stop.

Result: plan iter 6 (`tools/motel_court_spec.py`).

## Plan -> engine (gameplay adjustments, not hill-climb iterations)

Building the plan into the authoritative map and running the mission tests
moved four things away from plan iteration 6; each is recorded because it
changed the site:

- Room 106 dropped from the rear wing: staging had moved 8.5 m east (to the
  far side of 106) and every squad route grew by ~1000 ticks. The breezeway now
  sits right beyond Room 105 (x 12..17) and staging (14.1, -3) is 3.6 m east
  of the 5.0 staging.
- The west wing's rear-most bay (iteration 6) dropped: its post line crossed
  the vending-nook route west of the office (test_motel's capsule routes).
- The east wing moved 1 m east (x 17..23) so the four squad spawns at staging
  clear Room 105's pier and the wing's corner post (net-conditions harness).
- Extraction (0, 10) -> (0, 12): the evacuees and squad crowding the old spot
  boxed the commander in (motel completion, squad search).

## Street-level hill-climb (reference | capture, `street_iter*.jpg`, `court_iter*.jpg`)

Street view: Luna Lodge HAER NM-13 photo 13 (from across Central Avenue);
engine camera `exterior_capture --eye -14,1.6,46.5 --target 2,2.4,22 --fov 55`
(far side of the road). Court view: HAER NM-13 photo 19 (down a wing's
walkway toward the rear wing); engine camera `--eye -10.6,1.55,27 --target
-15.2,1.3,3 --fov 52`.

- street iter 0 (court built, wings to z 25.6): differences: (1) the wings
  stand ~10 m behind a sand verge where Luna's buildings meet the sidewalk;
  (2) Luna's sign is a tall pole sign perpendicular to the road at the kerb,
  ours is short, parallel and set back; (3) verge palms screen the wing ends.
- street iter 1: sign to the kerb (6.4, 33.0), perpendicular to the road,
  x1.35; its face is single-sided, so a back-to-back twin (court row) reads
  from both directions. Closer: yes.
- street iter 2: both side wings one bay longer (7 bays, to z 29.6, 5 m from
  the kerb): 19 guest rooms. Closer: yes (marginal from this camera; the east
  wing end now stands at the street).
- street iter 3: the two verge palms that screened the wing ends moved to the
  lot corners. Closer: yes (the court opens to the street like Luna's, rear
  wing visible across it).
- court iter 4 (the same build, court camera): differences: (1) a parked
  wagon fills the foreground where Luna's court is open asphalt with the car at
  the far end; (2) Luna's walkway is punctuated by junipers between units, ours
  by evenly spaced posts; (3) the corrugated awning reads bright white in sun.
  No change made in this iteration (baseline for the court camera).
- court iter 5: wagon to the rear-most west stall. Closer: yes (open
  foreground, car at the far end by the rear wing).
- Not changed: junipers (no CC0 shrub in the kit; presentation-only planting
  on the walkway would be walked through) and the awning brightness (tuned by
  the architecture pass against its own references).
