#pragma once
#include "logic.h"

// Data-only world parameters. The translated instruction stream is immutable;
// level data is a separate per-world input shared by every frame of a rollout.
// These setters are usable by both CPU and CUDA scene builders.
SMB_HD int smb_world_byte(const uint8_t* data,int address) {
    return address>=0x8000&&address<0x10000?data[address-0x8000]:-1;
}
SMB_HD int smb_world_area_pointer(const uint8_t* data,int type,int index,int enemies) {
    const int counts[]={3,22,3,6};
    if(type<0||type>3||index<0||index>=counts[type])return -1;
    int table=enemies?0x9ce0:0x9d28;
    int offset=smb_world_byte(data,table+type)+index;
    return smb_world_byte(data,table+4+offset)+256*smb_world_byte(data,table+38+offset);
}
SMB_HD int smb_world_enemy_type(uint8_t* data,int area_type,int area_index,int entry,int type) {
    if(type<0||type>62||entry<0)return -1;
    int pointer=smb_world_area_pointer(data,area_type,area_index,1);
    if(pointer<0x8000)return -1;
    for(int offset=0,found=0;offset<256;) {
        int first=smb_world_byte(data,pointer+offset),second=smb_world_byte(data,pointer+offset+1);
        if(first<0||second<0||first==255)return -1;
        int row=first&15;
        if(row==14){offset+=3;continue;}
        if(row!=15) {
            if(found++==entry){data[pointer+offset+1-0x8000]=(uint8_t)((second&192)|type);return 0;}
        }
        offset+=2;
    }
    return -1;
}
SMB_HD int smb_world_block_contents(uint8_t* data,int area_type,int area_index,int entry,int contents) {
    if(contents<0||contents>8||entry<0)return -1;
    int pointer=smb_world_area_pointer(data,area_type,area_index,0);
    if(pointer<0x8000)return -1;
    for(int offset=2,found=0;offset<258;offset+=2) {
        int first=smb_world_byte(data,pointer+offset),second=smb_world_byte(data,pointer+offset+1);
        if(first<0||second<0||first==253)return -1;
        if((first&15)<12&&!(second&112)&&(second&15)<=8) {
            if(found++==entry){data[pointer+offset+1-0x8000]=(uint8_t)((second&240)|contents);return 0;}
        }
    }
    return -1;
}
