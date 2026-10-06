# F integrated padding and stem proximity review

Attachment-scope conclusion: the contoured central backing visually reads as part of each cap’s continuous rigid surface in actual source-material and isolated diagnostic pixels. Central padding is close to the sleeves in neutral and the tested carry path; ADS left remains the loosest central fit. Peripheral gaps remain visible and substantial. These measurements do not establish wearer contact, fastening, support force, or a uniformly flush underside.

## Source pins

- Static source SHA-256: `4271f2279bc3cbbcb46594157752b2ac0ca052628c24b4c8629a82c6771a3fc0`
- Movement source SHA-256: `7165d1454e0526f01ca85c81f12e0245dfecb22071191918f984477a42c091b1`
- Static source and movement source were rechecked after measurements and rendering; both hashes remain unchanged. No source scene was saved. Outputs are confined to this new attachments directory.

## Static proximity

Numbers below are minimum / median / maximum unsigned nearest sleeve distance in millimeters. Central region is actual inner vertices 193–289 inclusive (160 triangles, radial fraction at most 0.5); whole underface is vertices 193–385 (352 triangles). Central area is only about 23.1% left / 23.8% right of the underface, so it cannot stand in for the entire fit.

| Pose / side | Dense central underside | Dense whole underside | Outer rim vertices |
|---|---:|---:|---:|
| N / Left | 1.537 / 2.195 / 2.638 | 1.537 / 3.565 / 14.846 | 5.235 / 8.055 / 14.846 |
| N / Right | 0.914 / 1.640 / 3.173 | 0.914 / 3.742 / 18.546 | 4.812 / 9.286 / 18.546 |
| ADS / Left | 1.711 / 4.246 / 6.888 | 1.711 / 4.954 / 17.768 | 4.589 / 11.602 / 17.768 |
| ADS / Right | 0.527 / 2.122 / 3.841 | 0.527 / 3.677 / 18.621 | 4.375 / 8.811 / 18.547 |

The 1.8 mm authoring offset is relative to the maximum sampled sleeve envelope in the carrier frame; it is not a measured uniform distance to either posed sleeve. For example, ADS-left actual central median is 4.246 mm and maximum 6.888 mm. Only 12.1% of its dense central samples are within 3 mm. Neutral left is entirely within 3 mm; neutral right is 99.8% within 3 mm.

## Bounded movement

The pinned three-second carry action was evaluated at every half-frame from 0 through 90 (181 poses at 60 Hz). The all-pose set contains central vertices plus triangle centroids (257 points), and a separate whole-underface set (545 points). Dense 0.5 mm grids were also measured at start, peak and end.

| Side | Across-path central minimum range | Across-path central median range | Across-path central maximum range |
|---|---:|---:|---:|
| Left | 1.088–1.548 | 1.668–2.211 | 1.790–2.638 |
| Right | 0.741–1.038 | 1.413–1.646 | 3.017–3.173 |

| Dense pose / side | Central underside min / median / max | Whole underside min / median / max |
|---|---:|---:|
| start / Left | 1.537 / 2.195 / 2.638 | 1.537 / 3.565 / 14.846 |
| start / Right | 0.914 / 1.640 / 3.173 | 0.914 / 3.742 / 18.546 |
| peak / Left | 1.029 / 1.664 / 1.897 | 1.029 / 2.937 / 14.443 |
| peak / Right | 0.679 / 1.409 / 3.018 | 0.679 / 3.544 / 18.328 |
| end / Left | 1.537 / 2.195 / 2.638 | 1.537 / 3.565 / 14.846 |
| end / Right | 0.914 / 1.640 / 3.173 | 0.914 / 3.742 / 18.546 |

The dense peak samples find closer points than the coarser all-pose vertex/centroid set, particularly right at 0.679 mm. These are two distinct sampling densities and should not be mixed into a purported exact global bound.

## Headset stem

Inward stem selection uses head-local y ≥ 121 mm and padding material: 84 triangles per headset. Nearest retained mount-band/pin results equal the overhead-band-only results in static poses. Dense minimum / median / maximum distances are approximately left 0.231 / 1.491 / 7.607 mm and right 0.231 / 1.471 / 7.607 mm in both N and ADS. Distances to the helmet shell itself are much larger (minimum about 10.47–10.49 mm), so the relevant nearby attachment surface is the retained overhead band.

The carry path leaves these headset distances constant. Dense start/peak/end values are left 0.231 / 1.492 / 7.607 mm and right 0.231 / 1.471 / 7.607 mm. Coarse vertex/centroid minima are 0.272 / 0.279 mm, again reflecting sampling rather than a geometry change. The upper stem is locally near the band; its full inward face is not flush.

## Pixel observations

- Actual source-material crops and new isolated sleeve/cap side and reverse views were rendered directly from the pinned evaluated source. Diagnostic isolation changes visibility and colors only; it does not add a backing object or alter cap geometry.
- Cap-only inward views show a smooth, uninterrupted contoured padded back. The side views show that back rising from the same cap’s thin outer rim. This supports an integrated rigid padded-shell visual interpretation.
- The peripheral lip visibly projects away from the sleeve. The source’s near-black shading can conceal small central separation, while isolated colors make the contour and outer daylight easier to see.
- ADS left has the most apparent central stand-off. It remains an integrated cap shape, but it is less snug than neutral or the bounded carry peak.
- The movement-peak source and isolated side pixels retain the same integrated backing appearance with a projecting rim. Their visual impression agrees with the closer central peak measurements.
- Source and isolated headset closeups show the upper stem immediately in front of the retained overhead band. They support local alignment/proximity; an overlay in the view does not prove contact.

Inspected pixel sheets:

- [Neutral left](N_left_PIXEL_REVIEW.png), [Neutral right](N_right_PIXEL_REVIEW.png)
- [ADS left](ADS_left_PIXEL_REVIEW.png), [ADS right](ADS_right_PIXEL_REVIEW.png)
- [Headset stems](HEADSET_PIXEL_REVIEW.png)
- [Movement peak](MOVEMENT_PEAK_PIXEL_REVIEW.png)

## Method and limits

Dense samples are deduplicated barycentric surface grids with maximum triangle-edge step 0.5 mm. Distances use nearest triangles on actual evaluated native sleeves and retained head surfaces. Quantiles describe these sampled points, not exact area-weighted distributions. All distances are unsigned. Positive sampled distance is not proof of zero triangle crossings; that independent audit belongs elsewhere. The 181 temporal observations are discrete and not a continuous-time guarantee. No topology, carrier, joint convention, collision, shoulder, camera, force, fastening, broad movement or final acceptance gate is issued here.

Raw evidence: [Static measurements](ATTACHMENT_MEASUREMENTS.json), [Movement measurements](MOVEMENT_ATTACHMENT_MEASUREMENTS.json), [Static/source view metadata](RENDER_CAMERAS.json), [Movement view metadata](RENDER_MOVEMENT_CAMERAS.json).
