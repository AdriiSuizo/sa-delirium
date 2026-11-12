"""Cover aiming and native death pickup checks in an isolated free-roam session."""
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(sys.argv[1]).resolve()))
import audit as a


def reaction(mode):
    result = a.send('probe_vehicle_reaction', mode=mode)
    print(mode, result, flush=True)
    return int(result.removeprefix('vehicle_reaction_result_'))


deadline = time.monotonic() + 40
while time.monotonic() < deadline:
    s = a.observer.observe(a.PID)
    if (s.get('player') and s.get('game_state') == 9 and not s.get('menu_active') and
        not s.get('game_not_loaded') and not s.get('script_mission_active') and
        not s.get('audit_cutscene') and s.get('audit_control') == 0):
        break
    time.sleep(.3)
else:
    raise RuntimeError(s)
if s.get('current_area') == 3:
    a.send('probe_house_transition')
    a.await_state(lambda s: s.get('current_area') == 0 and s.get('entry_exit', {}).get('transition') in (0, 3), 20)
    if a.current()['entry_exit']['transition'] == 3:
        a.send('probe_house_clear_threshold')
a.await_state(lambda s: s.get('current_area') == 0 and
              s.get('entry_exit', {}).get('transition') == 0 and not s.get('script_mission_active'), 15)
for scene in (426, 1905):
    assert reaction(scene) == 1
    reaction(397)
    time.sleep(.7)
    assert reaction(427) == 1
    time.sleep(.7)
    a.shot(f'{scene}-covered')
    for edge in (1901, 1902):
        assert reaction(edge) == 1
        a.send('audit_pad', buttons=0, ms=450, rx=65 if edge == 1901 else -65)
        time.sleep(.6)
        assert reaction(1900) & 4
        a.shot(f'{scene}-{edge}-before-aim')
        a.pad(16, 6000)
        time.sleep(.1)
        assert reaction(438) == 1
        for n in range(3):
            time.sleep(.2)
            assert reaction(1900) == 15, 'Must aim and expose the edge without holding fire'
            a.shot(f'{scene}-{edge}-aim-{n}')
        a.pad(0, 100)
        time.sleep(1.4)
        assert reaction(1900) == 5, 'Must return to the same corner'
    assert reaction(427) == 0
assert reaction(400) >= 0
time.sleep(.3)
victim = reaction(1906)
time.sleep(2)
assert reaction(1904) >= 2, 'Both native death weapons must land'
a.shot('death-weapons')
rows = [row for line in (a.ROOT / 'SA/DeliriumSA-local-v3/events.jsonl').read_text().splitlines()
        if (row := json.loads(line))['pid'] == a.PID]
assert sum(r['event'] == 'death_weapon_dropped' and r['ref'] == victim for r in rows) == 2
assert not any(r['event'] == 'engine_error' for r in rows)
frames = [dict(kv.split('=') for kv in r['detail'].split()) for r in rows if r['event'] == 'cover_animation_frame']
(a.RUN / 'aim-frames.json').write_text(json.dumps(frames, indent=2))
(a.RUN / 'aim-and-pickups-result.json').write_text(json.dumps(dict(
    status='passed', frames=len(frames), victim=victim, death_pickups=2,
    cases=['low-left', 'low-right', 'high-left', 'high-right', 'native-fatal-shot']), indent=2))
print(str(a.RUN), flush=True)
