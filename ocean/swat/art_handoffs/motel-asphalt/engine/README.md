# Motel asphalt integration

Original Poly Haven Clean Asphalt, Dimitrios Savva, CC0, from artist handoff
Drive 1HT8UbsP5G07S3P6-okQccn5vuVwT06Nc. The three original 1024² 16-bit PNGs
are retained once in `assets/environment/motel_asphalt`; hashes/provenance are
one directory above. The extracted delivery and duplicate archive are disposable.

Only motel ground owner 0 uses this material. Physical concrete, dimensions and
top Y=-.08 are unchanged. World-plane U=+X/2.1, V=-Z/2.1 gives an upward tangent
frame. Upload flips image rows exactly once, preserves green, generates 11 mip
levels and requests 8× anisotropy. Base color is decoded as sRGB by the existing
shader; normal and roughness data are linear, metallic zero. No displacement,
AO multiply, baked lighting or broad noise variation has been added.

PNG16_SAMPLES.json is an independent native Node/zlib decode of source channels.
The native Windows graphics test reads back four coordinates from each uploaded
texture and checks normalized values within one 8-bit step after row inversion.
It also verifies mip levels, physical repeat, original ground physics and location
teardown. Walking/grazing captures were visually reviewed: restrained gray grain,
no obvious tile boundaries in those views. Fine detail remains 1K-resolution-limited.

`SWAT_ENVIRONMENT_PBR=0` retains color only; legacy style or missing base color
uses the prior ground fallback. Runtime loads on motel entry and releases on
location change. Full graphics regression and ordinary `./swat` capture pass.
