"""Generate an opt-in, process-local concurrent QA bridge; never deploy it."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def prepare(source: Path, destination: Path) -> None:
    source = source.resolve()
    destination = destination.resolve()
    original = source / "AgentBridge.cpp"
    text = original.read_text(encoding="utf-8-sig")
    start = "static bool passiveWindow=false;"
    end = "static DWORD(WINAPI* originalXInput)(DWORD,XINPUT_STATE*)=nullptr;"
    init = "    if(!installPassiveWindow())return;"
    for marker in (start, end, init):
        if text.count(marker) != 1:
            raise ValueError(f"Expected exactly one bridge marker: {marker}")
    if text.index(start) >= text.index(end):
        raise ValueError("Unexpected passive-hook source order")
    if 'ignored_command_for_other_process' not in text:
        raise ValueError("The source bridge lacks PID command isolation")
    generated = (text[:text.index(start)] + '#include "ConcurrentQaIsolation.hpp"\n'
                 + text[text.index(end):])
    generated = generated.replace(init, "    installPassiveWindow(); // Fails closed before game startup if isolation fails.")
    def replace_once(old: str, new: str) -> None:
        nonlocal generated
        if generated.count(old) != 1:
            raise ValueError(f"Expected exactly one timed-pad marker: {old}")
        generated = generated.replace(old, new)

    replace_once('static ULONGLONG auditPadUntil=0;', '''static ULONGLONG auditPadUntil=0;
#include "ConcurrentQaPadClock.hpp"
static qa_pad::Clock auditGamePad;
static bool qaAuditPadHeld();''')
    replace_once('    originalUpdatePads();\n    if(!testSession)return;', '''    originalUpdatePads();
    if(!testSession)return;
    const bool auditHeld=qaAuditPadHeld(); // Native game-clock expiry, every input frame.''')
    replace_once('if(GetTickCount64()<auditPadUntil){', 'if(auditHeld){')
    replace_once('auditPrevious=GetTickCount64()<auditPadUntil?auditPad:std::array<short,24>{};',
                 'auditPrevious=auditHeld?auditPad:std::array<short,24>{};')
    replace_once('#include "PortableNativeImpact.hpp"', '''static bool qaAuditPadHeld(){
    const bool running=gameState()==9;
    const auto now=running?*reinterpret_cast<unsigned*>(0xB7CB84):auditGamePad.lastGame;
    const auto frame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    auto* ped=running?player():nullptr;
    const bool ready=running&&!menu().m_bMenuActive&&!menu().m_bGameNotLoaded&&ped&&field<float>(ped,0x540)>0;
    const bool held=auditGamePad.poll(ready,now,frame,GetTickCount64(),auditPadUntil);
    if(auditGamePad.tracked&&!held)auditPadUntil=0;
    return held;
}
#include "PortableNativeImpact.hpp"''')
    replace_once('    auto*p=player();\n    if(!passiveWindow||gameState()!=9||!p||menu().m_bMenuActive||field<float>(p,0x540)<=0){', '''    std::uint32_t gameDuration=0;
    const auto gameDurationField=press?qa_pad::durationField(text,gameDuration):qa_pad::DurationField::Missing;
    const bool timed=gameDurationField!=qa_pad::DurationField::Missing;
    const bool running=gameState()==9;
    const auto gameNow=running?*reinterpret_cast<unsigned*>(0xB7CB84):auditGamePad.lastGame;
    const auto gameFrame=*reinterpret_cast<unsigned*>(0xB7CB4C);
    if(timed&&(gameDurationField==qa_pad::DurationField::Invalid||!std::isfinite(auditNumber(text,"ms",150)))){
        auditPad={};auditPrevious={};auditPadUntil=0;
        auditGamePad.reject(gameNow,gameFrame,"invalid_game_ms");
        result="audit_pad_invalid_game_ms";return true;
    }
    auto*p=player();
    if(!passiveWindow||!running||!p||menu().m_bMenuActive||(timed&&menu().m_bGameNotLoaded)||field<float>(p,0x540)<=0){
        if(timed){auditPad={};auditPrevious={};auditPadUntil=0;auditGamePad.reject(gameNow,gameFrame,"game_not_ready");}''')
    replace_once('    if(press){\n        auditPad={};', '''    if(press){
        if(timed)auditGamePad.arm(gameDuration);
        else auditGamePad.legacy(gameNow,gameFrame);
        auditPad={};''')
    marker = r'     <<",\"input_isolated\":"<<(testSession&&originalUpdatePads?"true":"false")'
    replace_once(marker, marker + r'''
     <<",\"pad_game_start_ms\":"<<auditGamePad.start
     <<",\"pad_game_end_ms\":"<<auditGamePad.deadline
     <<",\"pad_game_stopped_ms\":"<<auditGamePad.stopped
     <<",\"pad_game_requested_ms\":"<<auditGamePad.requested
     <<",\"pad_game_start_frame\":"<<auditGamePad.startFrame
     <<",\"pad_game_stop_frame\":"<<auditGamePad.stopFrame
     <<",\"pad_game_active\":"<<(auditGamePad.active?"true":"false")
     <<",\"pad_game_pending\":"<<(auditGamePad.pending?"true":"false")
     <<",\"pad_game_stop_reason\":\""<<auditGamePad.reason<<"\""''')
    destination.mkdir(parents=True, exist_ok=False)
    for header in source.glob("*.hpp"):
        shutil.copy2(header, destination / header.name)
    header = Path(__file__).with_name("ConcurrentQaIsolation.hpp")
    shutil.copy2(header, destination / header.name)
    clock_header = Path(__file__).with_name("ConcurrentQaPadClock.hpp")
    shutil.copy2(clock_header, destination / clock_header.name)
    (destination / "AgentBridge.cpp").write_text(generated, encoding="utf-8", newline="\n")
    (destination / "provenance.json").write_text(json.dumps({
        "fixture_only": True,
        "source": str(original),
        "source_sha256": hashlib.sha256(original.read_bytes()).hexdigest(),
        "isolation_header_sha256": hashlib.sha256(header.read_bytes()).hexdigest(),
        "pad_clock_header_sha256": hashlib.sha256(clock_header.read_bytes()).hexdigest(),
        "pad_game_ms": "Optional integer 1..5000, starts on first injected input frame; existing ms <=8000 wall TTL remains authoritative",
        "required_environment": {"GTASA_PASSIVE_TEST_WINDOW": "1", "GTASA_CONCURRENT_QA": "1"},
        "singleton": "CreateEventA exact name: Grand theft auto San Andreas",
        "scope": "Current test process only; no game file, external process, or global input changes",
    }, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    prepare(args.source, args.out)
    print(args.out.resolve())
