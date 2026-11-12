"""Measure terminal speed and two turns at 75% on the Los Santos runway."""
import argparse
import csv
import json
import math
from pathlib import Path
import sys
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('lab', type=Path)
parser.add_argument('models', nargs='*', type=int)
parser.add_argument('--reference', type=Path, action='append', default=[],
                    help='Reuse top speeds from previous terminal runs for a paired comparison')
parser.add_argument('--native-pad', action='store_true',
                    help='Verify the native control path at partial stick and during a launch')
parser.add_argument('--expect-slow', action='store_true',
                    help='Require a majority of wheel samples above 40 ms, with the IV solver active')
parser.add_argument('--corner-damage', action='store_true',
                    help='Verify a native collision against a fixed convex corner')
parser.add_argument('--driving-mode', choices=('classic', 'izzy', 'iv'), default='iv',
                    help='Expected driving mode for the corner-damage test')
parser.add_argument('--launch-model', type=int, action='append', default=[],
                    help='Also measure launches for these models without terminal speed or turns')
args = parser.parse_args()
sys.path.insert(0, str(args.lab.resolve()))
import audit as a

trace = a.ROOT / 'SA/DeliriumSA-local-v3/vehicle-lab.csv'
models = args.models or (429, 480, 445, 466, 567, 596, 524)
reference = {row['model']: row for path in args.reference for row in json.loads(path.read_text())}


def send(action, **values):
    for _ in range(20):
        try:
            return a.send(action, **values)
        except PermissionError:
            time.sleep(.04)
    raise RuntimeError(f'Bridge busy for {action}')


def reaction(mode):
    result = send('probe_vehicle_reaction', mode=mode)
    ref = int(result.removeprefix('vehicle_reaction_result_'))
    if ref < 0:
        raise RuntimeError(f'{mode}: {result}')
    return ref


def trace_rows(lines):
    return [{k: float(v) for k, v in row.items()}
            for row in csv.DictReader(lines) if all(row.values())]


def run(mode, duration, label, required_ms=None):
    reaction(mode)
    began = a.current()['game_time_ms']
    required_ms = duration if required_ms is None else required_ms
    start = time.monotonic()
    rows = []
    while time.monotonic() < start + duration / 1000 + 25:
        if trace.exists():
            data = trace.read_text().splitlines()
            # Drop the last line, which may be incomplete while the game is writing.
            rows = trace_rows(data[:-1])
            if rows and rows[-1]['ms'] >= required_ms - 60:
                a.await_state(lambda s: s['game_time_ms'] >= began + duration + 380, 12)
                data = trace.read_text()
                (a.RUN / f'{label}.csv').write_text(data)
                return trace_rows(data.splitlines())
        time.sleep(.3)
    raise TimeoutError(f'{label}: last sample {rows[-1] if rows else None}')


def heading_delta(end, start):
    return (end - start + 180) % 360 - 180


def slip(row):
    return math.degrees(math.atan2(row['lateral_mps'], row['forward_mps']))


class InterruptedTurn(ValueError):
    """The fixture hit something in the runway instead of completing its turn."""


def turning(rows, top, handbrake):
    turn_end = 3750 if args.native_pad else 4850
    samples = [row for row in rows if 2350 <= row['ms'] < turn_end]
    entry, end = samples[0], samples[-1]
    speeds = [math.hypot(row['forward_mps'], row['lateral_mps']) for row in samples]
    measured_entry = speeds[0]
    if not .73 <= measured_entry / top <= .76:
        raise InterruptedTurn(f'Entry reached {measured_entry / top:.1%} of top speed')
    if any(before - after > 4 for before, after in zip(speeds, speeds[1:])):
        raise InterruptedTurn('Speed fell more than 4 m/s between consecutive samples')
    if args.native_pad:
        if not all(row['from_native_pad'] for row in samples):
            raise InterruptedTurn('CJ left the test car during the turn')
        assert max(abs(row['raw_steer']) for row in samples) > .4
        assert any(row['handbrake'] for row in samples) == handbrake
    iv_steps = int(end['iv_steps'] - entry['iv_steps'])
    fallback_steps = int(end['fallback_steps'] - entry['fallback_steps'])
    slow_share = sum(row['dt'] > .04 for row in samples) / len(samples)
    if args.expect_slow:
        assert slow_share > .5, 'Enable a low frame-rate limit in the test installation'
    peak_roll = max(abs(row['roll']) for row in samples)
    peak_slip = max(abs(slip(row)) for row in samples)
    return dict(entry_kmh=round(measured_entry * 3.6, 1),
                body_turn_deg=round(heading_delta(end['heading'], entry['heading']), 1),
                travel_turn_deg=round(heading_delta(end['heading'] - slip(end),
                                                     entry['heading'] - slip(entry)), 1),
                peak_slip_deg=round(peak_slip, 1),
                exit_kmh=round(speeds[-1] * 3.6, 1),
                minimum_wheels=min(row['grounded'] for row in samples),
                peak_roll_deg=round(peak_roll, 1), iv_steps=iv_steps,
                fallback_steps=fallback_steps, slow_sample_share=round(slow_share, 2),
                stable=iv_steps > 0 and fallback_steps <= .05 * iv_steps and peak_roll < 35 and peak_slip < 45)


def measure_turn(mode, label, top, handbrake=False):
    for attempt in range(3):
        filename = label if attempt == 0 else f'{label}-retry-{attempt}'
        try:
            required_ms = 3750 if args.native_pad else None
            return turning(run(mode, 5350, filename, required_ms), top, handbrake)
        except InterruptedTurn as exc:
            print(f'{filename}: {exc}', flush=True)
    raise RuntimeError(f'{label}: runway remained obstructed')


def measure_launch(model):
    rows = run(6000 + model, 6800, f'{model}-launch', 6740)
    # After the stationary phase SA can still be braking a small reverse
    # motion. Time the launch from the first forward throttle the game accepts.
    start = next(row for row in rows if row['ms'] >= 500 and row['gas'] > .9)
    assert start['ms'] <= 1000 and abs(start['forward_mps']) < 1, (model, start)
    powered = [row for row in rows if start['ms'] <= row['ms'] <= start['ms'] + 5000]
    assert all(row['from_native_pad'] and row['gas'] > .9 for row in powered), model
    assert max(abs(row['steer']) for row in powered) < .02, model
    assert powered[-1]['ms'] >= start['ms'] + 4900, model
    return {f'kmh_at_{seconds}s': round(min(powered, key=lambda row: abs(row['ms'] - (start['ms']+seconds*1000)))
                                        ['forward_mps'] * 3.6, 1) for seconds in (2, 4, 5)}


for _ in range(100):
    try:
        if json.loads((a.bridge / 'state.json').read_text()).get('pid') == a.PID:
            break
    except (OSError, ValueError):
        pass
    time.sleep(.25)
else:
    raise RuntimeError('Bridge did not start')

a.await_state(lambda s: s.get('player') and s['game_state'] == 9 and not s['menu_active'] and
              not s['game_not_loaded'] and not s['script_mission_active'] and
              not s['audit_cutscene'] and s.get('audit_control') == 0, 40)
for _ in range(20):
    state = a.current()
    area = state['current_area']
    transition = state['entry_exit']['transition']
    if area == 0 and transition == 0:
        break
    if area == 0 and transition == 3:
        send('probe_house_clear_threshold')
    if area == 3 and transition == 0:
        send('probe_house_transition')
    time.sleep(1)
a.await_state(lambda s: s.get('current_area') == 0 and s['entry_exit']['transition'] == 0, 15)
if args.corner_damage:
    assert reaction(384) == 1, 'Enable the deformation toggle in the test installation'
    assert reaction(385) == ('classic', 'izzy', 'iv').index(args.driving_mode)
    assert reaction(386) == (args.driving_mode == 'izzy')
    assert send('probe_grove_street') == 'grove_scene_result_0'
    time.sleep(2)
    target = reaction(389)
    until = time.monotonic() + 8
    rows = []
    while time.monotonic() < until:
        time.sleep(.2)
        data = (a.ROOT/'SA/DeliriumSA-local-v3/events.jsonl').read_bytes().splitlines()
        rows = [row for line in data[:-1] if (row := json.loads(line))['pid'] == a.PID and row['ref'] == target]
        if any(row['event'] == 'vehicle_deformation_corner' for row in rows):
            break
    (a.RUN/'corner-damage.json').write_text(json.dumps(rows, indent=2))
    assert any(row['event'] == 'vehicle_deformation_corner' for row in rows), rows[-8:]
    reaction(388)
    time.sleep(.5)
    assert reaction(381) > 0, 'No deformed panel was drawn'
    assert reaction(382) == 1, 'The untouched car also deformed'
    assert a.shot('corner-damage') == 'visual_frame_ok'
    assert reaction(383) == 1, 'Repair did not reset the dents'
    print(json.dumps([row for row in rows if row['event']=='vehicle_deformation_corner']), flush=True)
    sys.exit(0)

results = []
for model in models:
    if model in reference:
        measured = round(reference[model]['top_kmh'] / .36)
        assert reaction(1000000 + model * 1000 + measured) == model
        top = measured * .1
        top_min = reference[model]['top_min_kmh']
    else:
        top_run = run(3000 + model, 52000, f'{model}-terminal')
        tail = [row['forward_mps'] for row in top_run if row['ms'] >= 42000]
        top = max(tail)
        assert top > 18 and min(tail) >= .94 * top, (model, min(tail), top)
        top_min = round(min(tail) * 3.6, 1)
    result = dict(model=model, top_kmh=round(top * 3.6, 1), top_min_kmh=top_min,
                  steering=measure_turn(4000 + model, f'{model}-steer', top),
                  handbrake=measure_turn(5000 + model, f'{model}-handbrake', top, True))
    if args.native_pad:
        result['launch'] = measure_launch(model)
    results.append(result)
    (a.RUN / 'vehicle-speed-results.json').write_text(json.dumps(results, indent=2))
    print(json.dumps(results[-1]), flush=True)
assert all(r[mode]['stable'] for r in results for mode in ('steering', 'handbrake'))
if args.launch_model:
    launches = {model: measure_launch(model) for model in args.launch_model}
    (a.RUN / 'vehicle-launch-results.json').write_text(json.dumps(launches, indent=2))
    print(json.dumps(launches), flush=True)
