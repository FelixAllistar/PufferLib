// Scene depth (render/view.c): reverse-Z in a 32-bit float buffer, stored
// depth = CRET_NEAR / w (sky = 0, test GEQUAL). Written by the rasterizer,
// never by gl_FragDepth, so early-Z and the foliage depth prepass work.
// u_depth = (a, b) sets clip z = a * w + b: (0, near) with glClipControl
// 0..1, (-1, 2 near) without it; (0, 0) leaves the projection's z alone
// (shadow-map passes share these vertex shaders).
uniform vec2 u_depth;
invariant gl_Position;
vec4 scene_clip(vec4 c) { if (u_depth.y != 0.0) c.z = u_depth.x * c.w + u_depth.y; return c; }
