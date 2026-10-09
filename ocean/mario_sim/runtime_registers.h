#pragma once
#include "logic.h"
#include "runtime_poll.h"

// Keep CPU registers and the instruction clock in scalar locals while a
// compiled page runs. They are written back at every page/interrupt boundary.
// RAM, controller latches and PPU effects keep the reference representation.
typedef struct {uint8_t a,x,y,p,sp;} SmbRegisters;
SMB_HD void smb_reg_nz(SmbRegisters& r,int value) {
    r.p=(r.p&~130)|((value&255)?0:2)|(value&128);
}
SMB_HD void smb_reg_cmp(SmbRegisters& r,int a,int b) {
    r.p=(r.p&~1)|(a>=b);smb_reg_nz(r,(a-b)&255);
}
SMB_HD void smb_reg_adc(SmbRegisters& r,int value) {
    int a=r.a,t=a+value+(r.p&1);
    r.p=(r.p&~65)|(t>255)|((~(a^value)&(a^t)&128)>>1);
    r.a=(uint8_t)t;smb_reg_nz(r,r.a);
}
SMB_HD int smb_reg_shift(SmbRegisters& r,int value,int kind) {
    int carry=r.p&1,result;
    if(kind==0||kind==1){r.p=(r.p&~1)|(value>>7);result=((value<<1)|(kind==1?carry:0))&255;}
    else {r.p=(r.p&~1)|(value&1);result=(value>>1)|(kind==3?carry*128:0);}
    smb_reg_nz(r,result);return result;
}
SMB_HD void smb_reg_push(SmbLogic* s,SmbRegisters& r,int value) {
    s->ram[256+r.sp]=(uint8_t)value;--r.sp;
}
SMB_HD int smb_reg_pop(SmbLogic* s,SmbRegisters& r) {
    ++r.sp;return s->ram[256+r.sp];
}
SMB_HD int smb_reg_read(SmbLogic* s,const uint8_t* data,int address,int& clock) {
    address&=65535;
    if(address<8192)return s->ram[address&2047];
    if(address>=32768)return data[address-32768];
    s->timing.clock=clock;int value=smb_read(s,data,address);clock=s->timing.clock;return value;
}
SMB_HD void smb_reg_write(SmbLogic* s,int address,int value,int& clock) {
    address&=65535;
    if(address<8192){s->ram[address&2047]=(uint8_t)value;return;}
    s->timing.clock=clock;smb_write(s,address,value);clock=s->timing.clock;
}
SMB_HD void smb_reg_poll(SmbLogic* s,SmbRegisters& r,int& clock,uint32_t& instructions,int wait_for_set) {
    s->a=r.a;s->p=r.p;s->timing.clock=clock;s->instructions=instructions;
    smb_runtime_poll_skip(s,wait_for_set);
    r.a=s->a;r.p=s->p;clock=s->timing.clock;instructions=s->instructions;
}
