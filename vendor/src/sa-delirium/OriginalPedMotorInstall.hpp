#pragma once
// The ordinary native XY overwrite is replaced only for a fresh shared owner.
// Other native UpdatePosition branches and all ground/Z work remain intact.
static bool installOriginalPedMotor(){
    using namespace original_ped_motor_branch;
    originalPedMotorEnabled=false;
    auto*location=reinterpret_cast<unsigned char*>(site);
    if(std::memcmp(location,expected,sizeof(expected))!=0){
        event("original_ped_motor_install_refused",-1,-1,"native_branch_bytes_changed=1 no_mutation=1");return false;
    }
    configureOriginalPedMotorCapture();
    DWORD old=0;
    if(!VirtualProtect(location,sizeof(expected),PAGE_EXECUTE_READWRITE,&old))return false;
    DWORD unused=0;
    if(std::memcmp(location,expected,sizeof(expected))!=0){
        VirtualProtect(location,sizeof(expected),old,&unused);return false;
    }
    std::array<unsigned char,6> replacement{0xE9,0,0,0,0,0x90};
    const int32_t relative=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&branch)-(site+5));
    std::memcpy(replacement.data()+1,&relative,4);std::memcpy(location,replacement.data(),replacement.size());
    if(FlushInstructionCache(GetCurrentProcess(),location,replacement.size())&&
       VirtualProtect(location,replacement.size(),old,&unused)){
        originalPedMotorEnabled=true;
        event("original_ped_motor_installed",-1,-1,"ordinary_grounded_xy=1 force_in_common_substeps=1 moving_support_and_special_slope_native=1");return true;
    }
    // Failed protection restoration leaves the current writable protection
    // unchanged; roll back the complete instruction and its protection.
    std::memcpy(location,expected,sizeof(expected));
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),location,sizeof(expected))!=0;
    const bool restored=VirtualProtect(location,sizeof(expected),old,&unused)!=0;
    event("original_ped_motor_install_rolled_back",-1,-1,"cache_flushed="+std::to_string(flushed)+" protection_restored="+std::to_string(restored));return false;
}
