#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>

// DetailedCapture only: one row per physics frame that cost at least 5 ms, with the engine's own
// stages summed over its substeps. A substep costs 2-5 ms, under the 8 ms floor of the older
// per-step report, so bursts of cheap substeps were invisible (07/11). Off: one branch per frame.
namespace physicsbreakdown {
constexpr double minimumMs=5.;
constexpr std::uintmax_t maximumBytes=64ull*1024*1024;
struct Frame {
    unsigned frame=0,gameMs=0;float dt=0,fastest=0;int substeps=0,stepLimit=0,bodies=0,owners=0,vehicleShapes=0;bool playerFast=false;
    double sceneMs=0,bindingMs=0,solverMs=0,commitMs=0,totalMs=0;
};
struct Output {
    std::ofstream file;std::uintmax_t bytes=0;unsigned buffered=0;
    Output(){
        const DWORD n=GetEnvironmentVariableW(L"GTASA_PHYSICS_BREAKDOWN",nullptr,0);
        if(n<2||n>32768)return;
        std::wstring path(n,L'\0');GetEnvironmentVariableW(L"GTASA_PHYSICS_BREAKDOWN",path.data(),n);path.resize(n-1);
        file.open(std::filesystem::path(path),std::ios::app);
    }
};
inline Output& output(){static Output o;return o;}
inline void list(std::ostream& f,const char* name,const double* v,int count){
    f<<",\""<<name<<"\":[";for(int i=0;i<count;i++){if(i)f<<',';f<<v[i];}f<<']';
}
// Always take the counters, so a skipped cheap frame never adds to the next row.
inline void record(NmGroup* group,const Frame& x){
    auto& out=output();if(!out.file.is_open()||!group)return;
    std::array<double,44> v{};
    if(nm_group_profile_take(group,v.data(),int(v.size()))!=int(v.size()))return;
    if(x.totalMs<minimumMs||out.bytes>=maximumBytes)return;
    const auto before=out.file.tellp();auto& f=out.file;
    f<<std::setprecision(5)<<"{\"frame\":"<<x.frame<<",\"game_ms\":"<<x.gameMs<<",\"dt\":"<<x.dt<<",\"substeps\":"<<x.substeps<<",\"step_limit\":"<<x.stepLimit
     <<",\"fastest\":"<<x.fastest<<",\"player_fast\":"<<(x.playerFast?1:0)<<",\"bodies\":"<<x.bodies<<",\"owners\":"<<x.owners<<",\"vehicle_shapes\":"<<x.vehicleShapes
     <<",\"total_ms\":"<<x.totalMs<<",\"scene_ms\":"<<x.sceneMs<<",\"binding_ms\":"<<x.bindingMs<<",\"solver_ms\":"<<x.solverMs<<",\"commit_ms\":"<<x.commitMs
     <<",\"engine_steps\":"<<v[0]<<",\"members_max\":"<<v[1]<<",\"engine_ms\":"<<v[2]<<",\"worst_step_ms\":"<<v[3];
    list(f,"ground_prep_pairs_solve_ms",&v[4],4);
    list(f,"peers_alone_build_velocity_prep_position_end_commit_ms",&v[8],8);
    list(f,"impulse_warm_index_sweep_ms",&v[16],3);
    list(f,"impulse_calls_rows_blocks_flat_sweeps",&v[19],5);
    f<<",\"owner_pair_prepare_ms\":"<<v[24];
    list(f,"owner_pair_counts",&v[25],4);
    f<<",\"alone_bodies\":"<<v[29]<<",\"islands\":"<<v[30]<<",\"largest_island\":"<<v[31];
    // Summed over bodies that ran on several cores: compare with prep, whose time is the slowest of them.
    f<<",\"contact_bodies\":"<<v[32];
    list(f,"contact_world_vehicle_manifold_self_block_ms",&v[33],5);
    f<<",\"contact_worst_body_ms\":"<<v[38]<<",\"contact_total_ms\":"<<v[39]<<",\"contact_components\":"<<v[40]<<",\"contact_manifolds\":"<<v[41]
     <<",\"contact_hits\":"<<v[42]<<",\"contact_blocks\":"<<v[43]<<"}\n";
    const auto after=f.tellp();if(before>=0&&after>before)out.bytes+=std::uintmax_t(after-before);
    if(++out.buffered==32){f.flush();out.buffered=0;}
}
}
