# near: hill-climb log (round 2)

Track: the trees you stand under (full meshes, near bark and leaf shading, the small woody plants).
One line per pass: the fault, the change, kept or reverted, why. Views: the gallery's match capture of
single trees at 3 and 10 m (eye 1.2 m) and from under the crown (`CRET_GALLERY_MATCH_AIM=0.6`,
`CRET_GALLERY_MATCH_EYE=1.0`, 4 and 9 m; the two switches were added for this pass), the tree gallery
pages, and island world shots.

1. Cycad trunks drew near black (sRGB 19 on the lit side at 3 m), tree-fern and palm trunks nearly so, all under their own crown. Three causes, three changes: bark under a low crown is now baked by the visibility of its sides, a cell and a half out from its surface (it counted the trunk's own area and the crown above as occluders); bark takes twice the leaves' ground bounce (terrain.fs takes about three times; a trunk's sides face the lit ground); the cycad, palm and tree-fern bark tones are lighter (cycad 0.11 to 0.17 albedo, warm brown). Kept: the cycad trunk 19 to 38 sRGB with its armour visible, the palm a grey ringed column, other trunks in shade a little warmer and less blue.
