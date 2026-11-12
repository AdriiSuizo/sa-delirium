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

from PIL import Image
segments=[]
def sequence(label, buttons=0, seconds=3.0, **axes):
    start=a.current()['frame_counter']
    ammo=reaction(442)
    a.send('audit_pad',buttons=buttons,ms=int((seconds+3)*1000),**axes)
    begin=time.monotonic()
    shots=[]
    for index, moment in enumerate((.15,.45,.8,1.4,2.2)):
        if moment>=seconds:break
        time.sleep(max(0,begin+moment-time.monotonic()))
        if a.send('probe_visual_frame')=='visual_frame_ok':
            target=a.RUN/f'{label}-{index}.jpg'
            with Image.open(a.bridge/f'visual-frame-{a.PID}.bmp') as im:
                im.thumbnail((960,540));im.convert('RGB').save(target,quality=88)
            shots.append(target.name)
    time.sleep(max(0,begin+seconds-time.monotonic()))
    end=a.current()['frame_counter']
    segments.append(dict(label=label,start=start,end=end,ammo_before=ammo,ammo_after=reaction(442),shots=shots))
    a.pad(0,100)
    time.sleep(.5)

for scene in (443,425):
    assert reaction(scene)==1
    reaction(397)
    time.sleep(.7)
    assert reaction(427)==1
    time.sleep(.7)
    axis=reaction(440);assert axis in (-2,-1,1,2),'Cover was lost before test';stick='sx' if abs(axis)==1 else 'sy';press=75 if axis>0 else -75
    sequence(f'{scene}-walk',seconds=.9,**{stick:press})
    sequence(f'{scene}-reverse',seconds=1.2,**{stick:-press})
    if scene==443:
        sequence('low-centre-aim',buttons=16,seconds=4)
        sequence('low-centre-blind',buttons=4,seconds=4)
        assert reaction(1907)==30
        sequence('low-centre-rifle-blind',buttons=4,seconds=4)
    reaction(427)
for scene in (() if '--centre-only' in sys.argv else (426,1905)):
    assert reaction(scene)==1
    reaction(397);time.sleep(.6);assert reaction(427)==1;time.sleep(.7)
    for edge in (1901,1902):
        assert reaction(edge)==1
        sequence(f'{scene}-{edge}-aim',buttons=16,seconds=4)
        time.sleep(1)
        sequence(f'{scene}-{edge}-blind',buttons=4,seconds=4)
    reaction(427)
rows=[r for line in (a.ROOT/'SA/DeliriumSA-local-v3/events.jsonl').read_text().splitlines() if (r:=json.loads(line))['pid']==a.PID]
frames=[dict(kv.split('=') for kv in r['detail'].split()) for r in rows if r['event']=='cover_animation_frame']
(a.RUN/'stability-segments.json').write_text(json.dumps(segments,indent=2))
(a.RUN/'stability-frames.json').write_text(json.dumps(frames,indent=2))
print(a.RUN,flush=True)
