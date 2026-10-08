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
