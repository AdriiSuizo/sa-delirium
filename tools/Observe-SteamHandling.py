"""Read the stock Steam handling table for an owned lab process."""
import ctypes as c
import hashlib
import json
from pathlib import Path
import struct
import sys

path = Path(sys.argv[2]).resolve()
image = path.read_bytes()
if hashlib.sha256(image).hexdigest() != "15e3cfedba9a841df67d8194e7249afb493b0e10d6138fb8ebab2c136e543efb":
    raise SystemExit("Unsupported lab executable")
k = c.WinDLL("kernel32", use_last_error=True)
k.OpenProcess.argtypes = [c.c_uint32, c.c_int, c.c_uint32]
k.OpenProcess.restype = c.c_void_p
k.QueryFullProcessImageNameW.argtypes = [c.c_void_p, c.c_uint32, c.c_wchar_p, c.POINTER(c.c_uint32)]
k.ReadProcessMemory.argtypes = [c.c_void_p, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
k.CloseHandle.argtypes = [c.c_void_p]
class Module(c.Structure):
    _fields_ = [("size", c.c_uint32), ("module_id", c.c_uint32), ("pid", c.c_uint32),
                ("global_count", c.c_uint32), ("process_count", c.c_uint32),
                ("base", c.c_void_p), ("base_size", c.c_uint32), ("handle", c.c_void_p),
                ("name", c.c_wchar * 256), ("path", c.c_wchar * 260)]
k.CreateToolhelp32Snapshot.argtypes = [c.c_uint32, c.c_uint32]
k.CreateToolhelp32Snapshot.restype = c.c_void_p
k.Module32FirstW.argtypes = [c.c_void_p, c.POINTER(Module)]
k.Module32NextW.argtypes = [c.c_void_p, c.POINTER(Module)]
h = k.OpenProcess(0x1010, False, int(sys.argv[1]))
if not h:
    raise c.WinError(c.get_last_error())
try:
    name = c.create_unicode_buffer(32768)
    length = c.c_uint32(len(name))
    if not k.QueryFullProcessImageNameW(h, 0, name, c.byref(length)) or Path(name.value).resolve() != path:
        raise RuntimeError("PID does not belong to this lab")
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    num = struct.unpack_from("<H", image, pe + 6)[0]
    opt_size = struct.unpack_from("<H", image, pe + 20)[0]
    snapshot = k.CreateToolhelp32Snapshot(0x18, int(sys.argv[1]))
    if snapshot == c.c_void_p(-1).value:
        raise c.WinError(c.get_last_error())
    module = Module()
    module.size = c.sizeof(module)
    base = None
    try:
        ok = k.Module32FirstW(snapshot, c.byref(module))
        while ok:
            if Path(module.path).resolve() == path:
                base = module.base
                break
            ok = k.Module32NextW(snapshot, c.byref(module))
    finally:
        k.CloseHandle(snapshot)
    if base is None:
        raise RuntimeError("Lab executable was not mapped")
    sections = pe + 24 + opt_size
    results = []
    reads = 0
    for i in range(num):
        off = sections + i * 40
        size, rva = struct.unpack_from("<II", image, off + 8)
        if not struct.unpack_from("<I", image, off + 36)[0] & 0x80000000:
            continue
        for part in range(0, size, 65536):
            n = min(size - part, 65536 + 256)
            buf = c.create_string_buffer(n)
            got = c.c_size_t()
            address = base + rva + part
            if not k.ReadProcessMemory(h, address, buf, n, c.byref(got)):
                continue
            reads += got.value
            data = buf.raw[:got.value]
            for car, mass, turn in (("LANDSTAL", 1700.0, 5008.3), ("BRAVURA", 1300.0, 2200.0)):
                marker = struct.pack("<f", mass)
                begin = 4
                while (at := data.find(marker, begin)) >= 0:
                    begin = at + 1
                    if at + 0xC0 >= len(data) or data[at+8:at+12] != struct.pack("<f", turn):
                        continue
                    force, damping = struct.unpack_from("<ff", data, at + 0xA8)
                    results.append({"vehicle": car, "address": hex(address + at - 4),
                                    "force": force, "damping": damping})
    print(json.dumps({"module_base": hex(base), "read_bytes": reads, "rows": results}, allow_nan=False))
finally:
    k.CloseHandle(h)
