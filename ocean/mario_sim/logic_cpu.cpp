#include "logic_cpu.h"
#include "generated_logic.h"
int smb_native_frame(SmbLogic* state,const uint8_t* data,int buttons) {
    return smb_logic_frame(state,data,buttons);
}
