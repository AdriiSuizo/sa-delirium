"""Build the vehicle-pose ASI and bank reader in an x86 MSVC developer prompt."""
from pathlib import Path
import argparse
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--sdk", required=True, type=Path, help="Root of DK22Pac/plugin-sdk")
parser.add_argument("--out", required=True, type=Path)
args = parser.parse_args()
source = Path(__file__).resolve().parent
sdk = args.sdk.resolve()
sa = sdk / "plugin_sa/game_sa"
out = args.out.resolve()
out.mkdir(exist_ok=True, parents=True)
includes = [sdk/"shared", sdk/"shared/game", sdk/"shared/dxsdk", sa, sa/"rw", sa/"enums", sdk/"safetyhook"]
flags = ["/nologo", "/O2", "/MT", "/EHsc", "/Gy", "/bigobj", "/std:c++latest",
         "/D_USE_MATH_DEFINES", "/DGTASA", "/DPLUGIN_SGV_10US", "/DRW", "/DNOMINMAX",
         "/D_CRT_SECURE_NO_WARNINGS"] + ["/I" + str(d) for d in includes if d.is_dir()]
units = [sa/(f+".cpp") for f in ("CPools", "common", "RenderWare", "CWorld", "CTimer")]
units += [sdk/"shared"/(f+".cpp") for f in ("GameVersion", "DynAddress", "Patch")]
subprocess.run(["cl", *flags, "/c", *units], cwd=out, check=True)
subprocess.run(["lib", "/nologo", "/out:game-sdk.lib", *[p.stem+".obj" for p in units]], cwd=out, check=True)
subprocess.run(["cl", *flags, "/LD", source/"DriverPose.cpp", "game-sdk.lib", "/link",
                "/OPT:REF", "/OUT:IVDriverPose.asi"], cwd=out, check=True)
subprocess.run(["cl", *flags, source/"verificar_banco.cpp", "game-sdk.lib", "/link",
                "/OPT:REF", "/OUT:verificar_banco.exe"], cwd=out, check=True)
