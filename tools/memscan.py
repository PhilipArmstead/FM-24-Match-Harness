"""Read-only memory scanner for the running fm.exe (no debugger needed).

    python -I memscan.py vtable 0x145A25F58 [--max 50] [--dump 0x100]
    python -I memscan.py ptr 0x120877B20 [--max 50]
    python -I memscan.py dump 0x120877B20 [--len 0x100]

`vtable` finds 8-byte-aligned heap qwords equal to the value (objects of that class);
`ptr` finds references to an address; `dump` hex-dumps memory.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import struct
import sys

PROCESS_VM_READ = 0x10
PROCESS_QUERY_INFORMATION = 0x400
MEM_COMMIT, MEM_PRIVATE = 0x1000, 0x20000
READABLE = {0x02, 0x04, 0x08, 0x20, 0x40, 0x80}  # RO, RW, WC, XR, XRW, XWC


class MBI(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_ulonglong), ("AllocationBase", ctypes.c_ulonglong),
                ("AllocationProtect", wt.DWORD), ("PartitionId", wt.WORD), ("RegionSize", ctypes.c_ulonglong),
                ("State", wt.DWORD), ("Protect", wt.DWORD), ("Type", wt.DWORD)]


k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = wt.HANDLE
k32.VirtualQueryEx.argtypes = [wt.HANDLE, ctypes.c_ulonglong, ctypes.POINTER(MBI), ctypes.c_size_t]
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_ulonglong, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]


def find_pid(name="fm.exe"):
    import subprocess
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {name}", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if parts and parts[0].lower() == name:
            return int(parts[1])
    sys.exit(f"{name} not running")


class Proc:
    def __init__(self):
        self.h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, find_pid())
        if not self.h:
            sys.exit(f"OpenProcess failed: {ctypes.get_last_error()}")

    def read(self, addr, size):
        buf = ctypes.create_string_buffer(size)
        n = ctypes.c_size_t()
        if not k32.ReadProcessMemory(self.h, addr, buf, size, ctypes.byref(n)):
            return None
        return buf.raw[: n.value]

    def regions(self, private_only=True):
        addr, mbi = 0, MBI()
        while k32.VirtualQueryEx(self.h, addr, ctypes.byref(mbi), ctypes.sizeof(mbi)):
            if mbi.State == MEM_COMMIT and (mbi.Protect & 0xFF) in READABLE and not (mbi.Protect & 0x100) \
                    and (not private_only or mbi.Type == MEM_PRIVATE):
                yield mbi.BaseAddress, mbi.RegionSize
            addr = mbi.BaseAddress + mbi.RegionSize
            if addr >= 0x7FFFFFFF0000:
                break

    def find_qword(self, value, limit):
        needle = struct.pack("<Q", value)
        hits = []
        for base, size in self.regions():
            for off in range(0, size, 1 << 26):
                chunk = self.read(base + off, min(1 << 26, size - off))
                if not chunk:
                    continue
                i = chunk.find(needle)
                while i != -1:
                    if i % 8 == 0:
                        hits.append(base + off + i)
                        if len(hits) >= limit:
                            return hits
                    i = chunk.find(needle, i + 1)
        return hits


def hexdump(p, addr, n):
    data = p.read(addr, n) or b""
    for o in range(0, len(data), 16):
        row = data[o:o + 16]
        qs = " ".join(f"{struct.unpack_from('<Q', row, k)[0]:016x}" for k in range(0, len(row) - 7, 8))
        print(f"  +{o:04x}  {row.hex(' ')}   {qs}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["vtable", "ptr", "dump"])
    ap.add_argument("addr", type=lambda s: int(s, 0))
    ap.add_argument("--max", type=int, default=50)
    ap.add_argument("--dump", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--len", type=lambda s: int(s, 0), default=0x100)
    a = ap.parse_args()
    p = Proc()
    if a.cmd == "dump":
        hexdump(p, a.addr, a.len)
        return
    hits = p.find_qword(a.addr, a.max)
    print(f"{len(hits)} hit(s){' (limit reached)' if len(hits) >= a.max else ''}")
    for h in hits:
        print(hex(h))
        if a.dump:
            hexdump(p, h, a.dump)


if __name__ == "__main__":
    main()
