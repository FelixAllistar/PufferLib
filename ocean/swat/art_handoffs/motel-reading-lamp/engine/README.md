# Bedside light switches

Each of the four motel reading lamps now uses its visible pushbutton. Aim at the
button on the mounting plate and press F within 1.7 m; the prompt appears out to
2.2 m. The authored button center is local (0, -.034, .0165), transformed with
the supported fixture. Its targeting tolerance is 2.5–6 cm, with an exact ray to
the button checking intervening walls, props and actors. No extra collider is
added and shooting through the fixture retains the existing physical behavior.

A fresh press toggles the authoritative room_light_off_mask; held F does not
repeat. The same selector supplies the HUD and simulation. On/off changes the
existing room light's radiance without changing geometry or rebuilding static
shadow maps. Removing the mounting board still removes the fixture and its
light regardless of switch state. Scenario reset restores the default lights.
This changes visible illumination only; NPC perception is not brightness-aware.

Snapshots carry the switches, including late joins. Wire version is now 10;
old clients and old version-9 replay/save files are rejected by the existing
version check. Replays record the ordinary interact input and include light
state in their digest. No training observation or action dimensions changed.

Validation: tactical checks exercise all four buttons, held/released input,
reach, misses, wall/prop occlusion, support loss, snapshot serialization and
application, invalid masks and reset. Native frontend checks exercise actual F
routing to interaction rather than compliance. Native rendering compares the
same room on/off, asserts changed illumination and separately checks support
removal. See validation.txt and reading-lamp-room-on/off.png.
