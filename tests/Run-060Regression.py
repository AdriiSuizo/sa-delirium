"""Compile production policies and regression fixtures in an MSVC x86 prompt.

No game process is started. Results are scoped to the tested native policies.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
runtime = repo / 'vendor/src/sa-delirium'
for name in ('InteriorPedArea', 'CjRecoveryTraffic'):
    subprocess.run([sys.executable, repo / f'tests/Test-{name}.py', runtime, out], check=True)
sources = sorted((repo / 'tests/release060').glob('*.cpp'))
sources += [out / 'interior_test.cpp', out / 'TestTraffic.cpp']
results = []
for source in sources:
    exe = out / (source.stem + '.exe')
    commands = [['cl', '/nologo', '/EHsc', '/std:c++20', '/O2', '/MT', '/I' + str(runtime),
                 str(source), '/Fe:' + str(exe)],
                [str(exe), *(['1.25', '1.75'] if source.stem == 'check_recovery' else [])]]
    status = 'passed'
    for command in commands:
        result = subprocess.run(command, cwd=out, capture_output=True, text=True)
        with (out / (source.stem + '.log')).open('a', encoding='utf-8') as log:
            log.write(result.stdout + result.stderr)
        if result.returncode:
            print(result.stdout + result.stderr)
            status = 'failed'
            break
    results.append({'case': source.stem, 'status': status})
    print(source.stem + ': ' + status, flush=True)
(out / 'results.json').write_text(json.dumps({'scope': 'native-policy-fixtures-not-gameplay',
                                            'results': results}, indent=2))
sys.exit(any(row['status'] != 'passed' for row in results))
