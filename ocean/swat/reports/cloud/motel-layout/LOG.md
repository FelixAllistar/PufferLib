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
