"""Isolated fixture bridge: native first-new-game action, no desktop input."""
from pathlib import Path
import argparse,hashlib,json
import shutil

ap=argparse.ArgumentParser()
ap.add_argument('--project',type=Path,required=True)
ap.add_argument('--out',type=Path,required=True)
args=ap.parse_args();project=args.project.resolve();out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
original=project/'src/sa-test-bridge/AgentBridge.cpp'
s=original.read_text(encoding='utf-8-sig')
include='static void probeVisualScene(int scene){'
assert s.count(include)==1
s=s.replace(include,'#include "PortableNativeImpact.hpp"\n'+include)
shutil.copy2(Path(__file__).with_name('PortableNativeImpact.hpp'),out/'PortableNativeImpact.hpp')
old='out.parent_path().parent_path()/"game"/"userfiles"'
assert s.count(old)==1
s=s.replace(old,'[](){wchar_t path[32768];GetModuleFileNameW(nullptr,path,32768);return fs::path(path).parent_path();}()/"userfiles"')
s=s.replace('static bool pendingLoad=false;','static bool pendingLoad=false;static bool firstNewGame=false;')
marker='    if(text.find("\\\"load_test_save\\\"")==std::string::npos)'
assert s.count(marker)==1
s=s.replace(marker,'''    if(text.find("\\\"probe_native_ammo\\\"")!=std::string::npos){nativeAmmoStatus();return;}
    if(text.find("\\\"prepare_native_impact\\\"")!=std::string::npos){prepareNativeImpact();return;}
    if(text.find("\\\"fire_native_impact\\\"")!=std::string::npos){fireNativeImpact();return;}
    if(text.find("\\\"start_new_game\\\"")!=std::string::npos){
        if(gameState()==9&&!menu().m_bGameNotLoaded){result="rejected_loaded_game";return;}
        firstNewGame=true;pendingLoad=true;requestedAt=GetTickCount64();result="first_new_game_pending";return;
    }
'''+marker)
marker='    if(m.m_nCurrentMenuPage==34||m.m_nCurrentMenuPage==1){'
assert s.count(marker)==1
s=s.replace(marker,'''    if(firstNewGame){
        const auto page=m.m_nCurrentMenuPage;
        if(page==34){reinterpret_cast<char(__thiscall*)(CMenuManager*,char)>(0x573680)(&m,1);result="opened_new_game_menu";return;}
        if(page==1||page==6||page==7){
            for(int i=0;i<NUM_ENTRIES;i++){
                const auto& entry=screens()[page].m_aEntries[i];
                const auto text=std::string(entry.m_EntryName,strnlen(entry.m_EntryName,8));
                if((page==1&&entry.m_nAction==MENUACTION_NEWGAME)||(page==6&&entry.m_nAction==14)||(page==7&&text=="FEN_NGX")){
                    m.m_nCurrentMenuEntry=i;char exit=0;
                    reinterpret_cast<void(__thiscall*)(CMenuManager*,char,char*,char)>(0x576FE0)(&m,0,&exit,1);
                    result="selected_native_new_game_action";return;
                }
            }
        }
        result="waiting_for_native_first_game";return;
    }
'''+marker)
(out/'AgentBridge.cpp').write_text(s,encoding='utf-8',newline='\n')
(out/'provenance.json').write_text(json.dumps({'original_sha256':hashlib.sha256(original.read_bytes()).hexdigest(),'fixture_only':True,'native_menu_references':['FrontendScreensPC.h screens1/6/7','MenuManager_Process.cpp actions10/14'],'changes':['Resolve actual executable root','Native standard new-game UI actions; no mission or save flag edits']}),encoding='utf-8')
print(out)
