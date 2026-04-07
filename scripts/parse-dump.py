"""Parse minidump to extract thread instruction pointers and resolve to modules."""
import struct
import sys
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TEST_REPO_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, ".."))
emule_workspace_root = os.environ.get(
    "EMULE_WORKSPACE_ROOT",
    os.path.normpath(os.path.join(TEST_REPO_ROOT, "..", "..")),
)
DUMP_PATH = os.path.join(
    emule_workspace_root,
    "repos",
    "eMule-build-tests",
    "reports",
    "diag-hash-latest",
    "emule-stuck.dmp",
)

from minidump.minidumpfile import MinidumpFile

mf = MinidumpFile.parse(DUMP_PATH)

# Collect modules
modules = []
if mf.modules:
    for m in mf.modules.modules:
        modules.append((m.baseaddress, m.size, m.name))

def resolve(addr):
    for base, size, name in modules:
        if base <= addr < base + size:
            offset = addr - base
            short = name.rsplit("\\", 1)[-1]
            return f"{short}+{offset:#x}"
    return f"{addr:#018x}"

# Read the raw dump file to extract CONTEXT structures
with open(DUMP_PATH, "rb") as f:
    raw = f.read()

print("=== THREADS ===")
if mf.threads:
    for t in mf.threads.threads:
        ctx_rva = t.ThreadContext.Rva
        ctx_size = t.ThreadContext.DataSize

        # x64 CONTEXT: RIP at offset 0xF8, RSP at offset 0x98
        if ctx_size >= 0x100:
            rip = struct.unpack_from("<Q", raw, ctx_rva + 0xF8)[0]
            rsp = struct.unpack_from("<Q", raw, ctx_rva + 0x98)[0]
            loc = resolve(rip)
            print(f"  TID={t.ThreadId:#06x}  RIP={rip:#018x}  RSP={rsp:#018x}  => {loc}")
        else:
            print(f"  TID={t.ThreadId:#06x}  (context too small: {ctx_size})")

print()
print("=== KEY MODULES ===")
for base, size, name in modules:
    short = name.rsplit("\\", 1)[-1].lower()
    if any(k in short for k in ("emule", "ntdll", "kernel", "mfc", "msvc")):
        print(f"  {base:#018x}  size={size:#010x}  {name.rsplit(chr(92), 1)[-1]}")
