# Night mode previews

Rendered headless under software GL (llvmpipe, `xvfb-run -a`) at 1600x900 with
`SWAT_TIME=night`, motel mission, layout seed 42, through the production
cinematic path (shadows, bloom, grade, fog). Exterior views use
`--lights-off 0x8`: Room 103's lamp is switched off through the authoritative
room switch, so its window reads dark next to lit Room 102.

| File | Tool / view |
|---|---|
| `1_street_approach.jpg` | `surroundings_capture --shot 1` (street) |
| `2_parking_lot.jpg` | `surroundings_capture --shot 2` (parking) |
| `3_porch_walkway.jpg` | `exterior_capture --view 2` (gallery) |
| `4_lit_and_dark_windows.jpg` | `exterior_capture --view 3` (Room 102 lit, 103 switched off) |
| `5_room102_interior.jpg` | `interior_capture --shot 0` (Room 102, lamp-lit) |
| `6_office.jpg` | `exterior_capture --view 4` (office glazing, Room 101) |
| `7_sign.jpg` | `exterior_capture --view 0` (roadside sign, VACANCY face) |
| `8_rear_wallpacks.jpg` | `exterior_capture --view 5` (rear wall packs) |
