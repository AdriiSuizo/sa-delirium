#pragma once
// Actual HAnim tags, NOT the misleading legacy plugin-sdk ePedBones names.
// gta-reversed/source/game_sa/Enums/eBoneTag.h and CJ's skin hierarchy agree:
// jaw/brows are children of HEAD (5); thighs are children of PELVIS (1).
namespace saBone {
constexpr int root=0,pelvis=1,spine=2,chest=3,neck=4,head=5;
constexpr int leftBrow=6,rightBrow=7,jaw=8;
constexpr bool face(int id){return id==leftBrow||id==rightBrow||id==jaw;}
}
