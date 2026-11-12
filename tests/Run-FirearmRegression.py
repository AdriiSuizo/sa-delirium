"""Run against an active isolated SA session in free roam."""
import json
import math
import os
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(sys.argv[1]).resolve()))
import audit as a


def command(action, **fields):
    identity = time.time_ns() // 1_000_000
    data = dict(pid=a.PID, id=identity, action=action, **fields)
    request = a.bridge / "command.json"
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        try:
            request.write_text(json.dumps(data), encoding="ascii")
            break
        except PermissionError:
            time.sleep(.04)
    while time.monotonic() < deadline:
        try:
            reply = json.loads((a.bridge / "state.json").read_text(encoding="ascii"))
            if reply.get("pid") == a.PID and reply.get("command_id") == identity:
                return reply["result"]
        except (OSError, ValueError):
            pass
        time.sleep(.05)
    raise RuntimeError(f"No reply to {data}")


def reaction(mode):
    return int(command("probe_vehicle_reaction", mode=mode).removeprefix("vehicle_reaction_result_"))


def events():
    log = a.ROOT / "SA/DeliriumSA-local-v3/events.jsonl"
    return [row for line in log.read_text(encoding="utf-8").splitlines()
            if (row := json.loads(line))["pid"] == a.PID]


failures = []


def check(name, actual, expected):
    print(f"{name}: {actual} (expected {expected})", flush=True)
    if actual != expected:
        failures.append(name)


def review_cover_frames(rows):
    frames = [dict(part.split("=") for part in row["detail"].split())
              for row in rows if row["event"] == "cover_animation_frame"]
    summary = {}
    for frame in frames:
        summary.setdefault(frame["entered"], []).append(frame)
    report = []
    for sequence in summary.values():
        changes = []
        for frame in sequence:
            if not changes or changes[-1]["pose"] != frame["pose"]:
                changes.append({key: frame[key] for key in ("frame", "pose", "blending")})
        jumps = []
        for previous, current in zip(sequence, sequence[1:]):
            if int(current["frame"]) != int(previous["frame"])+1 or previous["pose"] == current["pose"]:
                continue
            jumps.append({
                "frame": int(current["frame"]), "from": previous["pose"], "to": current["pose"],
                **{bone: math.dist(tuple(map(float, previous[bone].split(","))),
                                   tuple(map(float, current[bone].split(","))))
                   for bone in ("head", "pelvis", "hand")},
            })
        report.append({"low": sequence[0]["low"]=="1", "frames": len(sequence), "changes": changes, "jumps_m": jumps})
    print("Cover frame trace:", [(row["low"], row["frames"], [c["pose"] for c in row["changes"]]) for row in report], flush=True)
    (a.RUN / "cover-frames.json").write_text(json.dumps({"frames": frames, "summary": report}, indent=2), encoding="utf-8")
    for low in (False, True):
        check(f"Frame trace captured {'low' if low else 'high'} walk and reversal", any(
            entry["low"]==low and {"walk", "turn"} <= {c["pose"] for c in entry["changes"]} for entry in report), True)
    check("Frame trace captured a blind-fire pose", any(frame["pose"]=="blind" for frame in frames), True)
    changes = [jump for entry in report for jump in entry["jumps_m"] if {jump["from"],jump["to"]}<={"rest","walk","turn","peek","blind"}]
    if changes:
        peaks = {bone: max(jump[bone] for jump in changes) for bone in ("head", "pelvis", "hand")}
        print("Largest pose-switch displacement (metres):", peaks, flush=True)
    check("Adjacent cover pose changes have no head or pelvis snap over 8 cm",
          bool(changes) and all(max(jump["head"], jump["pelvis"])<.08 for jump in changes), True)


if "--cover-only" in sys.argv:
    for scene, expected in ((425, 13), (429, 13), (426, 31), (443, 31)):
        check(f"Cover fixture {scene} placed", reaction(scene), 1)
        if scene in (426, 443):
            reaction(397)
        time.sleep(.65)
        check(f"Cover fixture {scene} entered", reaction(427), 1)
        if scene == 425:
            check("Standing entry is already blending into the cover pose", reaction(437), 3)
        for _ in range(12):
            state = reaction(428)
            if state == expected:
                break
            time.sleep(.12)
        check(f"Cover fixture {scene} settled and playing the IV clip", state, expected)
        if state == expected and command("probe_visual_frame") == "visual_frame_ok":
            (a.RUN / f"cover-{scene}.bmp").write_bytes((a.bridge / f"visual-frame-{a.PID}.bmp").read_bytes())
        if scene in (425, 443):
            start = reaction(439)
            depth = [reaction(444)]
            axis = reaction(440)
            stick = "sx" if abs(axis) == 1 else "sy"
            press = 82 if axis > 0 else -82
            command("audit_pad", buttons=0, ms=850, **{stick: press})
            steps = []
            for _ in range(4):
                time.sleep(.08)
                steps.append(reaction(441))
            check(f"Half stick moves along cover with a rendered gait ({steps})", any(state & 5 == 5 for state in steps), True)
            depth.append(reaction(444))
            time.sleep(.42)
            moved = reaction(439)
            check("Half stick actually displaced CJ along cover", abs(moved-start)>15, True)
            command("audit_pad", buttons=0, ms=1150, **{stick: -press})
            turns = []
            for _ in range(5):
                time.sleep(.10)
                turns.append(reaction(441))
                depth.append(reaction(444))
            check("Reversing plays the cover turn", any(state & 2 for state in turns), True)
            time.sleep(.60)
            reversed_to = reaction(439)
            check("CJ walked back after reversing", abs(reversed_to-start)<abs(moved-start), True)
            depth.append(reaction(444))
            check(f"Cover {scene} depth stays within 7 cm ({depth})", max(depth)-min(depth)<=70, True)
        if scene == 426:
            check("Cover exposure and return use the IV clip", reaction(436), 3)
            check("Blind fire uses the IV clip, muzzle and native ammo", reaction(433), 7)
            initial = reaction(442)
            for fire in (4, 16, 8):
                command("audit_pad", buttons=fire, ms=1000)
                time.sleep(.55)
                if reaction(442)<initial and reaction(441)&16:
                    break
                command("audit_pad", buttons=0, ms=60)
                time.sleep(.30)
            check("Fire alone spent a round through the cover route", reaction(442)<initial and bool(reaction(441)&16), True)
            command("audit_pad", buttons=0, ms=60)
        check(f"Cover fixture {scene} exited", reaction(427), 0)
        check(f"Cover fixture {scene} released", reaction(428), 0)
    check("Aimed entry fixture placed", reaction(426), 1)
    reaction(397)
    time.sleep(.65)
    for button in (16, 32, 8, 4):
        command("audit_pad", buttons=button, ms=1250)
        time.sleep(.18)
        if reaction(438):
            break
    if reaction(438):
        check("CJ entered with native aim held", reaction(427), 1)
        check("Aimed entry blends from the native pose", reaction(437), 7)
        command("audit_pad", buttons=0, ms=60)
        time.sleep(.65)
        check("Aimed entry settled in cover", reaction(428), 31)
        reaction(427)
    else:
        failures.append("No native aim input from fixture")
        print("No native aim input from fixture", flush=True)
    ref = reaction(430)
    check("Armed police fixture spawned", ref >= 0, True)
    time.sleep(.2)
    print(f"Police fixture readiness bits: {reaction(434)}", flush=True)
    ammo = reaction(435)
    check("Real player shot reached the police threat tracker", reaction(431), 1)
    for i in range(28):
        time.sleep(.15)
        if i in (9, 19):
            reaction(431)
        reaction(432)
    rows = events()
    if os.environ.get("GTASA_COVER_FRAME_TRACE"):
        review_cover_frames(rows)
    native_rays = [row["detail"] for row in rows if row["event"] == "cover_blind_native_ray"]
    print("Blind native rays:", native_rays, flush=True)
    check("Native blind-fire ray matches the muzzle and camera direction", any(
        float(dict(item.split("=") for item in row.split())["direction"])>.94 and
        float(dict(item.split("=") for item in row.split())["muzzle_error"])<.12
        for row in native_rays), True)
    phases = [row["detail"] for row in rows if row["ref"] == ref and row["event"] == "police_cover_phase"]
    ammo_after = reaction(435)
    print("Police cover phases:", phases, "ammo:", ammo, "to", ammo_after, flush=True)
    check("Police fired native rounds from cover", 0 <= ammo_after < ammo, True)
    check("Police took cover, exposed and returned", set(phases) >= {"approach", "enter", "hidden", "expose", "fire", "return"}, True)
    check("A running entry used the dive", any(row["event"] == "player_cover_entered" and "dive" in row["detail"] for row in rows), True)
    (a.RUN / "cover-evidence.json").write_text(json.dumps(
        [row for row in rows if "cover" in row["event"] or row["event"] == "engine_error"], indent=2), encoding="utf-8")
    check("Engine errors", sum(row["event"] == "engine_error" for row in rows), 0)
    raise SystemExit(", ".join(failures) if failures else 0)


if "--health-only" in sys.argv:
    ref = reaction(400)
    check("Health fixture NPC spawned", ref >= 0, True)
    time.sleep(.6)
    for mode, name in enumerate(("NPC armour and overflow", "NPC wounded threshold", "CJ pistol damage",
                                 "CJ armour", "NPC melee damage", "CJ melee damage", "CJ God mode",
                                 "CJ fatal threshold"), 417):
        check(name, reaction(mode), 1)
        time.sleep(.22)
    rows = events()
    (a.RUN / "health-evidence.json").write_text(json.dumps(
        [row for row in rows if row["event"] in {"iv_combat_health", "probe_iv_combat_health", "engine_error"}],
        indent=2), encoding="utf-8")
    check("Engine errors", sum(row["event"] == "engine_error" for row in rows), 0)
    raise SystemExit(", ".join(failures) if failures else 0)


for actor, (mode, interval) in enumerate(((404, 333), (405, 133), (406, 1233), (407, 66))):
    baseline = len(events())
    ref = reaction(400 + actor)
    check(f"NPC {actor} spawned", ref >= 0, True)
    if ref < 0:
        continue
    time.sleep(.55)
    check(f"NPC {actor}, native fire and cadence", reaction(mode), interval)
    if mode == 406:
        check("NPC shooter spawned", reaction(416) >= 0, True)
        time.sleep(.45)
        check("NPC shotgun cadence", reaction(415), 1233)
    reaction(411)
    time.sleep(.55)
    reaction(411)
    time.sleep(.35)
    reaction(412)
    time.sleep(.65)
    recent = events()[baseline:]
    rows = [row for row in recent if row["ref"] == ref]
    kinds = {row["event"] for row in rows}
    check(f"NPC {actor}, physical shot admitted", "shot_started" in kinds, True)
    check(f"NPC {actor}, subsequent hit retains native bookkeeping",
          "owned_shot_native_reaction_replaced" in kinds, True)
    check(f"NPC {actor}, fatal follow-up kept the same body", "shot_native_death_selected" in kinds, True)
    if mode == 406:
        check("Shotgun native pellet contacts",
              any(row["event"] == "shotgun_native_contacts" for row in rows), True)
        for count in (16, 8):
            check(f"Shotgun casts {count} rays",
                  any(row["event"] == "shotgun_iv_ray_cast" and f"rays={count} " in row["detail"]
                      for row in recent), True)

ref = reaction(400)
check("Armed civilian spawned", ref >= 0, True)
time.sleep(.55)
check("Pistol equipped", reaction(408), 22)
time.sleep(.45)
reaction(409)
for _ in range(10):
    state = reaction(410)
    if state == 7:
        break
    time.sleep(.15)
check("Hand shot makes a landed native pickup", state, 7)
if state == 7:
    if command("probe_visual_frame") == "visual_frame_ok":
        (a.RUN / "weapon-on-ground.bmp").write_bytes((a.bridge / f"visual-frame-{a.PID}.bmp").read_bytes())

check("Two camera targets spawned", reaction(413), 1)
check("Camera and right-stick switching", reaction(414), 31)
rows = events()
(a.RUN / "firearm-evidence.json").write_text(json.dumps(
    [row for row in rows if row["event"] in {
        "native_firearm_hooks_installed", "native_dropped_weapon_effect_installed", "weapon_table",
        "probe_native_weapon_cadence", "shot_started", "shot_burst_push", "shot_fed", "grounded_shot_impulse",
        "owned_shot_native_reaction_replaced", "shot_native_death_selected",
        "shotgun_native_contacts", "shotgun_iv_ray_cast", "shot_out_of_hand", "shot_weapon_landed", "engine_error",
    }], indent=2), encoding="utf-8")
check("Engine errors", sum(row["event"] == "engine_error" for row in rows), 0)
if failures:
    raise SystemExit(", ".join(failures))
