# trunks: hill-climb log

One line per pass: the fault, the change, kept or reverted, why. Views: the
gallery's LOD-match capture of 11 trees (`CRET_GALLERY_MATCH`, at 3 m and
30 m, from the shaded and the sunlit side; `CRET_GALLERY_MATCH_DIST`,
`_AZ` and `_FRAMES` were added for this), the tree gallery pages, and the
island world shots.

1. Every species shared four 512 px bark scans, drawn at about half their real size (a tile 0.8 to 1 m round a trunk). Added four bark layers (araucaria plates `pine_bark`, podocarp strips `japanese_cedar_bark`, ginkgo furrows `bark_brown_02`, plane flakes `bark_platanus`; the general broadleaf now `chinese_hackberry_bark`), fetched at 2K, and each tile laid at the scan's own size (1 to 1.8 m). Kept: neutral in shade, but the plates and strips read at their real size once lit (pass 3).
2. Trunks were smooth cones: a faceted skirt at the foot (redwood) or nothing, radial normals. Added a bark relief by species on every trunk (root flare with 4 to 8 buttress roots, flutes fading up the trunk, bumps and burls), trunk rings every 0.25 m in the bottom 2 m, more sides for the flutes, normals from the surface itself. Kept: the redwood foot is a real flared, rooted base, and every trunk now widens into the ground instead of standing on it like a post.
3. The bark was near black in sun and shade (median sRGB 18 on the redwood) and its texture invisible: dark targets (0.05 to 0.11 linear), a grey-brown tone multiplied on top, and the shader's cavity term taking another 20 to 33%. Each scan now has its own furrow shade baked in at fetch time (its AO times the low end of its height map), the shader's cavity term is a third as strong on bark, the conifer and broadleaf tones are 1, and each species' mean bark albedo is set to a real one (redwood 0.17/0.088/0.056, plane 0.19/0.18/0.145). Kept: plates on the araucaria, red fibrous strips on the redwood, grey furrows on the ginkgo, from both sides. (A probe showed the lit side is not self-shadowed: a high sun just grazes a trunk.)
