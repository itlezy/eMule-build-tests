"""Resolve RVAs in emule.exe to function names using dbghelp.dll and the PDB."""
import ctypes
from ctypes import wintypes
import struct
import os

# Constants
SYMOPT_UNDNAME = 0x00000002
SYMOPT_DEFERRED_LOADS = 0x00000004
SYMOPT_LOAD_LINES = 0x00000010
MAX_SYM_NAME = 2000

# Load dbghelp
dbghelp = ctypes.WinDLL("dbghelp.dll")

# SYMBOL_INFO structure
class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [
        ("SizeOfStruct", wintypes.ULONG),
        ("TypeIndex", wintypes.ULONG),
        ("Reserved", ctypes.c_uint64 * 2),
        ("Index", wintypes.ULONG),
        ("Size", wintypes.ULONG),
        ("ModBase", ctypes.c_uint64),
        ("Flags", wintypes.ULONG),
        ("Value", ctypes.c_uint64),
        ("Address", ctypes.c_uint64),
        ("Register", wintypes.ULONG),
        ("Scope", wintypes.ULONG),
        ("Tag", wintypes.ULONG),
        ("NameLen", wintypes.ULONG),
        ("MaxNameLen", wintypes.ULONG),
        ("Name", ctypes.c_char * MAX_SYM_NAME),
    ]

class IMAGEHLP_LINE64(ctypes.Structure):
    _fields_ = [
        ("SizeOfStruct", wintypes.DWORD),
        ("Key", ctypes.c_void_p),
        ("LineNumber", wintypes.DWORD),
        ("FileName", ctypes.c_char_p),
        ("Address", ctypes.c_uint64),
    ]

# Setup
hProcess = ctypes.c_void_p(0xDEADBEEF)  # Pseudo handle
dbghelp.SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES)

if not dbghelp.SymInitialize(hProcess, None, False):
    print(f"SymInitialize failed: {ctypes.GetLastError()}")
    exit(1)

# Load module
exe_path = r"C:\prj\p2p\eMule\eMulebb\eMule-build\eMule\srchybrid\x64\Debug\emule.exe"
pdb_dir = r"C:\prj\p2p\eMule\eMulebb\eMule-build\eMule\srchybrid\x64\Debug"
IMAGE_BASE = 0x140000000

# Set symbol search path to PDB directory before loading
sym_path = pdb_dir.encode()
dbghelp.SymSetSearchPath(hProcess, sym_path)
print(f"Symbol search path: {pdb_dir}")

# SymLoadModuleEx
dbghelp.SymLoadModuleEx.restype = ctypes.c_uint64
dbghelp.SymLoadModuleEx.argtypes = [
    ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
    ctypes.c_uint64, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD
]

# Get file size for SymLoadModuleEx
exe_size = os.path.getsize(exe_path)
mod_base = dbghelp.SymLoadModuleEx(
    hProcess, None, exe_path.encode(), None, IMAGE_BASE, exe_size, None, 0
)
if mod_base == 0:
    err = ctypes.GetLastError()
    print(f"SymLoadModuleEx failed: {err}")
    if err != 0:
        exit(1)

print(f"Module loaded at base {mod_base:#x}")

# Resolve RVAs
rvas = [0xe67ed1, 0x69227b, 0xe67e60, 0xe67e10]  # stuck addresses + function starts

for rva in rvas:
    addr = IMAGE_BASE + rva

    # Symbol
    sym = SYMBOL_INFO()
    sym.SizeOfStruct = ctypes.sizeof(SYMBOL_INFO) - MAX_SYM_NAME
    sym.MaxNameLen = MAX_SYM_NAME
    displacement = ctypes.c_uint64(0)

    dbghelp.SymFromAddr.restype = wintypes.BOOL
    dbghelp.SymFromAddr.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(SYMBOL_INFO)]

    if dbghelp.SymFromAddr(hProcess, addr, ctypes.byref(displacement), ctypes.byref(sym)):
        name = sym.Name.decode("utf-8", errors="replace")
        print(f"  RVA {rva:#010x} => {name}+{displacement.value:#x}")
    else:
        print(f"  RVA {rva:#010x} => (symbol lookup failed: {ctypes.GetLastError()})")

    # Source line
    line = IMAGEHLP_LINE64()
    line.SizeOfStruct = ctypes.sizeof(IMAGEHLP_LINE64)
    line_disp = wintypes.DWORD(0)

    dbghelp.SymGetLineFromAddr64.restype = wintypes.BOOL
    dbghelp.SymGetLineFromAddr64.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(IMAGEHLP_LINE64)]

    if dbghelp.SymGetLineFromAddr64(hProcess, addr, ctypes.byref(line_disp), ctypes.byref(line)):
        fname = line.FileName.decode("utf-8", errors="replace") if line.FileName else "(unknown)"
        print(f"            => {fname}:{line.LineNumber}")
    print()

dbghelp.SymCleanup(hProcess)
