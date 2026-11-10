// GLSL ES 3.00 prelude for the web build (WebGL2). render/gfx.c puts it in
// place of each shader's "#version 330" line when built with Emscripten; the
// desktop build never reads it. ES has no default precision for floats in
// fragment shaders or for the 3D, array and shadow samplers, so everything is
// declared highp, as on desktop GL.
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp sampler3D;
precision highp sampler2DArray;
precision highp sampler2DShadow;
precision highp sampler2DArrayShadow;
precision highp usampler2D;
precision highp isampler2D;
#define CRET_WEB 1
