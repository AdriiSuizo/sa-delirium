"""Bake the player's extracted GTA IV drive-by animations in vehicle coordinates."""
import argparse
import os
import struct
import subprocess
import tempfile
import sys
from pathlib import Path
import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "03-pruebas-y-datos"))
from gait_io import motion

GROUPS = ("std", "low", "van", "truck", "bike_spt", "bike_dirt", "bike_free", "bike_chop", "bike_scot")
SEATS = ("ds", "ps", "bl", "br")


def unit(v):
    return v / np.linalg.norm(v, axis=-1, keepdims=True).clip(min=1e-7)


def bake(file, rows, aim_hand):
    names = [row[0] for row in rows]
    _, poses, duration = motion(file, rows)
    def raw(name):
        return poses[:, names.index("Char_" + name), 3, :3]

    # The root axes differ between dictionaries and even between the entry
    # and the aim loop in the same seat. The seated hip line fixes this once
    # per clip, preserving the driver's animated twists.
    right = (raw("R_Thigh") - raw("L_Thigh"))[0].copy()
    right[2] = 0
    right = unit(right)
    up = np.array([0., 0., 1.])
    basis = np.column_stack((right, np.cross(up, right), up))
    def joint(name):
        return raw(name) @ basis

    def hand_travel(side):
        return np.linalg.norm(np.diff(joint(side+"_Hand") - joint(side+"_UpperArm"), axis=0), axis=1).sum()
    if aim_hand is None:
        aim_hand = int(hand_travel("L") > hand_travel("R"))
    side = ("R", "L")[aim_hand]
    values = [unit(joint(side+"_Hand") - joint(side+"_Forearm")),
              unit(joint("R_UpperArm") - joint("L_UpperArm")), unit(joint("Head") - joint("Spine")),
              unit(joint(side+"_Finger2") - joint(side+"_Hand"))]
    for prefix in ("R_", "L_"):
        upper, el, wrist = (joint(prefix + part) for part in ("UpperArm", "Forearm", "Hand"))
        reach = unit(wrist - upper)
        fore = el - upper
        ext = np.linalg.norm(wrist-upper, axis=1) / (np.linalg.norm(fore,axis=1) + np.linalg.norm(wrist-el,axis=1))
        values.extend((unit(fore - reach*np.sum(fore*reach,axis=1,keepdims=True)), reach, ext[:,None]))
    frames = np.concatenate(values,axis=1).astype("<f4")
    assert frames.shape[1] == 26 and 2 <= len(frames) <= 512 and np.isfinite(frames).all()
    velocity = np.gradient(joint(side+"_Hand") - joint(side+"_UpperArm"), axis=0)
    lo, hi = max(1,round(len(frames)*.1)), max(2,round(len(frames)*.85))
    peak = lo + np.argmax(np.linalg.norm(velocity[lo:hi],axis=1))
    return frames, duration, aim_hand, (peak/(len(frames)-1), *unit(velocity)[peak])


def write_bank(clips_root, dest):
    count = 0
    with dest.open("wb") as stream:
        stream.write(b"DBV4")
        for index, group in enumerate(GROUPS):
            path = clips_root / group
            rows = [r.split() for r in (path/"bones.tsv").read_text().splitlines()]
            for seat in SEATS:
                if not (path/(seat+"_aim_loop.bin")).is_file():
                    if seat in ("ds", "ps") or group in ("std", "van", "truck"):
                        raise FileNotFoundError(path/(seat+"_aim_loop.bin"))
                    stream.write(struct.pack("<I", 0))
                    continue
                aim_hand = int(index < 4 and seat in ("ds", "bl"))
                aim, entry, leave = [bake(path/(seat+"_aim_"+part+".bin"), rows, aim_hand)
                                     for part in ("loop","in","out")]
                name = "throw_grenade" if seat == "ds" else seat+"_throw_grenade"
                throwing = bake(path/(name+".bin"), rows, None)
                stream.write(struct.pack("<4I2I7f", *(len(c[0]) for c in (aim,entry,leave,throwing)),
                                         aim_hand, throwing[2], entry[1], leave[1], throwing[1], *throwing[3]))
                for c in (aim,entry,leave,throwing):
                    stream.write(c[0].tobytes())
                count += 1
    print(f"{count} seats, {count*4} original IV animation clips")
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--iv", type=Path, help="GTA IV directory with GTAIV.exe")
    inputs.add_argument("--clips", type=Path, help="Previously decoded dictionaries")
    parser.add_argument("--extractor", type=Path, help="iv-ped-fixture executable")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.iv and not args.extractor:
        parser.error("--iv requires --extractor")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.out.parent) as directory:
        work = Path(directory)
        source = args.clips or work
        if args.iv:
            for group in GROUPS:
                dest = work / group
                dest.mkdir()
                subprocess.run([args.extractor.resolve(), args.iv.resolve(), dest, "--driveby",
                                "veh@driveby" + group], check=True)
        output = work / "DriverPoses.bin"
        write_bank(source, output)
        os.replace(output, args.out)

if __name__ == "__main__":
    main()
