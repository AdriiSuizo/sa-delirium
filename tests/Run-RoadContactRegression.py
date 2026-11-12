"""Run native free-roam road and melee fixtures in a started QA case."""
import argparse
import json
from pathlib import Path
from statistics import median, quantiles
import sys
import time

parser = argparse.ArgumentParser()
parser.add_argument('lab', type=Path)
parser.add_argument('--baseline', action='store_true')
parser.add_argument('--mode', type=int, action='append')
parser.add_argument('--melee', choices=('punch', 'kick', 'receive'),
                    help='Run one melee fixture in a fresh game session')
args = parser.parse_args()
sys.path.insert(0, str(args.lab.resolve()))
import audit as a

def send(action, **kwargs):
    for _ in range(20):
        try:
            return a.send(action, **kwargs)
        except PermissionError:
            time.sleep(.04)
    raise RuntimeError(f'Bridge busy: {action}')

def outdoor(s):
    return s.get('current_area') == 0 and s.get('entry_exit', {}).get('transition') == 0 and not s.get('script_mission_active')

for _ in range(100):
    try:
        snapshot=json.loads((a.bridge/'state.json').read_text())
        if snapshot.get('pid')==a.PID:
            break
    except (OSError,ValueError):
        pass
    time.sleep(.25)
else:
    raise RuntimeError('Bridge did not start')

a.await_state(lambda s: s.get('game_state') == 9 and not s.get('menu_active') and
              not s.get('game_not_loaded') and (s.get('player') or {}).get('health', 0)>0, 45)
for _ in range(28):
    s = a.current()
    if outdoor(s):
        break
    area, transition = s.get('current_area'), s.get('entry_exit', {}).get('transition')
    if area==3 and transition==0:
        send('probe_house_transition')
    elif area==0 and transition==3:
        send('probe_house_clear_threshold')
    time.sleep(1)
else:
    raise RuntimeError('Could not get to free roam')
assert send('probe_grove_street') == 'grove_scene_result_0'
time.sleep(2)
events=a.ROOT/'SA/DeliriumSA-local-v3/events.jsonl'

def read_rows(path):
    with path.open('rb') as f:
        for line in f:
            try:
                yield json.loads(line)
            except ValueError:
                continue


def fixture(mode):
    result = send('probe_vehicle_reaction', mode=mode)
    ref = int(result.rsplit('_', 1)[-1])
    assert ref >= 0, result
    return ref


def melee_report(test):
    def watch(predicate, seconds=8):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            rows = [row for row in read_rows(events) if row.get('pid') == a.PID]
            if any(predicate(row) for row in rows):
                return rows
            time.sleep(.22)
        raise AssertionError('Melee event missing in ' + str(a.RUN))

    def clean_contact(row):
        return row['event'] == 'combat_contact' and 'blocked=0' in row['detail']

    if test == 'punch':
        target = fixture(165)
        time.sleep(.8)
        fixture(152)

        def accepts(row):
            return row['ref'] == target and clean_contact(row)

        rows = watch(accepts)
        report = dict(punch=next(filter(accepts, rows)))
    elif test == 'kick':
        target = fixture(1909)
        time.sleep(.8)
        fixture(153)

        def pushed(row):
            return row['ref'] == target and row['event'] == 'melee_scheduled_impulse' and (
                'original_blow=1' in row['detail'])

        rows = watch(pushed)
        kick = next(filter(pushed, rows))
        assert 'iz=0.000000' in kick['detail'], kick
        report = dict(kick=kick)
    else:
        attacker = fixture(161)

        def incoming(row):
            return clean_contact(row) and f'attacker={attacker} ' in row['detail']

        hit = next(filter(incoming, watch(incoming)))

        def reacts(row):
            return row['ref'] == hit['ref'] and row['time_ms'] >= hit['time_ms'] and (
                row['event'] in {'melee_motion_response', 'combat_reaction_motor_start'} or
                row['event'] == 'combat_action' and '/hit_' in row['detail'])

        rows = watch(reacts)
        report = dict(npc_hit=hit, cj_reaction=next(filter(reacts, rows)))
    assert not any(r['event'] == 'engine_error' for r in rows)
    (a.RUN/'melee-contact-results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report), flush=True)


if args.melee:
    melee_report(args.melee)
    sys.exit(0)

results=[]
for mode in args.mode or (1912,1913,1914,1915):
    ref = fixture(mode)
    start=a.current()
    end=a.await_state(lambda s:s['game_time_ms']>=start['game_time_ms']+3000,20)
    rows=[r for r in read_rows(events) if r.get('pid')==a.PID and r.get('ref')==ref]
    setup=next(r for r in reversed(rows) if r.get('event')=='probe_road_contact_case')
    start_ms=setup['time_ms']
    relevant={'probe_road_contact_case','accepted_vehicle_contact','road_solved_reaction',
              'approaching_vehicle','native_road_damage_animation_replaced','damage_activation',
              'native_damage_decision','damage_ended','native_ped_kick_removed'}
    rows=[r for r in rows if r['time_ms']>=start_ms and r['event'] in relevant]
    (a.RUN/f'road-{mode}.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
    contact=next((r for r in rows if r['event']=='accepted_vehicle_contact'),None)
    solved=next((r for r in rows if r['event']=='road_solved_reaction'),None)
    activates=[r for r in rows if r['event']=='damage_activation']
    approaches=[r for r in rows if r['event']=='approaching_vehicle']
    report=dict(mode=mode,ref=ref,contact=contact,solved=solved,activations=len(activates),previews=len(approaches),
                first_frame=start['frame_counter'], last_frame=end['frame_counter'],
                simulation_frames_per_second=(end['frame_counter']-start['frame_counter'])*1000/(end['game_time_ms']-start['game_time_ms']))
    if contact and 'vehicle_local_normal=' in contact['detail']:
        report['normal_local']=[float(x) for x in contact['detail'].split('vehicle_local_normal=')[1].split()[0].split(',')]
    results.append(report)
    print(json.dumps(report),flush=True)

profile=a.ROOT/'logs'/f'{a.case["name"]}-cost.jsonl'
if profile.exists():
    rows=[r for r in read_rows(profile) if r.get('scope')=='process']
    for report in results:
        samples=[r for r in rows if report['first_frame']<=r['frame']<=report['last_frame']]
        if len(samples)>10:
            costs=[r['ms'] for r in samples]
            intervals=[b['qpc_ms']-c['qpc_ms'] for c,b in zip(samples,samples[1:]) if b['frame']==c['frame']+1]
            report.update(n=len(samples), process_p50_ms=median(costs), process_p95_ms=quantiles(costs, n=100)[94],
                          interval_p50_ms=median(intervals), interval_p95_ms=quantiles(intervals, n=100)[94],
                          measured_fps=1000 * len(intervals) / sum(intervals))
(a.RUN/'road-contact-results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
if not args.baseline:
    for report in results:
        mode=report['mode']
        if mode==1914:
            assert report['contact'] is None and report['solved'] is None, report
            assert not report['activations'] and not report['previews'], report
            rows = json.loads((a.RUN/f'road-{mode}.json').read_text())
            assert not any(r['event'] == 'native_damage_decision' for r in rows), rows
        else:
            assert report['contact'] and report['solved'] and report['activations']==1,report
            x,y,z=report['normal_local']
            assert (abs(x)>.64) if mode==1913 else (abs(y)>abs(x)),report
            if mode==1915:
                assert report['previews']==1,report
