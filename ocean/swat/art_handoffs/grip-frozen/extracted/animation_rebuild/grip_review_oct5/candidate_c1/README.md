# C1: bounded index-base release from sealed C

C1 changes exactly four scalar quaternion curves on `mixamorig:LeftHandIndex1`. It reduces C's added native-local X flexion from +4.451142948549993° to 0°, keeping the added native-local Z spread at +0.42138296301187816°. Index2/3/4 retain their exact C local channels and follow the changed parent. Hand, arm, gear, rifle, right grip and every other digit retain C's exact channels and world matrices.

- Editable: `support_grip_index_release_c1.editable.blend`
- SHA-256: `17c1c13ffbea9d8fac459914eb2bbb1c6c336b785f9382450002f0ad6a53ac99`
- Action: `Support Grip / Index Base Release Candidate C1`, frame 0
- Action-only library: `support_grip_index_release_c1.actions.blend`
- Library SHA-256: `89bb9ada5f14941b0dc0a12262ce9111a9bdee8f176193b70ce3d1d706421c0a`
- Immutable source C SHA-256: `f1b39d78b32da500f9fd4370d942d364c49a1d9d788ac1122282b4275e371dcb`

## Exact source handoff

`EXACT_LEFT_GRIP_HANDOFF.json` provides all 24 desired native-bone local records and the 240 exact scalar replacement curves relative to frozen F. It separately provides the exact four-channel C→C1 replacement. `EXACT_INDEX_RELEASE_HANDOFF.json` isolates that change and verifies the 716 untouched C curves, unchanged other local matrices, unchanged hand/arm/gear/right and other-digit world matrices, and unchanged evaluated rifle vertices. All those preservation errors are exactly zero.

Native Rifle 7 mesh coordinates and the established stock-origin physical frame remain explicitly distinct. The hand matrix is exactly unchanged from corrected C. Its stock-origin physical wrist position is (+0.494427394, −0.027409224, −0.053811514) m, using +X forward, +Y up, +Z right. Native mesh wrist coordinates are (+0.055108426, −0.044427406, −0.004759001) m. Do not interchange them.

## One-dimensional selection

After the sealed C backup was verified, the fixed bracket evaluated added Index1 flexion at +4.45114295° (C reference), +3°, +1.5° and 0°. No whole-hand optimization or other channel adjustment was performed. Only the chosen 0° derivative was saved. All bracket evidence is in `BRACKET_EVIDENCE.json`; exact channel changes and selection are in `CANDIDATE_C1_AUTHORING.json`.

| Added Index1 flexion | Crease area, mm² | Dense sampled index overlap, mm | Index opposed area within 2 mm, mm² |
|---:|---:|---:|---:|
| C: +4.451° | 2.560 | 0.698 | 261.80 |
| +3° | 3.711 | 0.0069 | 126.10 |
| +1.5° | 4.914 | 0 | 34.30 |
| C1: 0° | 6.124 | 0 | 0 |

The target palmar index-base triangle [8513,8515,8516] restores 99.23% of F's 6.171 mm² area. Its area/rest ratio rises from 0.06673 in C to 0.15963 in C1, close to F's 0.16087. Normal-transport alignment improves +0.9385→+0.9822. No left-glove triangle remains below 10% of rest area.

Central 50–75% palm median gap remains 5.548 mm, compared with C's 5.553 mm and F's 10.561 mm. Opposed central area within 2 mm rises from C's 62.01 to C1's 75.58 mm². Total fine-sampled palm area within 2 mm is 486.50 mm².

## Tradeoff and limits

Releasing Index1 reduces its contact pressure: its nearest sampled pad gap is 2.004 mm, with no opposed index area inside the strict 2 mm threshold. About 36.22 mm² remains within 3 mm. The index remains visibly wrapped but slightly more open. Its middle/distal skin has approximately 0.774 mm minimum bidirectionally sampled separation from the middle finger's middle/distal skin, versus C's 0.363 mm. Nonlocal glove self-crossings remain zero.

The whole-candidate dense review reports maximum overlap 0.825 mm, unchanged from C and located in the thumb, with no sample deeper than 1 mm. Left cap/arm crossing count is zero; the same pre-existing ring-crease normal folds remain. This is a shallow-contact static result, not a claim of zero intersections or motion robustness.

The four exact F-camera views are in `views/`. Their pixels were inspected; C1 retains C's tighter palm cup and changes the index subtly. The near-visor camera remains uncalibrated. Independent combined disposition is under `../candidate_c1_review/` and remains authoritative.

No native geometry, rests, weights, C file, original F file, R5 source, animation bank or engine export was edited. No camera, ADS shoulder, transition or armIK approval follows from this source-only derivative.
