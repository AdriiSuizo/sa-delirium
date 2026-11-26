"""Directed live cases for an already running, isolated diagnostic adapter.

Never launches the game, saves, or sends operating-system input. The native
bridge and diagnostic exports enforce free-roam and owned-process boundaries.
This is not a certification of missions, controller input, or frame rate.
"""
import argparse
import json
from pathlib import Path
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--profile', type=Path, required=True)
p.add_argument('--pid', type=int, required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=False)
bridge = a.profile / 'logs/bridge'
runtime = a.profile / 'SA/DeliriumSA-local-v3'


def read(path):
    for _ in range(20):
        try:
            return json.loads(path.read_text(encoding='utf-8-sig'))
        except (OSError, ValueError):
            time.sleep(.05)
    raise RuntimeError('Cannot read live state: ' + str(path))


def command(action, **fields):
    identity = time.time_ns() // 1_000_000
    data = dict(pid=a.pid, id=identity, action=action, **fields)
    (bridge / 'command.json').write_text(json.dumps(data), encoding='ascii')
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        row = read(bridge / 'state.json')
        if row.get('pid') == a.pid and row.get('command_id') == identity:
            return row
        time.sleep(.1)
    raise RuntimeError('No native reply: ' + action)


def snapshot():
    row = read(runtime / 'state.json')
    assert row['pid'] == a.pid and row['ready'] and row['playing'], row
    assert row['errors'] == 0 and not row['on_mission'], row
    return row


initial = read(bridge / 'state.json')
assert initial['pid'] == a.pid and initial['player_present'] and not initial['on_mission']
assert initial['input_isolated'], 'Requires an active, isolated lab bridge session'
assert snapshot()['area'] == 0, 'Load the checkpoint and exit the house first'
results = []
try:
    for name, mode, seconds in [('near-miss', 1914, 7), ('frontal', 1912, 10),
                                ('lateral', 1913, 10), ('cj-high-fall', 340, 12)]:
        reply = command('probe_grove_street')
        assert reply['result'] == 'grove_scene_result_0', reply
        time.sleep(1)
        before = snapshot()
        log = runtime / 'events.jsonl'
        offset = log.stat().st_size
        reply = command('probe_vehicle_reaction', mode=mode)
        prefix = 'vehicle_reaction_result_'
        assert reply['result'].startswith(prefix), reply
        target = int(reply['result'][len(prefix):])
        assert target >= 0, reply
        states = []
        for _ in range(seconds * 2):
            time.sleep(.5)
            states.append(snapshot())
        with log.open('rb') as f:
            f.seek(offset)
            rows = [json.loads(line) for line in f.read().splitlines() if line]
        rows = [r for r in rows if r['pid'] == a.pid]
        failures = [r for r in rows if r['event'] in
                    ('engine_error', 'game_process_fault', 'native_exception')]
        target_events = sorted(set(r['event'] for r in rows if r.get('ref') == target))
        activated = 'damage_activation' in target_events
        required = {
            'near-miss': set(),
            'frontal': {'accepted_vehicle_contact', 'damage_activation', 'native_getup_completed'},
            'lateral': {'accepted_vehicle_contact', 'damage_activation', 'release'},
            'cj-high-fall': {'player_high_fall_started', 'player_high_fall_landed',
                             'native_getup_completed', 'cj_flight_voice', 'cj_ground_audio'},
        }[name]
        missing = sorted(required - set(target_events))
        # Record evidence rather than equating successful scene setup with a hit.
        record = dict(case=name, mode=mode, target=target, reply=reply, before=before,
                      after=states[-1], target_events=target_events,
                      activated=activated, missing_events=missing, faults=failures)
        (a.out / (name + '-states.json')).write_text(json.dumps(states, indent=2))
        (a.out / (name + '-events.jsonl')).write_text('\n'.join(json.dumps(r) for r in rows))
        results.append(record)
        print(json.dumps(dict(case=name, target=target, activated=activated,
                              missing_events=missing, faults=len(failures))), flush=True)
        assert not failures, failures
        assert not missing, missing
        assert name != 'near-miss' or not activated, 'Non-contact pass activated target'
finally:
    (a.out / 'results.json').write_text(json.dumps(results, indent=2))
