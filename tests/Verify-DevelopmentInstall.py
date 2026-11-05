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
    ap.add_argument('--resource-dir', default='DeliriumSA')
    ap.add_argument('--iso-runtime', action='store_true')
    ap.add_argument('--new-game', action='store_true', help='First installation: native new-game menu, no save import or physical fixture')
    ap.add_argument('--without-izzy', action='store_true')
    ap.add_argument('--izzy-sha256', help='Require the exact approved author ASI to be installed and loaded')
    ap.add_argument('--fxs-sha256', help='Require the exact selected effects ASI')
    ap.add_argument('--fire-effects', action='store_true', help='Fire actual owned-game pistol rounds and verify ammunition decreases')
    ap.add_argument('--require-module',action='append',default=[],help='Wait for a selected extra plugin to be loaded')
    ap.add_argument('--trainer-menu',action='store_true',help='Exercise trainer menu through its own process-local XInput samples; no desktop input')
    ap.add_argument('--scenario', choices=['melee', 'bike-shot', 'bicycle-shot', 'car-contact', 'native-shot'], default='melee')
    args = ap.parse_args()
    assert not args.trainer_menu or args.bridge,'Trainer menu case requires an isolated native bridge'
    target = args.target.resolve()
    game = target / 'SA'
    args.out.mkdir(parents=True, exist_ok=False)
    assert not [p for p in psutil.process_iter(['name']) if (p.info['name'] or '').lower() in ('gta_sa.exe', 'gta-sa.exe')]
    before_pids = set(psutil.pids())
    bridge = game / 'scripts/GameTestBridge.asi'
    assert not bridge.exists()
    if args.without_izzy:
        assert not list((game / 'modloader').rglob('LivelyVehiclePhysics.asi'))
    save = game / 'userfiles/GTASAsf2.b'
    save_sha, save_mode = (sha(save), save.stat().st_mode) if save.exists() else (None, None)
    if args.new_game:
        assert args.bridge and not list((game / 'userfiles').glob('GTASAsf*.b')), 'Use a fresh profile without any owner saves'
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
              'source_save_sha256': save_sha, 'observations': [], 'scenario': args.scenario,
              'adapter_sha256': sha(game / 'scripts/DeliriumSA.asi')}
    if args.iso_runtime:
        report['sdk_sha256'] = sha(game / 'scripts/sa_delirium_bootstrap.dll')
    try:
        if args.bridge and save.exists():
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
        if args.without_izzy:
            assert not any(p.lower().endswith('livelyvehiclephysics.asi') for p in report['modules'])
        if args.izzy_sha256:
            # Delirium initializes before Mod Loader finishes its ASI stage.
            # Wait for the real module, rather than treating that early snapshot
            # as the complete loaded profile.
            izzy_deadline = time.monotonic() + 30
            while time.monotonic() < izzy_deadline:
                assert child.is_running(), 'Game exited during Mod Loader startup'
                report['modules'] = sorted({m.path for m in child.memory_maps() if m.path.lower().endswith(('.asi', '.cleo', '.dll'))})
                izzy = [Path(p) for p in report['modules'] if p.lower().endswith('livelyvehiclephysics.asi')]
                if izzy:
                    break
                time.sleep(.25)
            assert len(izzy) == 1 and sha(izzy[0]) == args.izzy_sha256, 'Author ASI missing, duplicated or changed'
            report['izzy_sha256'] = sha(izzy[0])
        needed_modules=args.require_module+(['FxsFuncs.asi'] if args.fxs_sha256 else [])
        if needed_modules:
            deadline=time.monotonic()+45
            while time.monotonic()<deadline:
                report['modules']=sorted({m.path for m in child.memory_maps() if m.path.lower().endswith(('.asi','.cleo','.dll'))})
                loaded={Path(n).name.lower() for n in report['modules']}
                if all(n.lower() in loaded for n in needed_modules):break
                assert child.is_running(),'Game exited while loading selected extras'
                time.sleep(.25)
            else:raise RuntimeError('Required extras not loaded: '+repr([n for n in needed_modules if n.lower() not in loaded]))
            report['required_modules']=needed_modules
        if args.fxs_sha256:
            effects=[Path(p) for p in report['modules'] if p.lower().endswith('fxsfuncs.asi')]
            assert len(effects)==1 and sha(effects[0])==args.fxs_sha256
            report['fxs_sha256']=sha(effects[0])
        assert any(p.lower().endswith('deliriumsa.asi') for p in report['modules'])
        if args.iso_runtime:
            assert any(p.lower().endswith('sa_delirium_bootstrap.dll') for p in report['modules'])
            assert not any(p.lower().endswith('delirium_physics.dll') for p in report['modules'])
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
            def command(action, mode=0, **fields):
                command_id = time.time_ns() // 1_000_000
                body = {'pid': child.pid, 'id': command_id, 'action': action, 'mode': mode}
                body.update(fields)
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
            command('start_new_game' if args.new_game else 'load_test_save')
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
            if args.new_game:
                for _ in range(15):
                    time.sleep(1)
                    report['observations'].append(read(game / args.resource_dir / 'state.json'))
                state = command('probe_visual_frame')
                if state.get('result') == 'visual_frame_ok':
                    from PIL import Image
                    Image.open(folder / f'visual-frame-{child.pid}.bmp').convert('RGB').resize((1280, 720)).save(args.out / 'first-game.png')
                final = command('observe')
                assert final['frame'] > start_state['frame'] and final['game_time_ms'] > start_state['game_time_ms']
                assert final['player_present'] and final['health'] > 0 and final['input_isolated']
                assert report['observations'][-1].get('errors', 1) == 0
                assert not list((game / 'userfiles').glob('GTASAsf*.b'))
                report.update(status='first_new_game_intro_smoke', final=final)
                shutil.copy2(session / 'events.jsonl', args.out / 'events.jsonl')
                return
            physics = read(game / args.resource_dir / 'state.json')
            if physics.get('area') == 3:
                # A just-loaded checkpoint can still be in a natural phone
                # event. Let the native event finish; never clear mission flags.
                boundary_deadline = time.monotonic() + 90
                while time.monotonic() < boundary_deadline:
                    transition = command('probe_house_transition')
                    if transition.get('result') == 'native_house_transition_started':
                        break
                    if transition.get('result') != 'transition_rejected_on_mission':
                        raise RuntimeError('Native house transition refused: ' + json.dumps(transition))
                    time.sleep(1)
                else:
                    raise RuntimeError('Native mission/phone event did not release the house transition')
                while time.monotonic() < boundary_deadline:
                    physics = read(game / args.resource_dir / 'state.json')
                    if physics.get('area') == 0 and physics.get('playing'):
                        break
                    time.sleep(.25)
                else:
                    raise RuntimeError('Native house transition did not reach the outdoor world')
            time.sleep(4)
            if args.scenario == 'native-shot':
                # The native enex state remains3 until CJ leaves its trigger.
                # Move away through the existing bounded CEntity::Teleport
                # bridge command, then let CEntryExitManager reset its own state.
                threshold_deadline = time.monotonic() + 15
                while time.monotonic() < threshold_deadline:
                    threshold = command('probe_house_clear_threshold')
                    if threshold.get('result') == 'native_house_threshold_cleared':
                        break
                    if threshold.get('result') != 'threshold_transition_active':
                        raise RuntimeError('Native enex threshold could not be cleared: ' + json.dumps(threshold))
                    time.sleep(.25)
                else:
                    raise RuntimeError('Native enex remained active')
                time.sleep(1)
            command('probe_visual_scene', 0)
            command('probe_visual_camera')
            if args.fire_effects:
                aim=command('probe_aim_start',22)
                assert aim.get('result')=='aim_native_sample_started',aim
                time.sleep(.5)
                ammo=command('probe_native_ammo');before=int(ammo['result'].removeprefix('native_ammo_'))
                for _ in range(4):
                    firing=command('probe_aim_fire');assert firing.get('result')=='aim_native_fire_sample_started',firing
                    time.sleep(.5)
                ammo=command('probe_native_ammo');after=int(ammo['result'].removeprefix('native_ammo_'))
                assert 0<=after<before,(before,after)
                report['native_rounds_fired']=before-after
                command('probe_aim_stop');time.sleep(.5)
            if args.scenario not in ('melee', 'native-shot'):
                grove = command('probe_grove_street')
                assert grove.get('result') == 'grove_scene_result_0', grove
                time.sleep(2)
            if args.scenario == 'car-contact':
                car = command('probe_vehicle_reaction', 139)  # Native model445 sedan
                assert int(car.get('result', '').removeprefix('vehicle_reaction_result_')) > 0, car
            setup_mode, impact_mode = {'melee': (150, 152), 'bike-shot': (20, 30),
                                      'bicycle-shot': (21, 30), 'car-contact': (133, 134), 'native-shot': (0, 0)}[args.scenario]
            native = args.scenario == 'native-shot'
            setup = command('prepare_native_impact') if native else command('probe_vehicle_reaction', setup_mode)
            prefix = 'native_impact_result_' if native else 'vehicle_reaction_result_'
            if not setup.get('result', '').startswith(prefix) or int(setup['result'][len(prefix):]) <= 0:
                raise RuntimeError('Physics fixture was refused before any impact: ' + json.dumps(setup))
            target_ref = int(setup['result'][len(prefix):])
            time.sleep(.7)
            if args.scenario not in ('melee', 'native-shot'):
                focus = command('probe_delirium_focus', ref=target_ref)
                assert focus.get('result') == 'delirium_focus_result_0', focus
            impact = command('fire_native_impact') if native else command('probe_vehicle_reaction', impact_mode)
            assert impact.get('result') == prefix + str(target_ref), impact
            for sample in range(60):
                time.sleep(.25)
                report['observations'].append(read(game / args.resource_dir / 'state.json'))
                if args.scenario != 'melee' and sample in (1, 5, 11, 31):
                    frame = command('probe_visual_frame')
                    if frame.get('result') == 'visual_frame_ok':
                        from PIL import Image
                        Image.open(folder / f'visual-frame-{child.pid}.bmp').convert('RGB').resize((1280, 720)).save(args.out / f'impact-{sample:02d}.png')
            state = command('probe_visual_frame')
            if state['result'] == 'visual_frame_ok':
                from PIL import Image
                Image.open(folder / f'visual-frame-{child.pid}.bmp').convert('RGB').resize((1280, 720)).save(args.out / 'game.png')
            if args.trainer_menu:
                before=command('observe').get('trainer_pad_reads',0)
                for action,name in [('probe_trainer_toggle','trainer-open'),('probe_trainer_down','trainer-navigation'),('probe_trainer_toggle','trainer-closed')]:
                    result=command(action)
                    assert result.get('result')=='trainer_pad_probe_sent',result
                    time.sleep(1)
                    frame=command('probe_visual_frame')
                    if frame.get('result')=='visual_frame_ok':
                        from PIL import Image
                        Image.open(folder/f'visual-frame-{child.pid}.bmp').convert('RGB').resize((1280,720)).save(args.out/(name+'.png'))
                after=command('observe').get('trainer_pad_reads',0)
                assert after>before,(before,after)
                report['trainer_input']={'module_local_reads':after-before,'hardware_controller_tested':False,'desktop_input_sent':False}
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
        if save_mode is not None:
            save.chmod(save_mode)
        report['save_preserved'] = (sha(save) if save.exists() else None) == save_sha
        assert report['save_preserved']
        (args.out / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        print(json.dumps({'status': report.get('status'), 'report': str(args.out / 'report.json'), 'error': report.get('error')}), flush=True)


if __name__ == '__main__':
    main()
