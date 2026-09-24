#pragma once

// SMB1 World/NTSC: same PRG+CHR, two verified container headers.
// iNES SHA-1: ea343f4e445a9050d4b4fbac2c77d0693b1d0922
// NES 2.0 SHA-1: 33d23c2f2cfa4c9efec87f7bc1321ce3ce6c89bd
// The block generator reads these same definitions; PAL is never accepted.
#define RETRO_SMB1_NTSC_INES_FNV 0xbf120bdcb8979836ull
#define RETRO_SMB1_NTSC_NES2_FNV 0x6e01246e5d215cb3ull
#define RETRO_SMB1_ROM_PATH "ocean/retro/roms/smb1_ntsc.nes"

static bool retro_ntsc_rom_fingerprint(unsigned long long hash) {
    return hash==RETRO_SMB1_NTSC_INES_FNV||hash==RETRO_SMB1_NTSC_NES2_FNV;
}

// Verified against the imported NTSC PRG and all 32 natural level boots.
static constexpr int RETRO_WORLD_OFFSETS=0x9cb4;
static constexpr int RETRO_AREA_OFFSETS=0x9cbc;
static constexpr int RETRO_AREA_DATA_OFFSETS=0x9d28;
static constexpr int RETRO_AREA_DATA_LOW=0x9d2c;
static constexpr int RETRO_AREA_DATA_HIGH=0x9d4e;
