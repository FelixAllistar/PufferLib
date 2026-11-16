// A small baseline JPEG writer (one header, no dependencies): 8-bit RGB in, 4:4:4 YCbCr, the
// standard quantisation tables scaled by quality and the standard Huffman tables (ITU T.81 Annex K).
// Used by the headless review tools (tools/terrashade.c), which run where no image library is.
#ifndef CRET_JPEG_WRITE_H
#define CRET_JPEG_WRITE_H
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { FILE* f; uint32_t buf; int bits; } JpgBits;
static void jpg_put(JpgBits* b, uint32_t code, int len) {
    b->buf = (b->buf << len) | (code & ((1u << len) - 1)); b->bits += len;
    while (b->bits >= 8) {
        uint8_t c = (uint8_t)(b->buf >> (b->bits - 8)); fputc(c, b->f); if (c == 0xff) fputc(0, b->f);   // byte stuffing
        b->bits -= 8;
    }
}
static const uint8_t JPG_ZZ[64] = {0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
                                   35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
static const uint8_t JPG_QL[64] = {16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
                                   18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
static const uint8_t JPG_QC[64] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                                   99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};
static const uint8_t JPG_DC_BITS[2][16] = {{0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0}, {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0}};
static const uint8_t JPG_DC_VALS[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const uint8_t JPG_AC_BITS[2][16] = {{0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d}, {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77}};
static const uint8_t JPG_AC_VALS[2][162] = {
    {0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
     0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
     0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
     0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
     0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
     0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
     0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa},
    {0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
     0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
     0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
     0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
     0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
     0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
     0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa}};
// Canonical codes from a BITS/VALS table: code[symbol], len[symbol].
static void jpg_codes(const uint8_t bits[16], const uint8_t* vals, uint16_t code[256], uint8_t len[256]) {
    memset(len, 0, 256); uint16_t c = 0; int k = 0;
    for (int l = 1; l <= 16; l++) { for (int i = 0; i < bits[l - 1]; i++) { code[vals[k]] = c++; len[vals[k++]] = (uint8_t)l; } c <<= 1; }
}
static int jpg_cat(int v) { int a = v < 0 ? -v : v, n = 0; while (a) { n++; a >>= 1; } return n; }
static void jpg_block(JpgBits* b, const float* px, const float* q, int* dc, const uint16_t* dcc, const uint8_t* dcl, const uint16_t* acc, const uint8_t* acl) {
    float tmp[64]; int z[64];
    for (int v = 0; v < 8; v++) for (int u = 0; u < 8; u++) {   // separable DCT-II, rows then columns
        float s = 0; for (int x = 0; x < 8; x++) s += px[v * 8 + x] * cosf((2 * x + 1) * u * 3.14159265f / 16);
        tmp[v * 8 + u] = s * (u ? 0.5f : 0.35355339f);
    }
    for (int u = 0; u < 8; u++) for (int v = 0; v < 8; v++) {
        float s = 0; for (int y = 0; y < 8; y++) s += tmp[y * 8 + u] * cosf((2 * y + 1) * v * 3.14159265f / 16);
        s *= v ? 0.5f : 0.35355339f;
        z[v * 8 + u] = (int)lroundf(s / q[v * 8 + u]);
    }
    int d = z[0] - *dc, c = jpg_cat(d); *dc = z[0];
    jpg_put(b, dcc[c], dcl[c]); if (c) jpg_put(b, (uint32_t)(d < 0 ? d + (1 << c) - 1 : d), c);
    int run = 0;
    for (int k = 1; k < 64; k++) {
        int v = z[JPG_ZZ[k]];
        if (!v) { run++; continue; }
        while (run > 15) { jpg_put(b, acc[0xf0], acl[0xf0]); run -= 16; }
        c = jpg_cat(v); int sym = run << 4 | c;
        jpg_put(b, acc[sym], acl[sym]); jpg_put(b, (uint32_t)(v < 0 ? v + (1 << c) - 1 : v), c); run = 0;
    }
    if (run) jpg_put(b, acc[0], acl[0]);
}
// Writes rgb (w x h, 3 bytes a pixel, rows top down) as a JPEG of quality 1..100. Returns 0 on success.
static int jpeg_write(const char* path, const uint8_t* rgb, int w, int h, int quality) {
    FILE* f = fopen(path, "wb"); if (!f) return -1;
    int qs = quality < 50 ? 5000 / (quality < 1 ? 1 : quality) : 200 - 2 * (quality > 100 ? 100 : quality);
    uint8_t qt[2][64]; float qf[2][64];
    for (int i = 0; i < 64; i++) for (int t = 0; t < 2; t++) {
        int v = ((t ? JPG_QC : JPG_QL)[i] * qs + 50) / 100; v = v < 1 ? 1 : v > 255 ? 255 : v; qt[t][i] = (uint8_t)v; qf[t][i] = (float)v;
    }
    static const uint8_t app0[] = {0xff, 0xd8, 0xff, 0xe0, 0, 16, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0};
    fwrite(app0, 1, sizeof app0, f);
    for (int t = 0; t < 2; t++) { uint8_t hd[] = {0xff, 0xdb, 0, 67, (uint8_t)t}; fwrite(hd, 1, 5, f); for (int k = 0; k < 64; k++) fputc(qt[t][JPG_ZZ[k]], f); }
    uint8_t sof[] = {0xff, 0xc0, 0, 17, 8, (uint8_t)(h >> 8), (uint8_t)h, (uint8_t)(w >> 8), (uint8_t)w, 3, 1, 0x11, 0, 2, 0x11, 1, 3, 0x11, 1};
    fwrite(sof, 1, sizeof sof, f);
    for (int t = 0; t < 2; t++) for (int ac = 0; ac < 2; ac++) {
        const uint8_t* bits = ac ? JPG_AC_BITS[t] : JPG_DC_BITS[t]; const uint8_t* vals = ac ? JPG_AC_VALS[t] : JPG_DC_VALS;
        int n = 0; for (int i = 0; i < 16; i++) n += bits[i];
        uint8_t hd[] = {0xff, 0xc4, (uint8_t)((19 + n) >> 8), (uint8_t)(19 + n), (uint8_t)(ac << 4 | t)};
        fwrite(hd, 1, 5, f); fwrite(bits, 1, 16, f); fwrite(vals, 1, n, f);
    }
    static const uint8_t sos[] = {0xff, 0xda, 0, 12, 3, 1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0};
    fwrite(sos, 1, sizeof sos, f);
    uint16_t dcc[2][256], acc[2][256]; uint8_t dcl[2][256], acl[2][256];
    for (int t = 0; t < 2; t++) { jpg_codes(JPG_DC_BITS[t], JPG_DC_VALS, dcc[t], dcl[t]); jpg_codes(JPG_AC_BITS[t], JPG_AC_VALS[t], acc[t], acl[t]); }
    JpgBits b = {f, 0, 0}; int dc[3] = {0, 0, 0};
    for (int by = 0; by < h; by += 8) for (int bx = 0; bx < w; bx += 8) {
        float yc[3][64];
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
            int sx = bx + x < w ? bx + x : w - 1, sy = by + y < h ? by + y : h - 1; const uint8_t* p = rgb + ((size_t)sy * w + sx) * 3;
            float r = p[0], g = p[1], bl = p[2];
            yc[0][y * 8 + x] = 0.299f * r + 0.587f * g + 0.114f * bl - 128;
            yc[1][y * 8 + x] = -0.168736f * r - 0.331264f * g + 0.5f * bl;
            yc[2][y * 8 + x] = 0.5f * r - 0.418688f * g - 0.081312f * bl;
        }
        for (int c = 0; c < 3; c++) jpg_block(&b, yc[c], qf[c ? 1 : 0], &dc[c], dcc[c ? 1 : 0], dcl[c ? 1 : 0], acc[c ? 1 : 0], acl[c ? 1 : 0]);
    }
    jpg_put(&b, 0x7f, 7);   // pad the last byte with ones
    fputc(0xff, f); fputc(0xd9, f);
    return fclose(f);
}
#endif
