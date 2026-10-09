#ifndef SWAT_MOTEL_ZONING_H
#define SWAT_MOTEL_ZONING_H
// Frozen Phase A mapping.json: twelve flat soil owners; all other faces retain their material.
static bool swat_motel_zoning_part(int part) {
    switch(part) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 5:
    case 12:
    case 13:
    case 27:
    case 28:
    case 31:
    case 52:
    case 53: return true;
    default: return false;
    }
}
#endif
