# Room 103 personal props — engine integration

Original wallet and glasses GLBs live in `assets/environment/motel_personal`.
They retain their original hashes, metre scale, 1,428/1,400 triangles and source
PBR materials. The compact editable source and original QA are retained above;
delivered scripts were not executed.

Both are visual details attached to desk owner 72. Wallet root is
`(.8,.7606,-2.03)`, yaw +8 degrees; glasses root is `(.8,.7606,-2.23)`, yaw -12.
No extra navigation/collision objects were added. Rendering follows active
desk ownership and all models are released on location change.

The artist's provisional tray used yaw zero. The actual engine tray is +90
degrees; native transformed-vertex checks require at least 65 mm clearance
from its east edge: measured clearances are 75.35 mm (wallet) and 70.86 mm
(glasses), with 63.45/59.90 mm remaining to the desktop edge. Native desk
detail/context screenshots are retained here; the original source QA remains
provenance rather than a claim that its provisional placement was the engine's.

## Detached-stroke diagnosis, 2026-10-08

The long black tabletop strokes are original desk scratch geometry, not detached
eyeglass arms or shadows. Native fixed-camera captures isolate eyeglass casting,
lamp-shadow receiving and contact occlusion; the strokes persist in every case,
in unlit rendering, and with both personal props removed. The original desk GLB
has seven separate `HP_wood_dark` scratch boxes at local Y=.7497–.7523 m over its
.7500 m desktop, roughly .12–.18 m long. Room 103 owner 72 uses this original desk;
the existing W2 art replacement only covers Room 101 owner 24.

`personal-unlit.png` and `personal-no-personal-props.png` retain the decisive
captures; original detail above is the matched baseline. Diagnostic mode:
`test_environment_art.exe CAPTURE_DIRECTORY personal`. All toggles are test-local,
restore their models/lighting, and leave the authoritative world byte-identical.
Shifted-camera and individual lighting-isolation captures/logs were reviewed under
`build/swat/review/fence-destruction/personal`. The artist was given this attribution
and asked to preserve the glasses/wallet while queuing a measured desk cleanup.
