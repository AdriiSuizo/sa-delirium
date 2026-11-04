"""PID-scoped validation; never sends desktop input and never saves a game."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import time
import psutil


def read(path):
    try:
        return json.loads(path.read_text(encoding='utf-8-sig'))
    except (OSError, ValueError):
        return {}


def sha(path):
    return hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--target', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--bridge', type=Path)
    ap.add_argument('--steam', action='store_true')
    args = ap.parse_args()
    target = args.target.resolve()
    game = target / 'SA'
    args.out.mkdir(parents=True, exist_ok=False)
    assert not [p for p in psutil.process_iter(['name']) if (p.info['name'] or '').lower() in ('gta_sa.exe', 'gta-sa.exe')]
    before_pids = set(psutil.pids())
    bridge = game / 'scripts/GameTestBridge.asi'
    assert not bridge.exists()
    save = game / 'userfiles/GTASAsf2.b'
    save_sha, save_mode = sha(save), save.stat().st_mode
    env = os.environ.copy()
    env['GTASA_TEST_SAVE_SLOT'] = '2'
    if args.bridge:
        shutil.copy2(args.bridge, bridge)
        env['GTASA_PASSIVE_TEST_WINDOW'] = '1'
    # Actual Steam's existing client supplies its normal environment. For that
    # case use no injected test bridge or synthetic commands.
    assert not (args.steam and args.bridge)
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    owned = []
    report = {'target': str(target), 'steam_launch': args.steam, 'bridge': bool(args.bridge),
              'source_save_sha256': save_sha, 'observations': []}
    try:
        if args.bridge:
            save.chmod(save_mode & ~stat.S_IWRITE)
        if args.steam:
            subprocess.run([r'C:\Program Files (x86)\Steam\steam.exe', '-applaunch', '12120'], startupinfo=startup, check=True)
        else:
            launcher = subprocess.Popen([str(target / 'gta-sa.exe')], cwd=target, env=env, startupinfo=startup)
            owned.append(psutil.Process(launcher.pid))
        end = time.monotonic() + 90
        child = None
        while time.monotonic() < end:
            for process in psutil.process_iter(['pid', 'name', 'exe']):
                if process.pid not in before_pids and (process.info['name'] or '').lower() in ('gta_sa.exe', 'gta-sa.exe'):
                    expected = game / 'gta_sa.exe' if (process.info['name'] or '').lower() == 'gta_sa.exe' else target / 'gta-sa.exe'
                    assert Path(process.info['exe']).resolve() == expected.resolve()
                    if process.pid not in {p.pid for p in owned}:
                        owned.append(process)
                    if process.name().lower() == 'gta_sa.exe':
                        child = process
            if child:
                sessions = list((target / 'logs/manual-sessions').glob(f'*-p{child.pid}'))
                if sessions:
                    session = max(sessions, key=lambda p: p.stat().st_mtime)
                    events = (session / 'events.jsonl').read_text(encoding='utf-8', errors='replace')
                    if '"event":"initialized"' in events:
                        break
                    if 'unsupported_executable' in events or '_refused"' in events:
                        raise RuntimeError('Adapter refused native startup')
            time.sleep(.25)
        else:
            raise RuntimeError('No new initialized game process')
        report['pid'] = child.pid
        report['session'] = str(session)
        report['modules'] = sorted({m.path for m in child.memory_maps() if m.path.lower().endswith(('.asi', '.cleo', '.dll'))})
        assert not any('v_hud_by' in p.lower() for p in report['modules'])
        assert not any('sastandalone.asi' in p.lower() for p in report['modules'])
        assert any(p.lower().endswith('deliriumsa.asi') for p in report['modules'])
        if args.bridge:
            folder = target / 'logs/bridge'
            # The adapter initializes before the first-run renderer/window.
            # Wait for the bridge's main-thread timer, not merely its loaded DLL.
            bridge_deadline = time.monotonic() + 90
            while time.monotonic() < bridge_deadline:
                if read(folder / 'state.json').get('pid') == child.pid:
                    break
                if not child.is_running():
                    raise RuntimeError('Game exited before bridge readiness')
                time.sleep(.25)
            else:
                raise RuntimeError('Main-thread bridge readiness timeout')
            def command(action, mode=0):
                command_id = time.time_ns() // 1_000_000
                body = {'pid': child.pid, 'id': command_id, 'action': action, 'mode': mode}
                temp = folder / 'command.json.tmp'
                temp.write_text(json.dumps(body), encoding='ascii')
                os.replace(temp, folder / 'command.json')
                until = time.monotonic() + 12
                while time.monotonic() < until:
                    state = read(folder / 'state.json')
                    if state.get('pid') == child.pid and state.get('command_id') == command_id:
                        report.setdefault('commands', []).append({'action': action, 'mode': mode, 'state': state})
                        return state
                    if not child.is_running():
                        raise RuntimeError('Owned game exited')
                    time.sleep(.1)
                raise RuntimeError('Command not acknowledged: ' + action)
            command('load_test_save')
            end = time.monotonic() + 120
            while time.monotonic() < end:
                state = read(folder / 'state.json')
                if state.get('game_state') == 9 and state.get('player_present') and not state.get('menu_active'):
                    break
                if state.get('result') == 'unexpected_load_menu':
                    screenshot = command('probe_visual_frame')
                    if screenshot.get('result') == 'visual_frame_ok':
                        from PIL import Image
                        Image.open(folder / f'visual-frame-{child.pid}.bmp').convert('RGB').resize((1280, 720)).save(args.out / 'load-menu.png')
                    raise RuntimeError('Unexpected native load menu: ' + json.dumps(state))
                if not child.is_running():
                    raise RuntimeError('Game exited during checkpoint load')
                time.sleep(.25)
            else:
                raise RuntimeError('Checkpoint load timeout: ' + json.dumps(state))
            start_state = command('observe')
            physics = read(game / 'DeliriumSA/state.json')
            if physics.get('area') == 3:
                command('probe_house_transition')
                time.sleep(8)
            time.sleep(4)
            command('probe_visual_scene', 0)
            command('probe_visual_camera')
            command('probe_vehicle_reaction', 150)
            time.sleep(.7)
            command('probe_vehicle_reaction', 152)
            for _ in range(15):
                time.sleep(1)
                report['observations'].append(read(game / 'DeliriumSA/state.json'))
            state = command('probe_visual_frame')
            if state['result'] == 'visual_frame_ok':
                from PIL import Image
                Image.open(folder / f'visual-frame-{child.pid}.bmp').convert('RGB').resize((1280, 720)).save(args.out / 'game.png')
            final = command('observe')
            assert final['frame'] > start_state['frame'] and final['game_time_ms'] > start_state['game_time_ms']
            assert final['input_isolated'] and final['health'] > 0
            physics = report['observations'][-1]
            assert physics.get('pid') == child.pid and physics.get('ready') and physics.get('world_triangles', 0) > 0
            assert physics.get('errors', 1) == 0, physics
            assert any(p.get('physics_steps', 0) > 0 for p in report['observations'])
            report['final'] = final
        else:
            time.sleep(8)
            assert child.is_running()
        report['status'] = 'scoped_pass'
        shutil.copy2(session / 'session.json', args.out / 'session.json')
        shutil.copy2(session / 'events.jsonl', args.out / 'events.jsonl')
        shutil.copy2(game / 'modloader/modloader.log', args.out / 'modloader.log')
    except BaseException as error:
        report['status'] = 'failed'
        report['error'] = repr(error)
        raise
    finally:
        for process in reversed(owned):
            try:
                expected = game / 'gta_sa.exe' if process.name().lower() == 'gta_sa.exe' else target / 'gta-sa.exe'
                assert Path(process.exe()).resolve() == expected.resolve()
                process.kill()
                process.wait(timeout=15)
            except psutil.NoSuchProcess:
                pass
        if args.bridge and bridge.exists():
            assert sha(bridge) == sha(args.bridge)
            bridge.unlink()
        save.chmod(save_mode)
        report['save_preserved'] = sha(save) == save_sha
        assert report['save_preserved']
        (args.out / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        print(json.dumps({'status': report.get('status'), 'report': str(args.out / 'report.json'), 'error': report.get('error')}), flush=True)


if __name__ == '__main__':
    main()
