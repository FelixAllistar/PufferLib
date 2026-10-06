# Mixamo SWAT comparison assets

Private delivery of the converted assets from your authorized Mixamo download.
Unzip this archive over the swat-animation-testbed repository root; the files belong in assets/generated/.

Included: normalized meter-space SWAT GLB and editable Blender file, separate original proxy rifle, a provenance/animation manifest, and the inspected Idle still. The source-code package contains the converter, asset validation harness, procedural fallback, and run instructions. The short motion-preview MP4 is delivered separately. Original FBX sources and temporary render frames are not bundled.

The character is Ch15_nonPBR from Adobe Mixamo, with Idle, Walk, CrouchIdle, CrouchWalk and Aim source clips from Pro Rifle Pack. The downloaded source archive contained one character and 49 animation FBXs. The converted character and animations remain subject to applicable Adobe/Mixamo terms; the project code license does not relicense them. This conversion does not establish suitability for ML training. Source: https://www.mixamo.com/

Coordinates: meters; +Y up; +Z nominal forward. Rig object transforms are identity. For a live weapon attachment, use the animated global pose of RifleSocket. Its local axes are +Y up and +Z barrel; Muzzle is a child at (0, 0.09, 0.73) meters. Read names from the manifest, not fixed joint indices.

Verification: Blender idle/walk/crouch renders were inspected. Independent GLB checks passed for inverse-bind transforms, joint order, unit socket scale, and rigid hand/socket/muzzle attachment across all 254 exported clip samples. Source animations were baked at 30 FPS; linear horizontal walk travel was removed.

First-slice limitations: support-hand contact is approximate, source CrouchIdle is a kneeling stance, transitions need care, and the held rifle points about 22 degrees to the right of nominal forward in Idle. First-person view is a preview rather than sight-perfect ADS. No runtime IK, foot locking, ragdoll, or production retargeting is provided by the asset itself.
