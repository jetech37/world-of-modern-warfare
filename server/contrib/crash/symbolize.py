#!/usr/bin/env python3
"""Resolve a Windows crash offset in mangosd.exe to a function and source line.

The Windows Application log reports a crash as
    Exception code: 0xc0000005, Fault offset: 0x00000000002ebec9
The fault offset is an RVA into mangosd.exe.  With the matching mangosd.pdb
(written next to the exe by the Release build, /Zi + /DEBUG) this prints the
function, its source file and line:

    python symbolize.py path\\to\\mangosd.exe 0x2ebec9 [more offsets...]

Options:
    --pdb DIR     folder holding mangosd.pdb (default: the exe's folder)
    --disasm      also disassemble around the offset (needs `pip install capstone pefile`)
    --context N   bytes of code before the offset to disassemble (default 0x60)

The pdb must come from the very build that crashed (same image timestamp);
an exe from another build has a different layout.  Uses dbghelp.dll from
Windows via ctypes, so nothing else needs installing.
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import os
import sys

MAX_SYM_NAME = 2000


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [
        ("SizeOfStruct", wt.ULONG), ("TypeIndex", wt.ULONG), ("Reserved", ctypes.c_uint64 * 2),
        ("Index", wt.ULONG), ("Size", wt.ULONG), ("ModBase", ctypes.c_uint64), ("Flags", wt.ULONG),
        ("Value", ctypes.c_uint64), ("Address", ctypes.c_uint64), ("Register", wt.ULONG),
        ("Scope", wt.ULONG), ("Tag", wt.ULONG), ("NameLen", wt.ULONG), ("MaxNameLen", wt.ULONG),
        ("Name", ctypes.c_char * MAX_SYM_NAME),
    ]


class IMAGEHLP_LINE64(ctypes.Structure):
    _fields_ = [
        ("SizeOfStruct", wt.DWORD), ("Key", ctypes.c_void_p), ("LineNumber", wt.DWORD),
        ("FileName", ctypes.c_char_p), ("Address", ctypes.c_uint64),
    ]


def pe_info(path):
    """Image timestamp and the .pdata function range table, read without pefile."""
    import struct
    with open(path, "rb") as f:
        data = f.read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    timestamp = struct.unpack_from("<I", data, pe + 8)[0]
    nsections = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    exc_rva, exc_size = struct.unpack_from("<II", data, opt + 112 + 3 * 8)
    sections = []
    sec = opt + opt_size
    for i in range(nsections):
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, sec + 40 * i + 8)
        sections.append((va, max(vsize, rawsize), rawptr))

    def rva_to_off(rva):
        for va, size, raw in sections:
            if va <= rva < va + size:
                return raw + rva - va
        return None

    funcs = []
    off = rva_to_off(exc_rva)
    if off is not None:
        for i in range(exc_size // 12):
            b, e, _ = struct.unpack_from("<III", data, off + 12 * i)
            funcs.append((b, e))
    return data, timestamp, funcs, rva_to_off


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe")
    ap.add_argument("offsets", nargs="+")
    ap.add_argument("--pdb", default=None)
    ap.add_argument("--disasm", action="store_true")
    ap.add_argument("--context", default="0x60")
    args = ap.parse_args()

    exe = os.path.abspath(args.exe)
    data, timestamp, funcs, rva_to_off = pe_info(exe)
    print("image %s  timestamp 0x%08x" % (exe, timestamp))

    dbghelp = ctypes.WinDLL("dbghelp.dll")
    dbghelp.SymInitialize.argtypes = [wt.HANDLE, ctypes.c_char_p, wt.BOOL]
    dbghelp.SymLoadModuleEx.argtypes = [wt.HANDLE, wt.HANDLE, ctypes.c_char_p, ctypes.c_char_p,
                                        ctypes.c_uint64, wt.DWORD, ctypes.c_void_p, wt.DWORD]
    dbghelp.SymLoadModuleEx.restype = ctypes.c_uint64
    dbghelp.SymFromAddr.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64),
                                    ctypes.POINTER(SYMBOL_INFO)]
    dbghelp.SymGetLineFromAddr64.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.POINTER(wt.DWORD),
                                             ctypes.POINTER(IMAGEHLP_LINE64)]
    dbghelp.SymSetOptions(0x2 | 0x10)  # UNDNAME | LOAD_LINES
    proc = wt.HANDLE(0x1234)
    search = (args.pdb or os.path.dirname(exe)).encode()
    if not dbghelp.SymInitialize(proc, search, False):
        sys.exit("SymInitialize failed")
    base = 0x140000000
    if not dbghelp.SymLoadModuleEx(proc, None, exe.encode(), None, base, 0, None, 0):
        sys.exit("SymLoadModuleEx failed (%d)" % ctypes.GetLastError())

    for text in args.offsets:
        rva = int(text, 16)
        print("\n== offset 0x%x ==" % rva)
        fn = next(((b, e) for b, e in funcs if b <= rva < e), None)
        if fn:
            print("pdata function range 0x%x-0x%x (+0x%x)" % (fn[0], fn[1], rva - fn[0]))
        sym = SYMBOL_INFO()
        sym.SizeOfStruct = (SYMBOL_INFO.Name.offset + 1 + 7) & ~7   # C's sizeof(SYMBOL_INFO): 88
        sym.MaxNameLen = MAX_SYM_NAME
        disp = ctypes.c_uint64()
        if dbghelp.SymFromAddr(proc, base + rva, ctypes.byref(disp), ctypes.byref(sym)):
            print("symbol   %s+0x%x" % (sym.Name.decode(errors="replace"), disp.value))
        else:
            print("symbol   <none: no matching pdb?> (%d)" % ctypes.GetLastError())
        line = IMAGEHLP_LINE64()
        line.SizeOfStruct = ctypes.sizeof(IMAGEHLP_LINE64)
        ldisp = wt.DWORD()
        if dbghelp.SymGetLineFromAddr64(proc, base + rva, ctypes.byref(ldisp), ctypes.byref(line)):
            print("line     %s:%d" % (line.FileName.decode(errors="replace"), line.LineNumber))
        if args.disasm:
            import capstone
            before = int(args.context, 16)
            start = fn[0] if fn else rva - before
            end = fn[1] if fn else rva + 0x40
            off = rva_to_off(start)
            md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
            for ins in md.disasm(data[off:off + end - start], start):
                if rva - before <= ins.address <= rva + 0x30:
                    lt = ""
                    if dbghelp.SymGetLineFromAddr64(proc, base + ins.address, ctypes.byref(ldisp), ctypes.byref(line)):
                        lt = "  ; %s:%d" % (os.path.basename(line.FileName.decode(errors="replace")), line.LineNumber)
                    mark = " <<<" if ins.address == rva else ""
                    print("  %08x: %-22s %s %s%s%s" % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str, mark, lt))

    dbghelp.SymCleanup(proc)


if __name__ == "__main__":
    main()
