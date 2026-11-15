"""Compile the real continuation path against owned COL geometry; never launch GTA."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def function(text, signature):
    begin = text.index(signature)
    start = text.index("{", begin)
    depth = 1
    end = start + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[begin:end]


def owned_woodenbox(game):
    with (game / "models/gta3.img").open("rb") as stream:
        magic, count = struct.unpack("<4sI", stream.read(8))
        if magic != b"VER2" or not 0 < count <= 100000:
            raise ValueError("Unsupported owned IMG directory")
        directory = stream.read(count * 32)
        entries = [struct.unpack_from("<IHH24s", directory, offset) for offset in range(0, len(directory), 32)]
        matches = [entry for entry in entries if entry[3].split(b"\0")[0].lower() == b"dynamic.col"]
        if len(matches) != 1:
            raise ValueError("Expected one original dynamic.col")
        sector, streaming, archive, _ = matches[0]
        stream.seek(sector * 2048)
        collision = stream.read((streaming or archive) * 2048)
    offset = 0
    while offset + 32 <= len(collision):
        magic, size = struct.unpack_from("<4sI", collision, offset)
        if magic not in (b"COLL", b"COL2", b"COL3", b"COL4") or size < 24:
            break
        record = collision[offset:offset + size + 8]
        if len(record) != size + 8:
            raise ValueError("Truncated collision record")
        name = record[8:30].split(b"\0")[0]
        if name == b"woodenbox":
            if magic != b"COL3" or struct.unpack_from("<H", record, 30)[0] != 1224:
                raise ValueError("Expected the original model 1224 COL3")
            spheres, boxes, triangles = struct.unpack_from("<HHH", record, 72)
            if (spheres, boxes, triangles) != (8, 1, 0):
                raise ValueError("Owned woodenbox geometry changed; do not guess this fixture")
            sphere_at, box_at = struct.unpack_from("<II", record, 84)
            sphere_rows = [struct.unpack_from("<4f", record, 4 + sphere_at + 20 * n) for n in range(spheres)]
            box = struct.unpack_from("<6f", record, 4 + box_at)
            return record, box, sphere_rows
        offset += size + 8
    raise ValueError("Owned woodenbox collision was not found")


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--game", type=Path, required=True)
parser.add_argument("--out", type=Path, required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
record, box, spheres = owned_woodenbox(args.game)
fixture = out / "owned-woodenbox-fixture.bin"
fixture.write_bytes(struct.pack("<6fI", *box, len(spheres)) + b"".join(struct.pack("<4f", *row) for row in spheres))
player = (repo / "vendor/src/sa-delirium/PlayerCover.hpp").read_text(encoding="utf-8")
# Pin the unchanged acquisition function from the public 0.4.0 source. This
# check also works from a corresponding-source archive without Git history.
assert hashlib.sha256(function(player, "static bool wallRay(").encode()).hexdigest() == \
    "8c79be9052f74a423ab619029b198fc1de69773cc26d861c5161a5b9f9025092", "Initial wall acquisition changed"
body = function(player, "static bool traceWall(")
replacements = {
    "reinterpret_cast<ColModel*(__thiscall*)(void*)>(0x535300)": "nativeColModel",
    "reinterpret_cast<void(__thiscall*)(void*,M*)>(0x54EF40)": "nativeSimpleMatrix",
    "reinterpret_cast<bool(__cdecl*)(const Line&,const M&,ColModel&,ColPoint&,float&,bool,bool)>(0x417950)": "nativeColliderRay",
}
for old, new in replacements.items():
    assert body.count(old) == 1, old
    body = body.replace(old, new)

source = r'''
#include "CoverWallContinuity.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>
struct Wall {int vehicle=-1;V point{},normal{};bool low=false;};
struct ColPoint {V point;float pad0;V normal;float pad1;unsigned char surfaces[8];float depth;};
struct Sphere {V centre;float radius;};
struct ColModel {void* data;int row;};
struct Entity {alignas(16) unsigned char bytes[128]{};M frame{};ColModel col;};
static Entity entities[5];
static V boxMin{},boxMax{};
static std::vector<Sphere> spheres;
static unsigned enabled=31,worldCalls=0,nativeCalls=0,physicalReads=0;
static bool floorExists=true,useSimple=false,nativeAvailable=true,forced=false;
static ColPoint forcedHit{};
static Entity* forcedEntity=nullptr;
template<class T> T& at(void*p,size_t offset){
    if(offset==0x44||offset==0x50)++physicalReads;
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+offset);
}
static V flat(V v){v.z=0;return normalized(v);}
static int vehicleRef(void* e){return (static_cast<Entity*>(e)->bytes[0x36]&7)==2?512:-1;}
static bool ground(V,float& h){h=0;return floorExists;}
static M* matrix(void* e){return useSimple?nullptr:&static_cast<Entity*>(e)->frame;}
static ColModel* nativeColModel(void* e){return &static_cast<Entity*>(e)->col;}
static void nativeSimpleMatrix(void* simple,M* frame){*frame=reinterpret_cast<Entity*>(static_cast<unsigned char*>(simple)-4)->frame;}
static bool modelRay(V from,V to,const M& frame,ColPoint& hit,float& fraction){
    const V p=from-frame.pos,d=to-from;
    assert(std::abs(d.x)<1.e-5f&&std::abs(d.z)<1.e-5f&&d.y>0);
    float best=fraction;bool found=false;
    auto accept=[&](float y,V normal){
        const float t=(y-p.y)/d.y;
        if(t>=0&&t<=best){best=t;hit.point=from+d*t;hit.normal=normal;found=true;}
    };
    if(p.x>=boxMin.x&&p.x<=boxMax.x&&p.z>=boxMin.z&&p.z<=boxMax.z)accept(boxMin.y,{0,-1,0});
    for(const auto&s:spheres){
        const float x=p.x-s.centre.x,z=p.z-s.centre.z;
        const float residual=s.radius*s.radius-x*x-z*z;
        if(residual>=0){const float y=-std::sqrt(residual);accept(s.centre.y+y,{x/s.radius,y/s.radius,z/s.radius});}
    }
    if(found)fraction=best;
    return found;
}
static bool ray(V from,V to,ColPoint& hit,void*& entity){
    ++worldCalls;
    if(forced){hit=forcedHit;entity=forcedEntity;return forcedEntity!=nullptr;}
    bool found=false;float fraction=1;
    for(int i=0;i<5;i++)if(enabled&(1u<<i)){
        ColPoint candidate{};float limit=fraction;
        if(modelRay(from,to,entities[i].frame,candidate,limit)){
            fraction=limit;hit=candidate;entity=&entities[i];found=true;
        }
    }
    return found;
}
template<class Line> static bool nativeColliderRay(const Line&line,const M&frame,ColModel&col,ColPoint&hit,float&fraction,bool a,bool b){
    ++nativeCalls;assert(!a&&!b&&col.data);
    // The row is selected by the original native ray, never a new world query.
    assert(frame.pos.x==entities[col.row].frame.pos.x);
    return nativeAvailable&&modelRay(line.from,line.to,frame,hit,fraction);
}
'''
source += body + "\n"
source += r'''
static bool originalNominal(V root,const Wall& wall){
    root.z=.6f;ColPoint h{};void*e=nullptr;
    const V from=root+wall.normal*.12f;
    return ray(from,from-wall.normal*.95f,h,e)&&e&&finite(h.point)&&finite(h.normal)&&
        std::abs(h.normal.z)<=.4f&&dot(flat(h.normal),wall.normal)>=.55f;
}
int main(int argc,char**argv){
    assert(argc==2);std::ifstream input(argv[1],std::ios::binary);unsigned count=0;
    input.read(reinterpret_cast<char*>(&boxMin),12);input.read(reinterpret_cast<char*>(&boxMax),12);
    input.read(reinterpret_cast<char*>(&count),4);assert(count==8);spheres.resize(count);
    input.read(reinterpret_cast<char*>(spheres.data()),count*sizeof(Sphere));assert(input.good());
    for(int i=0;i<5;i++){
        auto&e=entities[i];e.bytes[0x36]=4;e.bytes[0x1c]=5;e.col={&e,i};
        e.frame.right={1,0,0};e.frame.up={0,1,0};e.frame.at={0,0,1};e.frame.pos={float(i-2)*.84f,0,.48f};
    }
    Wall wall{-1,{0,boxMin.y,.6f},{0,-1,0},true},hit{};
    unsigned recovered=0;
    for(int mm=-2300;mm<=2300;mm++){
        const V root{mm*.001f,boxMin.y-.55f,.9f};
        const bool old=originalNominal(root,wall);const unsigned before=nativeCalls;
        assert(traceWall(root,wall,hit));assert(dot(hit.normal,wall.normal)>.82f);
        if(!old){++recovered;assert(nativeCalls>before);}else assert(nativeCalls==before);
    }
    assert(recovered>0);
    // The nominal vehicle path is unchanged; validPlane accepts the exact
    // horizontal normalized normals produced by wallRay, including rotations.
    assert(cover_wall_continuity::validPlane(wall.point,normalized(V{.6f,-.8f,0})));
    entities[2].bytes[0x36]=2;Wall car=wall;car.vehicle=512;
    assert(traceWall({0,boxMin.y-.55f,.9f},car,hit)&&hit.vehicle==512);
    at<V>(&entities[2],0x44)={.04f,0,0};assert(traceWall({0,boxMin.y-.55f,.9f},car,hit));
    at<V>(&entities[2],0x44)={.041f,0,0};assert(!traceWall({0,boxMin.y-.55f,.9f},car,hit));
    at<V>(&entities[2],0x44)={};entities[2].bytes[0x36]=4;
    for(float x:{-2.5f,-2.4f,2.42f,2.5f})assert(!traceWall({x,boxMin.y-.55f,.9f},wall,hit));
    enabled=17;auto before=nativeCalls;assert(!traceWall({0,boxMin.y-.55f,.9f},wall,hit)&&nativeCalls==before);enabled=31;
    // Exercise a freshly-hit rounded seam with the real same-object fallback.
    const V seam{-.32f,boxMin.y-.55f,.9f};
    useSimple=true;assert(traceWall(seam,wall,hit));useSimple=false;
    nativeAvailable=false;assert(!traceWall(seam,wall,hit));nativeAvailable=true;
    floorExists=false;before=worldCalls;assert(!traceWall(seam,wall,hit)&&worldCalls==before);floorExists=true;
    Wall high=wall;high.low=false;before=nativeCalls;assert(!traceWall(seam,high,hit)&&nativeCalls==before);
    Wall vehicle=wall;vehicle.vehicle=5;assert(!traceWall(seam,vehicle,hit));
    // Capture the exact rejected sphere contact for entity/type guard tests.
    const V start{seam.x,seam.y-.12f,.6f};void* selected=nullptr;
    assert(ray(start,start+V{0,.95f,0},forcedHit,selected));forcedEntity=static_cast<Entity*>(selected);forced=true;
    for(unsigned type:{0u,2u,3u,6u,7u}){
        forcedEntity->bytes[0x36]=static_cast<unsigned char>(type);physicalReads=0;before=nativeCalls;
        assert(!traceWall(seam,wall,hit)&&physicalReads==0&&nativeCalls==before);
    }
    for(unsigned type:{1u,5u}){
        forcedEntity->bytes[0x36]=static_cast<unsigned char>(type);physicalReads=0;
        assert(traceWall(seam,wall,hit)&&physicalReads==0);
    }
    forcedEntity->bytes[0x36]=4;forcedEntity->bytes[0x1c]=1;before=nativeCalls;
    assert(!traceWall(seam,wall,hit)&&nativeCalls==before);forcedEntity->bytes[0x1c]=5;
    auto motion=[&](V move,V spin){at<V>(forcedEntity,0x44)=move;at<V>(forcedEntity,0x50)=spin;before=nativeCalls;assert(!traceWall(seam,wall,hit)&&nativeCalls==before);};
    const float nan=std::numeric_limits<float>::quiet_NaN();
    motion({.001f,0,0},{});motion({},{0,.001f,0});motion({nan,0,0},{});motion({},{0,0,nan});
    at<V>(forcedEntity,0x44)={};at<V>(forcedEntity,0x50)={};
    forcedEntity->bytes[0x1c]=4;assert(!traceWall(seam,wall,hit));forcedEntity->bytes[0x1c]=5;
    const auto good=forcedHit;
    for(V normal:{V{0,0,1},V{0,0,-1},V{0,-.2f,.98f},V{.7f,-.5f,-.509902f},V{nan,-1,0}}){
        forcedHit.normal=normal;before=nativeCalls;assert(!traceWall(seam,wall,hit)&&nativeCalls==before);
    }
    forcedHit=good;forcedHit.point.y-=.05f;before=nativeCalls;assert(!traceWall(seam,wall,hit)&&nativeCalls==before);
    forcedHit=good;forcedHit.point.x=nan;assert(!traceWall(seam,wall,hit));forcedHit=good;
    Wall invalid=wall;invalid.normal.x=nan;before=worldCalls;assert(!traceWall(seam,invalid,hit)&&worldCalls==before);
    assert(!traceWall({nan,0,0},wall,hit));
    auto savedMatrix=forcedEntity->frame;forcedEntity->frame.pos.x=nan;assert(!traceWall(seam,wall,hit));forcedEntity->frame=savedMatrix;
    forcedEntity->frame.at={};assert(!traceWall(seam,wall,hit));forcedEntity->frame=savedMatrix;
    forcedEntity->frame.right={1.e30f,0,0};forcedEntity->frame.up={0,1.e30f,0};
    assert(!traceWall(seam,wall,hit));forcedEntity->frame=savedMatrix;
    forcedEntity->col.data=nullptr;assert(!traceWall(seam,wall,hit));forcedEntity->col.data=forcedEntity;
    // Recovered surface must independently satisfy the original wall normal,
    // expected plane, and horizontal direction; a floor or side face is not cover.
    using namespace cover_wall_continuity;
    assert(recovered(wall.point,wall.normal,wall.point,wall.normal));
    assert(!recovered(wall.point,{0,0,1},wall.point,wall.normal));
    assert(!recovered(wall.point,{1,0,0},wall.point,wall.normal));
    assert(!recovered(wall.point+V{0,.05f,0},wall.normal,wall.point,wall.normal));
    assert(!recovered(wall.point,{nan,0,0},wall.point,wall.normal));
    assert(!recovered(wall.point+V{nan,0,0},wall.normal,wall.point,wall.normal));
    std::printf("PASS 4601 exact owned geometry samples; %u rounded-seam recoveries; real edges/gaps, floors, corners, plane/type/static/moving/finite guards, same collider and simple transforms\n",recovered);
}
'''
# Avoid a local counter hiding the policy function in the final negative tests.
source = source.replace("unsigned recovered=0;", "unsigned recoveredCount=0;")
source = source.replace("++recovered;", "++recoveredCount;").replace("assert(recovered>0);", "assert(recoveredCount>0);")
source = source.replace('",recovered);', '",recoveredCount);')
cpp, exe = out / "cover-wall-continuity.cpp", out / "cover-wall-continuity.exe"
cpp.write_text(source, encoding="utf-8")
build = out / "build.cmd"
build.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\Tools\\VsDevCmd.bat" -host_arch=x64 -arch=x86 >nul\n'
                 + f'cl /nologo /EHsc /std:c++20 /I"{repo / "vendor/src/sa-delirium"}" "{cpp}" /Fe:"{exe}"\n', encoding="utf-8")
compiled = subprocess.run(["cmd", "/d", "/c", str(build)], cwd=out, capture_output=True, text=True)
(out / "compile.log").write_text(compiled.stdout + compiled.stderr, encoding="utf-8")
print(compiled.stdout, end="")
compiled.check_returncode()
result = subprocess.run([str(exe), str(fixture)], cwd=out, capture_output=True, text=True)
(out / "test.log").write_text(result.stdout + result.stderr, encoding="utf-8")
print(result.stdout, end="")
evidence = {"status": "passed" if result.returncode == 0 else "failed", "game_launched": False,
            "initial_wall_detection_unchanged": True, "owned_model": 1224,
            "owned_col_sha256": hashlib.sha256(record).hexdigest(),
            "continuity_policy_sha256": hashlib.sha256((repo / "vendor/src/sa-delirium/CoverWallContinuity.hpp").read_bytes()).hexdigest(),
            "result": result.stdout, "exit_code": result.returncode}
(out / "result.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
result.check_returncode()
