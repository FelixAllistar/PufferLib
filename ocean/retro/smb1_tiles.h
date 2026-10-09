#pragma once
// Evaluation-only SMB1 collision-map residency. The parser cursor points at
// the NEXT unwritten column at phases 0 and 4, otherwise at the current one.
// The two-page circular buffer contains exactly 32 columns, never 33.
static inline int smb1_last_written_column(const unsigned char* ram) {
    int cursor=ram[0x725]*16+ram[0x726];
    return cursor-((ram[0x71f]&3)==0);
}
static inline int smb1_resident_tile(const unsigned char* ram,int col,int row) {
    if(row<2||row>=15)return 0;
    int last=smb1_last_written_column(ram);
    if(col<0||col<last-31||col>last)return -1;
    return ram[0x500+((col&16)?0xd0:0)+(row-2)*16+(col&15)];
}
