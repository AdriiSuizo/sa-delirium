#pragma once
// Diagnostics only (GTASA_SOLVER_GAP_AUDIT=1): the solver's own contact gaps
// per live agent, deepest per category over 100 ms windows. Categories as
// delirium::ragdoll::GapAudit: world, vehicle/obstacle, animated ped owner,
// other capsule bodies, own parts. Reads solver rows, never the rendered pose.
struct SolverGapWindow{float gap[5];int part[5];uint32_t kind[5],other[5];unsigned rows[5],deep10[5],deep30[5],steps=0,start=0;};
static std::map<int,SolverGapWindow> solverGapWindows;
static bool solverGapAuditEnabled(){static const bool on=GetEnvironmentVariableA("GTASA_SOLVER_GAP_AUDIT",nullptr,0)>0;return on;}
static void resetSolverGapWindow(SolverGapWindow&w,unsigned now){
    for(int k=0;k<5;k++){w.gap[k]=INFINITY;w.part[k]=-1;w.kind[k]=w.other[k]=0;w.rows[k]=w.deep10[k]=w.deep30[k]=0;}
    w.steps=0;w.start=now;
}
static void noteSolverGapAudit(const Active&a,unsigned now){
    if(!solverGapAuditEnabled())return;
    float rows[5*8]{};if(nm_agent_gap_audit(a.agent,rows,5)!=5)return;
    auto found=solverGapWindows.find(a.ref);
    if(found==solverGapWindows.end()){found=solverGapWindows.emplace(a.ref,SolverGapWindow{}).first;resetSolverGapWindow(found->second,now);}
    auto&w=found->second;w.steps++;
    for(int k=0;k<5;k++){const float*r=rows+k*8;
        w.rows[k]+=unsigned(r[5]);w.deep10[k]+=unsigned(r[6]);w.deep30[k]+=unsigned(r[7]);
        if(r[1]<w.gap[k]){w.gap[k]=r[1];w.part[k]=int(r[2]);w.kind[k]=uint32_t(r[3]);w.other[k]=uint32_t(r[4]);}
    }
    if(now-w.start<100)return;
    static const char*names[5]={"world","vehicle","ped_owner","capsule","self"};
    std::string detail="steps="+std::to_string(w.steps)+" recovering="+std::to_string(a.recovering)+" kind="+std::to_string(int(a.kind));
    for(int k=0;k<5;k++){
        detail+=std::string(" ")+names[k]+"_rows="+std::to_string(w.rows[k]);
        if(!w.rows[k])continue;
        detail+=std::string(" ")+names[k]+"_gap="+std::to_string(w.gap[k])+" "+names[k]+"_part="+std::to_string(w.part[k])+
            " "+names[k]+"_other="+std::to_string(w.kind[k])+":"+std::to_string(w.other[k])+
            " "+names[k]+"_below1cm="+std::to_string(w.deep10[k])+" "+names[k]+"_below3cm="+std::to_string(w.deep30[k]);
    }
    event("solver_gap_sample",a.ref,a.model,detail+" source=solver_contact_rows");
    resetSolverGapWindow(w,now);
}
