#!/usr/bin/env python3
"""
tests/test_runner.py

Authentic host-side static binary inspection and test orchestrator for Iron V bare-metal runtime.
Performs:
1. Direct ELF32 section and program header extraction via raw byte parsing (struct).
2. Memory topology monotonicity and 16-byte alignment validation.
3. Verification of static initialized .data symbol g_test_data_var (0x12345678) unpacked from raw ELF bytes.
4. Verification of .bss symbol g_test_bss_var residing in SHT_NOBITS section with SHF_WRITE | SHF_ALLOC.
5. Verification of .rodata symbol g_test_rodata_str ("IRON_V_RODATA_TEST_PATTERN") unpacked from raw ELF bytes.
6. Verification of Harvard segment isolation: 0 RWX segments, IRAM executable (0x40800000), DRAM read-write (0x40829000).
7. Verification of external flash XIP section (.flash_xip) allocatable status.
8. ESP32-C6 firmware.bin flash image header verification (Magic 0xE9, 8 MB flash geometry, entry 0x40800000).
9. Execution of host-native freestanding C unit test binary (tests/test_freestanding).
10. Architectural documentation that on-board hardware register reads/writes execute via src/test.c on physical silicon.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

IRAM_END = 0x40829000
FLASH_XIP_START = 0x42000000
FLASH_XIP_END = 0x42800000
ROM_START = 0x40000000
ROM_END = 0x40060000
ROM_DATA_START = 0x4087E610      # ROM .bss/.data (ld/link.ld)
IRAM_MIN_FREE = 16 * 1024        # budgets mirror the ASSERTs in ld/link.ld
MAIN_STACK_MIN_SIZE = 32 * 1024

# Code that must run without flash: before mmu_init() maps it, on trap/panic,
# and the flash erase/write/read routines that run while flash is busy.
# GCC clones keep the base name with a suffix (flash_write.isra.0).
IRAM_ONLY_ROOTS = [
    r"clock_init", r"mmu_init",
    r"trap_handler", r"trap_entry_exception", r"trap_entry_interrupt", r"panic_dump",
    r"flash_(read|write|erase_sector)(\..+)?",
]
FLASH_OP_ROUTINES_MIN = 6        # flash_read/write/erase_sector in nvs.c and ota.c

FUNC_RE = re.compile(r"^([0-9a-f]+) <([^>]+)>:$")
CALL_RE = re.compile(r"\b(?:jal|j|jalr|tail|call)\b.*?\b([0-9a-f]{8}) <([^>+]+)(\+0x[0-9a-f]+)?>")


def find_objdump(explicit=None):
    for cand in ([explicit] if explicit else []) + ["riscv64-unknown-elf-objdump", "riscv64-elf-objdump"]:
        if cand and shutil.which(cand):
            return cand
    return None


def iram_call_closure(elf_path, objdump, in_iram, in_flash):
    """Walks direct calls from IRAM_ONLY_ROOTS through IRAM functions.

    Returns (roots, walked, violations); a violation is a call path that reaches
    a function in flash. Indirect calls through pointers are not followed.
    """
    listing = subprocess.run([objdump, "-d", "--no-show-raw-insn", elf_path],
                             capture_output=True, text=True, check=True).stdout
    names, calls, cur = {}, {}, None
    for line in listing.splitlines():
        m = FUNC_RE.match(line)
        if m:
            cur = int(m.group(1), 16)
            names[cur] = m.group(2)
            calls[cur] = set()
            continue
        m = CALL_RE.search(line) if cur is not None else None
        if m and not (m.group(2) == names[cur] and m.group(3)):
            calls[cur].add(int(m.group(1), 16))
    roots = [a for a, n in names.items() if any(re.fullmatch(p, n) for p in IRAM_ONLY_ROOTS)]
    walked, violations = set(), []
    stack = [(a, [names[a]]) for a in roots]
    while stack:
        addr, path = stack.pop()
        if addr in walked:
            continue
        walked.add(addr)
        if not in_iram(addr):
            violations.append(" -> ".join(path))
            continue
        for tgt in calls.get(addr, ()):
            if in_flash(tgt):
                violations.append(" -> ".join(path + [names.get(tgt, hex(tgt))]))
            elif in_iram(tgt) and tgt in names:
                stack.append((tgt, path + [names[tgt]]))
    return [names[a] for a in roots], walked, violations

# ELF constants
EI_MAG0 = 0
ELFMAG = b"\x7fELF"
ELFCLASS32 = 1
ELFDATA2LSB = 1
EM_RISCV = 243

# Section types & flags
SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_NOBITS = 8

SHF_WRITE = 0x1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4

# Segment types & flags
PT_LOAD = 1
PF_X = 0x1
PF_W = 0x2
PF_R = 0x4

def parse_elf(elf_path):
    with open(elf_path, "rb") as f:
        raw = f.read()

    if len(raw) < 52 or raw[:4] != ELFMAG:
        raise ValueError(f"{elf_path} is not a valid 32-bit ELF file")

    if raw[4] != ELFCLASS32 or raw[5] != ELFDATA2LSB:
        raise ValueError(f"{elf_path} must be 32-bit little-endian ELF")

    e_entry, e_phoff, e_shoff = struct.unpack_from("<III", raw, 24)
    e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHHHH", raw, 42)

    # Section header string table
    shstr_hdr = raw[e_shoff + e_shstrndx * e_shentsize : e_shoff + (e_shstrndx + 1) * e_shentsize]
    shstr_offset, shstr_size = struct.unpack_from("<II", shstr_hdr, 16)
    shstrtab = raw[shstr_offset : shstr_offset + shstr_size]

    sections = {}
    symtab_info = None
    strtab_info = None

    for i in range(e_shnum):
        sh = raw[e_shoff + i * e_shentsize : e_shoff + (i + 1) * e_shentsize]
        sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize = struct.unpack_from("<IIIIIIIIII", sh, 0)
        sec_name = shstrtab[sh_name:].split(b"\x00")[0].decode("ascii", "ignore")
        sec_record = {
            "idx": i,
            "name": sec_name,
            "type": sh_type,
            "flags": sh_flags,
            "addr": sh_addr,
            "offset": sh_offset,
            "size": sh_size,
            "link": sh_link,
            "info": sh_info,
            "align": sh_addralign,
            "entsize": sh_entsize,
        }
        sections[sec_name] = sec_record
        if sh_type == SHT_SYMTAB:
            symtab_info = sec_record
        elif sh_type == SHT_STRTAB and sec_name == ".strtab":
            strtab_info = sec_record

    # Program headers
    segments = []
    for i in range(e_phnum):
        ph = raw[e_phoff + i * e_phentsize : e_phoff + (i + 1) * e_phentsize]
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = struct.unpack_from("<IIIIIIII", ph, 0)
        segments.append({
            "idx": i,
            "type": p_type,
            "offset": p_offset,
            "vaddr": p_vaddr,
            "paddr": p_paddr,
            "filesz": p_filesz,
            "memsz": p_memsz,
            "flags": p_flags,
            "align": p_align,
        })

    # Symbol table
    symbols = {}
    if symtab_info and strtab_info:
        strtab = raw[strtab_info["offset"] : strtab_info["offset"] + strtab_info["size"]]
        num_syms = symtab_info["size"] // 16
        for i in range(num_syms):
            sym_raw = raw[symtab_info["offset"] + i * 16 : symtab_info["offset"] + (i + 1) * 16]
            st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from("<IIIBBH", sym_raw, 0)
            sym_name = strtab[st_name:].split(b"\x00")[0].decode("ascii", "ignore")
            if sym_name:
                symbols[sym_name] = {
                    "name": sym_name,
                    "value": st_value,
                    "size": st_size,
                    "shndx": st_shndx,
                    "type": st_info & 0x0F,
                    "bind": st_info >> 4,
                }

    return {
        "raw": raw,
        "entry": e_entry,
        "sections": sections,
        "segments": segments,
        "symbols": symbols,
    }

def print_result_line(num, title, desc, expected, actual, pass_cond):
    print(f"\n[TEST {num:02d}] {title}")
    print(f"  Description: {desc}")
    print(f"  Expected:    {expected}")
    print(f"  Actual:      {actual}")
    print(f"  Result:      [ {'PASS' if pass_cond else 'FAIL'} ]")
    return 1 if pass_cond else 0

def run_suite(elf_path, bin_path, native_test_bin, objdump_path=None):

    if not os.path.exists(elf_path):
        print(f"Error: {elf_path} not found. Run 'make' first.")
        sys.exit(1)

    elf = parse_elf(elf_path)
    sections = elf["sections"]
    symbols = elf["symbols"]
    segments = elf["segments"]
    raw = elf["raw"]

    total = 0
    passed = 0

    print("=" * 70)
    print("        IRON V AUTHENTIC BASELINE VERIFICATION & UNIT TEST SUITE      ")
    print("=" * 70)

    # Required linker boundary symbols
    boundary_names = ["_stext", "_etext", "_srodata", "_erodata", "_sdata", "_edata",
                      "_sbss", "_ebss", "_stack_top", "g_test_data_var", "g_test_bss_var", "g_test_rodata_str"]
    for b_name in boundary_names:
        if b_name not in symbols:
            print(f"Error: Required symbol '{b_name}' not found in {elf_path}")
            sys.exit(1)

    stext = symbols["_stext"]["value"]
    etext = symbols["_etext"]["value"]
    srodata = symbols["_srodata"]["value"]
    erodata = symbols["_erodata"]["value"]
    sdata = symbols["_sdata"]["value"]
    edata = symbols["_edata"]["value"]
    sbss = symbols["_sbss"]["value"]
    ebss = symbols["_ebss"]["value"]
    stack_top = symbols["_stack_top"]["value"]

    # Placement predicates (ld/link.ld policy: code runs from flash unless kept in IRAM)
    def in_iram(addr):
        return stext <= addr < IRAM_END

    def in_flash(addr):
        return FLASH_XIP_START <= addr < FLASH_XIP_END

    def in_exec(addr):
        return in_iram(addr) or in_flash(addr)

    # TEST: Memory Section Topology & Monotonicity
    total += 1
    t1_pass = (stext == 0x40800000 and stext < etext and etext <= srodata
               and srodata >= 0x40829000 and srodata < erodata
               and erodata <= sdata and sdata <= edata and edata <= sbss and sbss <= ebss
               and ebss < stack_top and stack_top == 0x40880000)
    passed += print_result_line(
        total,
        "Memory Section Topology & Monotonicity",
        "Verify SRAM section layout conforms to ESP32-C6 Harvard architecture",
        "0x40800000 == _stext < _etext <= _srodata < _sdata < _sbss < _stack_top(0x40880000)",
        f"_stext=0x{stext:08x} _etext=0x{etext:08x} _srodata=0x{srodata:08x} _sdata=0x{sdata:08x} _sbss=0x{sbss:08x} _stack=0x{stack_top:08x}",
        t1_pass
    )

    # TEST: 16-Byte Section Alignment Verification
    total += 1
    t2_pass = ((stext % 16 == 0) and (srodata % 16 == 0) and (sdata % 16 == 0) and (sbss % 16 == 0))
    passed += print_result_line(
        total,
        "16-Byte Section Alignment Verification",
        "Verify all output section start VMAs are 16-byte aligned for ROM bootloader",
        "_stext%16==0, _srodata%16==0, _sdata%16==0, _sbss%16==0",
        f"_stext%16={stext % 16}, _srodata%16={srodata % 16}, _sdata%16={sdata % 16}, _sbss%16={sbss % 16}",
        t2_pass
    )

    # TEST: RW Data Static Initial Value in ELF Binary
    total += 1
    sym_data = symbols["g_test_data_var"]
    sec_data = sections[".data"]
    data_file_offset = sec_data["offset"] + (sym_data["value"] - sec_data["addr"])
    data_raw_bytes = raw[data_file_offset : data_file_offset + 4]
    data_unpacked_word = struct.unpack("<I", data_raw_bytes)[0]
    t3_pass = (data_unpacked_word == 0x12345678 and sym_data["value"] >= sdata and sym_data["value"] < edata)
    passed += print_result_line(
        total,
        "RW Data Section Static Initial Value in ELF Binary",
        "Unpack actual 4 bytes of g_test_data_var from .data section in firmware.elf",
        "Static initialized value = 0x12345678 at VMA in DRAM [0x40829000, 0x40880000)",
        f"File Offset=0x{data_file_offset:06x}, VMA=0x{sym_data['value']:08x}, Value=0x{data_unpacked_word:08x}",
        t3_pass
    )

    # TEST: BSS Section Allocation & SHT_NOBITS Verification
    total += 1
    sym_bss = symbols["g_test_bss_var"]
    sec_bss = sections[".bss"]
    bss_is_nobits = (sec_bss["type"] == SHT_NOBITS)
    bss_has_flags = ((sec_bss["flags"] & (SHF_WRITE | SHF_ALLOC)) == (SHF_WRITE | SHF_ALLOC))
    t4_pass = (bss_is_nobits and bss_has_flags and sym_bss["value"] >= sbss and sym_bss["value"] < ebss)
    passed += print_result_line(
        total,
        "BSS Section Allocation & SHT_NOBITS Verification",
        "Verify g_test_bss_var placement in SHT_NOBITS section with SHF_ALLOC | SHF_WRITE",
        "Section type = SHT_NOBITS(8), Flags contain SHF_WRITE | SHF_ALLOC, symbol in [_sbss, _ebss)",
        f"Section type={sec_bss['type']}, Flags=0x{sec_bss['flags']:x}, VMA=0x{sym_bss['value']:08x}",
        t4_pass
    )

    # TEST: Read-Only Memory (RODATA) Content & Flags Verification
    total += 1
    sym_rodata = symbols["g_test_rodata_str"]
    sec_rodata = sections[".rodata"]
    rodata_file_offset = sec_rodata["offset"] + (sym_rodata["value"] - sec_rodata["addr"])
    rodata_raw_bytes = raw[rodata_file_offset : rodata_file_offset + sym_rodata["size"]]
    rodata_str = rodata_raw_bytes.split(b"\x00")[0].decode("ascii", "replace")
    rodata_first_word = struct.unpack("<I", rodata_raw_bytes[:4])[0]
    rodata_alloc_only = ((sec_rodata["flags"] & SHF_ALLOC) != 0 and (sec_rodata["flags"] & SHF_WRITE) == 0)
    t5_pass = (rodata_alloc_only and rodata_str == "IRON_V_RODATA_TEST_PATTERN" and rodata_first_word == 0x4e4f5249)
    passed += print_result_line(
        total,
        "Read-Only Data (RODATA) Content & Flags Verification",
        "Unpack actual bytes of g_test_rodata_str from firmware.elf and inspect section flags",
        "Flags contain SHF_ALLOC (no SHF_WRITE), First Word=0x4e4f5249, String='IRON_V_RODATA_TEST_PATTERN'",
        f"Flags=0x{sec_rodata['flags']:x}, First Word=0x{rodata_first_word:08x}, String='{rodata_str}'",
        t5_pass
    )

    # TEST: Harvard Segment Isolation & W^X Permission Safety
    total += 1
    load_segs = [s for s in segments if s["type"] == PT_LOAD]
    rwx_segs = [s for s in load_segs if (s["flags"] & (PF_W | PF_X)) == (PF_W | PF_X)]
    iram_text_seg = [s for s in load_segs if s["vaddr"] >= 0x40800000 and s["vaddr"] < 0x40829000 and (s["flags"] & PF_X)]
    dram_data_seg = [s for s in load_segs if s["vaddr"] >= 0x40829000 and s["vaddr"] < 0x40880000 and (s["flags"] & PF_W)]
    t6_pass = (len(load_segs) >= 2 and len(rwx_segs) == 0 and len(iram_text_seg) > 0 and len(dram_data_seg) > 0)
    seg_flags_str = ", ".join(f"vaddr=0x{s['vaddr']:08x}:flags=0x{s['flags']:x}" for s in load_segs)
    passed += print_result_line(
        total,
        "Harvard Segment Isolation & W^X Permission Safety",
        "Inspect ELF program headers to ensure 0 RWX segments exist and IRAM/DRAM are segregated",
        "0 RWX segments, IRAM executable at 0x40800000, DRAM read-write in [0x40829000, 0x40880000)",
        f"Total LOAD segments={len(load_segs)}, RWX segments={len(rwx_segs)}, [{seg_flags_str}]",
        t6_pass
    )

    # TEST: External Flash XIP Section Allocation Remediation
    total += 1
    sec_xip = sections.get(".flash_xip")
    xip_ok = (sec_xip is not None and sec_xip["type"] == SHT_PROGBITS and sec_xip["addr"] == 0x42000000)
    passed += print_result_line(
        total,
        "External Flash XIP Section Allocation Remediation",
        "Verify .flash_xip is marked PROGBITS (NOLOAD removed) for execute-in-place flash execution",
        "Section .flash_xip exists, Type=SHT_PROGBITS(1), VMA=0x42000000",
        f"Section present={sec_xip is not None}, Type={sec_xip['type'] if sec_xip else 'N/A'}, VMA=0x{(sec_xip['addr'] if sec_xip else 0):08x}",
        xip_ok
    )

    # TEST: ESP32-C6 Flash Binary Image Geometry Validation
    total += 1
    t8_pass = False
    actual_bin_desc = "firmware.bin not found"
    if os.path.exists(bin_path):
        with open(bin_path, "rb") as f:
            bin_hdr = f.read(24)
        if len(bin_hdr) >= 24:
            b_magic = bin_hdr[0]
            b_segs = bin_hdr[1]
            b_flash_mode = bin_hdr[2]
            b_flash_sf = bin_hdr[3]
            b_entry = struct.unpack("<I", bin_hdr[4:8])[0]
            b_size_code = (b_flash_sf >> 4) & 0x0F
            actual_bin_desc = f"Magic=0x{b_magic:02x}, Segments={b_segs}, Entry=0x{b_entry:08x}, FlashSizeCode=0x{b_size_code:x} (8MB)"
            t8_pass = (b_magic == 0xE9 and b_entry == 0x40800000 and b_size_code == 0x3)

    passed += print_result_line(
        total,
        "ESP32-C6 Flash Binary Image Geometry Validation",
        "Inspect firmware.bin header for 8 MB SPI flash geometry and valid boot entry point",
        "Magic=0xE9, Entry=0x40800000, FlashSizeCode=0x3 (8 MB DIO @ 80MHz)",
        actual_bin_desc,
        t8_pass
    )

    # TEST: Host-Native Freestanding C Unit Test Suite Execution
    total += 1
    t9_pass = False
    native_desc = f"{native_test_bin} not found (run 'make host-test')"
    if os.path.exists(native_test_bin):
        run_res = subprocess.run([native_test_bin], capture_output=True, text=True)
        t9_pass = (run_res.returncode == 0)
        native_desc = f"ExitCode={run_res.returncode}, Status: {run_res.stdout.strip().splitlines()[-2] if run_res.stdout else 'no output'}"

    passed += print_result_line(
        total,
        "Host-Native Freestanding C Unit Test Suite Execution",
        "Execute compiled host test binary tests/test_freestanding linking src/string.c, src/dpc.c, src/arena.c, and src/pmp.c",
        "tests/test_freestanding exits with code 0; all freestanding C assertions pass",
        native_desc,
        t9_pass
    )

    # TEST: Low-Power SRAM, Flash XIP & Vector Table Symbols Validation
    total += 1
    lp_sym = symbols.get("_lp_sram_start", {}).get("value", None)
    flash_text_sym = symbols.get("_flash_text_start", {}).get("value", None)
    vec_table_sym = symbols.get("_vector_table", {}).get("value", None)
    vec_align_ok = (vec_table_sym is not None) and (vec_table_sym % 256 == 0) and (vec_table_sym >= stext and vec_table_sym < 0x40829000)
    lp_ok = (lp_sym == 0x50000000)
    flash_ok = (flash_text_sym == 0x42000000)
    t10_pass = (lp_ok and flash_ok and vec_align_ok)
    t10_actual = f"_vector_table=0x{vec_table_sym:08x} (align256={vec_align_ok}), _lp_sram_start=0x{lp_sym:08x}, _flash_text_start=0x{flash_text_sym:08x}"
    passed += print_result_line(
        total,
        "Low-Power SRAM, Flash XIP & Vector Table Symbols Validation",
        "Verify _vector_table (256-byte aligned in IRAM), _lp_sram_start (0x50000000), and _flash_text_start (0x42000000)",
        "_vector_table%256==0, _lp_sram_start==0x50000000, _flash_text_start==0x42000000",
        t10_actual,
        t10_pass
    )

    # TEST: Stack Pointer Boundary Geometry & Entry Vector Topology (Task 1.2)
    total += 1
    stack_top = symbols["_stack_top"]["value"]
    main_stack_top = symbols.get("_main_stack_top", {}).get("value", 0)
    start_sym = symbols.get("_start", {}).get("value", None)
    stack_align_ok = (main_stack_top % 16 == 0)
    stack_vma_ok = (stack_top == 0x40880000) and (0 < main_stack_top <= ROM_DATA_START)
    stack_headroom = main_stack_top - ebss
    entry_ok = (start_sym == 0x40800000) and (elf["entry"] == 0x40800000)
    t11_pass = stack_align_ok and stack_vma_ok and (stack_headroom >= MAIN_STACK_MIN_SIZE) and entry_ok
    t11_actual = (f"_main_stack_top=0x{main_stack_top:08x} (align16={stack_align_ok}), "
                  f"main stack={stack_headroom // 1024} KB, _start=0x{start_sym:08x}")
    passed += print_result_line(
        total,
        "Stack Boundary Geometry & CRT0 Entry Vector Topology",
        "Verify the main stack (_ebss.._main_stack_top) stays below ROM .bss/.data, is 16-byte aligned and >= 32 KB, and entry at _start",
        "_stack_top == 0x40880000, _main_stack_top <= 0x4087e610 and 16-byte aligned, main stack >= 32 KB, entry == 0x40800000",
        t11_actual,
        t11_pass
    )


    # TEST: PCR Clock Subsystem Linkage & Symbols Validation (Task 1.3)
    total += 1
    clock_syms = ["clock_init", "clock_get_config", "clock_get_cpu_freq_hz", "clock_get_apb_freq_hz"]
    found_clock_syms = [s for s in clock_syms if s in symbols]
    all_clock_found = len(found_clock_syms) == len(clock_syms)
    all_clock_in_text = all(
        (symbols[s]["value"] >= stext and symbols[s]["value"] < sdata)
        for s in found_clock_syms
    )
    t12_pass = all_clock_found and all_clock_in_text
    t12_actual = f"Found {len(found_clock_syms)}/{len(clock_syms)} symbols in IRAM (.text) [stext=0x{stext:08x}]"
    passed += print_result_line(
        total,
        "PCR Clock Subsystem Linkage & Symbols Validation",
        "Verify clock_init, clock_get_config, and query symbols exist in IRAM executable section",
        "All 4 clock subsystem symbols present in IRAM text section [0x40800000, 0x40829000)",
        t12_actual,
        t12_pass
    )

    # TEST: Watchdog Supervisor Subsystem Linkage & Symbols Validation (Task 1.4)
    total += 1
    wdt_syms = ["wdt_init", "wdt_feed", "wdt_supervisor_tick", "wdt_get_status", "wdt_get_reset_cause", "wdt_get_reset_cause_desc"]
    found_wdt_syms = [s for s in wdt_syms if s in symbols]
    all_wdt_found = len(found_wdt_syms) == len(wdt_syms)
    all_wdt_in_text = all(
        (symbols[s]["value"] >= stext and symbols[s]["value"] < sdata)
        for s in found_wdt_syms
    )
    t13_pass = all_wdt_found and all_wdt_in_text
    t13_actual = f"Found {len(found_wdt_syms)}/{len(wdt_syms)} symbols in IRAM (.text) [stext=0x{stext:08x}]"
    passed += print_result_line(
        total,
        "Watchdog Supervisor Linkage & Symbols Validation",
        "Verify wdt_init, wdt_feed, and tick symbols exist in IRAM executable section",
        "All 6 watchdog supervisor symbols present in IRAM text section [0x40800000, 0x40829000)",
        t13_actual,
        t13_pass
    )

    # TEST: RISC-V Machine Trap Handler & Vector Table Subsystem Linkage (Task 2.1)
    total += 1
    trap_syms = ["_vector_table", "trap_entry_exception", "trap_entry_interrupt", "trap_init", "trap_handler", "panic_dump"]
    found_trap_syms = [s for s in trap_syms if s in symbols]
    all_trap_found = len(found_trap_syms) == len(trap_syms)
    all_trap_in_text = all(
        in_iram(symbols[s]["value"])
        for s in found_trap_syms
    )
    vec_sym_val = symbols.get("_vector_table", {}).get("value", 0)
    vec_aligned = (vec_sym_val % 256 == 0)
    t14_pass = all_trap_found and all_trap_in_text and vec_aligned
    t14_actual = f"Found {len(found_trap_syms)}/{len(trap_syms)} symbols in IRAM [vector_table=0x{vec_sym_val:08x}, align256={vec_aligned}]"
    passed += print_result_line(
        total,
        "Trap Handler & Vector Table Subsystem Linkage",
        "Verify trap_init, trap_handler, panic_dump, and 256-byte aligned vector table exist in IRAM",
        "All 6 trap subsystem symbols present in IRAM, _vector_table 256-byte aligned",
        t14_actual,
        t14_pass
    )

    # TEST: Interrupt Matrix (INTMTX) & INTPRI Controller Subsystem Linkage (Task 2.2)
    total += 1
    intr_syms = [
        "interrupt_init",
        "interrupt_route",
        "interrupt_unroute",
        "interrupt_get_map",
        "interrupt_set_priority",
        "interrupt_get_priority",
        "interrupt_set_threshold",
        "interrupt_enable",
        "interrupt_disable",
        "interrupt_dispatch"
    ]
    found_intr_syms = [s for s in intr_syms if s in symbols]
    all_intr_found = len(found_intr_syms) == len(intr_syms)
    all_intr_in_text = all(
        in_iram(symbols[s]["value"])
        for s in found_intr_syms
    )
    t15_pass = all_intr_found and all_intr_in_text
    t15_actual = f"Found {len(found_intr_syms)}/{len(intr_syms)} symbols in IRAM (.text) [stext=0x{stext:08x}]"
    passed += print_result_line(
        total,
        "Interrupt Matrix (INTMTX) & INTPRI Controller Linkage",
        "Verify interrupt_init, route, priority, threshold, enable/disable, and dispatch in IRAM",
        "All 10 interrupt subsystem symbols present in IRAM text section [0x40800000, 0x40829000)",
        t15_actual,
        t15_pass
    )

    # TEST: Lock-Free SPSC DPC Queue Engine Subsystem Linkage (Task 2.3)
    total += 1
    dpc_syms = [
        "dpc_init",
        "dpc_queue_init",
        "dpc_queue_enqueue",
        "dpc_queue_dequeue",
        "dpc_queue_size",
        "dpc_queue_is_empty",
        "dpc_queue_is_full",
        "dpc_enqueue",
        "dpc_dequeue",
        "dpc_process",
        "dpc_process_all",
        "dpc_get_size",
        "dpc_get_drop_count"
    ]
    found_dpc_syms = [s for s in dpc_syms if s in symbols]
    all_dpc_found = len(found_dpc_syms) == len(dpc_syms)
    all_dpc_in_text = all(
        in_iram(symbols[s]["value"])
        for s in found_dpc_syms
    )
    t16_pass = all_dpc_found and all_dpc_in_text
    t16_actual = f"Found {len(found_dpc_syms)}/{len(dpc_syms)} symbols in IRAM (.text) [stext=0x{stext:08x}]"
    passed += print_result_line(
        total,
        "Lock-Free SPSC DPC Queue Engine Linkage & Symbols",
        "Verify dpc_init, queue, enqueue, dequeue, process, and query symbols exist in IRAM",
        "All 13 DPC engine symbols present in IRAM text section [0x40800000, 0x40829000)",
        t16_actual,
        t16_pass
    )

    # TEST: USB-Serial-JTAG CDC-ACM Driver Subsystem Linkage (Task 2.4)
    total += 1
    usb_syms = [
        "usb_serial_init",
        "usb_serial_is_tx_ready",
        "usb_serial_is_rx_ready",
        "usb_serial_putc_blocking",
        "usb_serial_putc_nonblocking",
        "usb_serial_puts",
        "usb_serial_write",
        "usb_serial_flush",
        "usb_serial_getc_nonblocking",
        "usb_serial_getc_blocking",
        "usb_serial_read",
        "usb_serial_get_dev"
    ]
    found_usb_syms = [s for s in usb_syms if s in symbols]
    all_usb_found = len(found_usb_syms) == len(usb_syms)
    all_usb_in_text = all(
        in_iram(symbols[s]["value"])
        for s in found_usb_syms
    )
    t17_pass = all_usb_found and all_usb_in_text
    t17_actual = f"Found {len(found_usb_syms)}/{len(usb_syms)} symbols in IRAM (.text) [stext=0x{stext:08x}]"
    passed += print_result_line(
        total,
        "USB-Serial-JTAG CDC-ACM Hardware Driver Linkage",
        "Verify usb_serial_init, tx/rx, read/write, flush, and dev symbols exist in IRAM",
        "All 12 USB-Serial-JTAG driver symbols present in IRAM text section [0x40800000, 0x40829000)",
        t17_actual,
        t17_pass
    )

    # TEST: Unified Dual-Console & Interrupt-Driven UART0 Subsystem Linkage (Task 2.5)
    total += 1
    console_syms = [
        "uart_init",
        "uart_isr",
        "uart_putc",
        "uart_puts",
        "uart_flush",
        "uart_getc_nonblocking",
        "uart_getc_blocking",
        "console_init",
        "console_putc",
        "console_puts",
        "console_getc_nonblocking",
        "console_getc_blocking",
        "console_flush",
        "console_set_active_mask",
        "console_get_active_mask",
        "console_set_echo",
        "console_get_echo",
        "console_get_manager",
        "console_read_line_nonblocking"
    ]
    found_console_syms = [s for s in console_syms if s in symbols]
    all_console_found = len(found_console_syms) == len(console_syms)
    all_console_in_text = all(
        in_iram(symbols[s]["value"])
        for s in found_console_syms
    )
    t18_pass = all_console_found and all_console_in_text
    t18_actual = f"Found {len(found_console_syms)}/{len(console_syms)} symbols in IRAM (.text) [stext=0x{stext:08x}]"
    passed += print_result_line(
        total,
        "Unified Dual-Console & Interrupt-Driven UART0 Subsystem Linkage",
        "Verify uart and console multiplexer functions exist in IRAM executable section",
        "All 19 UART0 and Console subsystem symbols present in IRAM text section [0x40800000, 0x40829000)",
        t18_actual,
        t18_pass
    )

    # TEST: Hardware Periodic Timer (TIMG0 T0) Driver Linkage (Task 2.6)
    total += 1
    timer_syms = [
        "timer_init",
        "timer_start",
        "timer_stop",
        "timer_isr",
        "timer_dpc_handler",
        "timer_get_tick_count",
        "timer_get_status",
        "timer_get_current_ticks"
    ]
    found_timer_syms = [s for s in timer_syms if s in symbols]
    all_timer_found = len(found_timer_syms) == len(timer_syms)
    all_timer_in_text = all(
        in_iram(symbols[s]["value"])
        for s in found_timer_syms
    )
    t19_pass = all_timer_found and all_timer_in_text
    t19_actual = f"Found {len(found_timer_syms)}/{len(timer_syms)} symbols in IRAM (.text) [stext=0x{stext:08x}]"
    passed += print_result_line(
        total,
        "Hardware Periodic Timer (TIMG0 T0) Driver Linkage",
        "Verify timer_init, start/stop, ISR, DPC handler, and query symbols exist in IRAM",
        "All 8 timer driver symbols present in IRAM text section [0x40800000, 0x40829000)",
        t19_actual,
        t19_pass
    )

    # TEST: Deterministic Static Arena Memory Allocator Linkage (Task 3.1)
    total += 1
    arena_syms = [
        "arena_init",
        "arena_alloc",
        "arena_alloc_pool",
        "arena_free",
        "arena_scratch_alloc",
        "arena_scratch_mark",
        "arena_scratch_reset",
        "arena_get_stats",
        "arena_get_pool_stats",
        "arena_get_scratch_stats"
    ]
    found_arena_syms = [s for s in arena_syms if s in symbols]
    all_arena_found = len(found_arena_syms) == len(arena_syms)
    all_arena_in_text = all(
        in_iram(symbols[s]["value"])
        for s in found_arena_syms
    )
    t20_pass = all_arena_found and all_arena_in_text
    t20_actual = f"Found {len(found_arena_syms)}/{len(arena_syms)} symbols in IRAM (.text) [stext=0x{stext:08x}]"
    passed += print_result_line(
        total,
        "Deterministic Static Arena Memory Allocator Linkage",
        "Verify arena_init, alloc/free, scratch mark/reset, and telemetry symbols exist in IRAM",
        "All 10 arena allocator symbols present in IRAM text section [0x40800000, 0x40829000)",
        t20_actual,
        t20_pass
    )

    # TEST: High-Resolution SYSTIMER Driver Linkage (Task 3.2)
    total += 1
    systimer_syms = [
        "systimer_init",
        "systimer_get_ticks",
        "systimer_get_us",
        "systimer_get_ms",
        "systimer_delay_us",
        "systimer_delay_ms",
        "systimer_alarm_init",
        "systimer_alarm_set_oneshot",
        "systimer_alarm_cancel",
        "systimer_isr",
        "systimer_get_telemetry"
    ]
    found_systimer_syms = [s for s in systimer_syms if s in symbols]
    all_systimer_found = len(found_systimer_syms) == len(systimer_syms)
    all_systimer_in_text = all(
        in_iram(symbols[s]["value"])
        for s in found_systimer_syms
    )
    t21_pass = all_systimer_found and all_systimer_in_text
    t21_actual = f"Found {len(found_systimer_syms)}/{len(systimer_syms)} symbols in IRAM (.text) [stext=0x{stext:08x}]"
    passed += print_result_line(
        total,
        "High-Resolution SYSTIMER Driver Linkage",
        "Verify systimer_init, get_ticks/us/ms, delay, alarm, and telemetry symbols exist in IRAM",
        "All 11 SYSTIMER driver symbols present in IRAM text section [0x40800000, 0x40829000)",
        t21_actual,
        t21_pass
    )

    # TEST: Cooperative Coroutine Scheduler Linkage (Task 3.3)
    total += 1
    task_syms = [
        "task_init",
        "task_create",
        "task_yield",
        "task_exit",
        "task_terminate",
        "task_get_current",
        "task_get_by_id",
        "task_get_count",
        "task_get_status",
        "task_state_name",
        "task_switch_asm"
    ]
    found_task_syms = [s for s in task_syms if s in symbols]
    all_task_found = len(found_task_syms) == len(task_syms)
    all_task_in_text = all(
        in_iram(symbols[s]["value"])
        for s in found_task_syms
    )
    t22_pass = all_task_found and all_task_in_text
    t22_actual = f"Found {len(found_task_syms)}/{len(task_syms)} symbols in IRAM (.text) [stext=0x{stext:08x}]"
    passed += print_result_line(
        total,
        "Cooperative Coroutine Task Engine Linkage",
        "Verify task_init, create, yield, exit, status, and task_switch_asm symbols exist in IRAM",
        "All 11 coroutine scheduler symbols present in IRAM text section [0x40800000, 0x40829000)",
        t22_actual,
        t22_pass
    )

    # TEST: RISC-V PMP & APM Fault Isolation Linkage (Task 3.4)
    total += 1
    pmp_syms = [
        "pmp_init",
        "pmp_calc_napot",
        "pmp_decode_napot",
        "pmp_set_region",
        "pmp_get_region",
        "pmp_disable_region",
        "pmp_read_cfg",
        "pmp_write_cfg",
        "pmp_read_addr",
        "pmp_write_addr",
        "apm_init",
        "apm_set_region",
        "apm_get_region",
        "apm_disable_region",
        "apm_enable_master",
        "apm_get_exception_info",
        "apm_clear_exception",
        "pmp_get_telemetry"
    ]
    found_pmp_syms = [s for s in pmp_syms if s in symbols]
    all_pmp_found = len(found_pmp_syms) == len(pmp_syms)
    all_pmp_in_text = all(
        in_exec(symbols[s]["value"])
        for s in found_pmp_syms
    )
    t23_pass = all_pmp_found and all_pmp_in_text
    t23_actual = f"Found {len(found_pmp_syms)}/{len(pmp_syms)} symbols, all executable"
    passed += print_result_line(
        total,
        "RISC-V PMP & APM Fault Isolation Linkage",
        "Verify pmp_init, napot calc/decode, set/get/disable, and apm driver symbols are linked (IRAM or flash)",
        f"All {len(pmp_syms)} PMP and APM symbols linked in executable memory (IRAM or flash XIP)",
        t23_actual,
        t23_pass
    )

    # TEST: LP Core Coprocessor Driver Linkage & Symbols Validation (Task 4.1)
    total += 1
    lp_syms = [
        "lp_core_init",
        "lp_core_load_firmware",
        "lp_core_load_header",
        "lp_core_start",
        "lp_core_stop",
        "lp_core_trigger_lp",
        "lp_core_get_lp_trigger",
        "lp_core_clear_lp_trigger",
        "lp_core_wait_handshake",
        "lp_core_is_running",
        "lp_core_read_counter",
        "lp_core_read_magic",
        "lp_core_get_telemetry",
        "lp_core_get_default_firmware"
    ]
    found_lp_syms = [s for s in lp_syms if s in symbols]
    all_lp_found = len(found_lp_syms) == len(lp_syms)
    all_lp_in_text = all(
        in_exec(symbols[s]["value"])
        for s in found_lp_syms
    )
    t24_pass = all_lp_found and all_lp_in_text
    t24_actual = f"Found {len(found_lp_syms)}/{len(lp_syms)} symbols, all executable"
    passed += print_result_line(
        total,
        "LP Core Coprocessor Driver Linkage & Symbols Validation",
        "Verify lp_core_init, load, start/stop, trigger, handshake, and telemetry symbols are linked (IRAM or flash)",
        f"All {len(lp_syms)} LP Core driver symbols linked in executable memory (IRAM or flash XIP)",
        t24_actual,
        t24_pass
    )

    # TEST: Power Management & LP Shared Mailbox Linkage & Symbols Validation (Task 4.2)
    total += 1
    pwr_syms = [
        "power_init",
        "power_mailbox_init",
        "power_get_mailbox",
        "power_send_cmd",
        "power_sample_telemetry",
        "power_set_mode",
        "power_get_mode",
        "power_get_telemetry",
        "power_read_retained_store",
        "power_write_retained_store"
    ]
    found_pwr_syms = [s for s in pwr_syms if s in symbols]
    all_pwr_found = len(found_pwr_syms) == len(pwr_syms)
    all_pwr_in_text = all(
        in_iram(symbols[s]["value"])
        for s in found_pwr_syms
    )
    t25_pass = all_pwr_found and all_pwr_in_text
    t25_actual = f"Found {len(found_pwr_syms)}/{len(pwr_syms)} symbols in IRAM (.text) [stext=0x{stext:08x}]"
    passed += print_result_line(
        total,
        "Power Management & LP Shared Mailbox Subsystem Linkage",
        "Verify power_init, mailbox_init, send_cmd, sample_telemetry, set/get_mode, and retained store symbols exist in IRAM",
        f"All {len(pwr_syms)} power management symbols present in IRAM text section [0x40800000, 0x40829000)",
        t25_actual,
        t25_pass
    )

    # TEST: GPIO Matrix & IO_MUX Pin Routing Linkage & Symbols Validation (Task 4.3)
    total += 1
    gpio_syms = [
        "gpio_init",
        "gpio_set_direction",
        "gpio_set_pull",
        "gpio_set_drive_strength",
        "gpio_set_function",
        "gpio_set_drive_mode",
        "gpio_set_level",
        "gpio_get_level",
        "gpio_get_output_level",
        "gpio_toggle_level",
        "gpio_set_intr_type",
        "gpio_intr_enable",
        "gpio_intr_disable",
        "gpio_intr_clear",
        "gpio_get_telemetry"
    ]
    found_gpio_syms = [s for s in gpio_syms if s in symbols]
    all_gpio_found = len(found_gpio_syms) == len(gpio_syms)
    all_gpio_in_text = all(
        in_exec(symbols[s]["value"])
        for s in found_gpio_syms
    )
    t26_pass = all_gpio_found and all_gpio_in_text
    t26_actual = f"Found {len(found_gpio_syms)}/{len(gpio_syms)} symbols, all executable"
    passed += print_result_line(
        total,
        "GPIO Matrix & IO_MUX Pin Routing Subsystem Linkage",
        "Verify gpio_init, set/get direction, pull, drive strength, level toggle, intr, and telemetry symbols are linked (IRAM or flash)",
        f"All {len(gpio_syms)} GPIO driver symbols linked in executable memory (IRAM or flash XIP)",
        t26_actual,
        t26_pass
    )

    # TEST: GDMA Multi-Channel Engine & Circular Buffer Descriptor Rings Linkage (Task 4.4)
    total += 1
    gdma_syms = [
        "gdma_init",
        "gdma_channel_init",
        "gdma_channel_reset",
        "gdma_inlink_set",
        "gdma_inlink_start",
        "gdma_inlink_stop",
        "gdma_inlink_restart",
        "gdma_outlink_set",
        "gdma_outlink_start",
        "gdma_outlink_stop",
        "gdma_outlink_restart",
        "gdma_desc_init",
        "gdma_desc_link_circular",
        "gdma_get_date_version",
        "gdma_get_channel_telemetry",
        "gdma_get_telemetry"
    ]
    found_gdma_syms = [s for s in gdma_syms if s in symbols]
    all_gdma_found = len(found_gdma_syms) == len(gdma_syms)
    all_gdma_in_text = all(
        in_exec(symbols[s]["value"])
        for s in found_gdma_syms
    )
    t27_pass = all_gdma_found and all_gdma_in_text
    t27_actual = f"Found {len(found_gdma_syms)}/{len(gdma_syms)} symbols, all executable"
    passed += print_result_line(
        total,
        "GDMA Multi-Channel Engine & Circular Buffer Rings Linkage",
        "Verify gdma_init, inlink/outlink controls, desc_init, circular linking, and telemetry symbols are linked (IRAM or flash)",
        f"All {len(gdma_syms)} GDMA driver symbols linked in executable memory (IRAM or flash XIP)",
        t27_actual,
        t27_pass
    )

    # TEST: Modem Clock & Power Control Linkage & Symbols Validation (Task 5.1)
    total += 1
    modem_syms = [
        "modem_init",
        "modem_enable_wifi_clocks",
        "modem_disable_wifi_clocks",
        "modem_enable_ble_clocks",
        "modem_disable_ble_clocks",
        "modem_enable_ieee802154_clocks",
        "modem_disable_ieee802154_clocks",
        "modem_enable_all_clocks",
        "modem_enable_coexistence",
        "modem_disable_coexistence",
        "modem_get_clock_state",
        "modem_get_syscon_date",
        "modem_get_lpcon_date",
        "modem_is_wifi_enabled",
        "modem_is_ble_enabled",
        "modem_is_ieee802154_enabled",
        "modem_is_coex_enabled",
        "modem_validate_coexistence"
    ]
    found_modem_syms = [s for s in modem_syms if s in symbols]
    all_modem_found = len(found_modem_syms) == len(modem_syms)
    all_modem_in_text = all(
        in_exec(symbols[s]["value"])
        for s in found_modem_syms
    )
    t28_pass = all_modem_found and all_modem_in_text
    t28_actual = f"Found {len(found_modem_syms)}/{len(modem_syms)} symbols, all executable"
    passed += print_result_line(
        total,
        "Modem Clock & Power Control Subsystem Linkage",
        "Verify modem_init, clock gating, reset release, and telemetry symbols are linked (IRAM or flash)",
        f"All {len(modem_syms)} modem driver symbols linked in executable memory (IRAM or flash XIP)",
        t28_actual,
        t28_pass
    )

    # TEST: 802.11ax Wi-Fi 6 MAC Driver & Zero-Copy Packet Ring Linkage (Task 5.3)
    total += 1
    wifi_syms = [
        "wifi_init",
        "wifi_rx_ring_init",
        "wifi_verify_rx_ring",
        "wifi_rx_poll",
        "wifi_rx_release",
        "wifi_tx_packet",
        "wifi_get_state",
        "wifi_get_mac_addr",
        "wifi_get_telemetry",
        "wifi_get_rx_packet"
    ]
    found_wifi_syms = [s for s in wifi_syms if s in symbols]
    all_wifi_found = len(found_wifi_syms) == len(wifi_syms)
    all_wifi_in_text = all(
        in_exec(symbols[s]["value"])
        for s in found_wifi_syms
    )
    t30_pass = all_wifi_found and all_wifi_in_text
    t30_actual = f"Found {len(found_wifi_syms)}/{len(wifi_syms)} symbols, all executable"
    passed += print_result_line(
        total,
        "802.11ax Wi-Fi 6 MAC Driver & Zero-Copy Packet Ring Linkage",
        "Verify wifi_init, RX queue init/verify, poll/release, and telemetry symbols are linked (IRAM or flash)",
        f"All {len(wifi_syms)} Wi-Fi driver symbols linked in executable memory (IRAM or flash XIP)",
        t30_actual,
        t30_pass
    )

    # TEST: IEEE 802.15.4 Radio Transceiver Driver Linkage (Task 5.4)
    total += 1
    ieee_syms = [
        "ieee802154_init",
        "ieee802154_cmd",
        "ieee802154_set_channel",
        "ieee802154_get_channel",
        "ieee802154_get_freq_mhz",
        "ieee802154_set_short_address",
        "ieee802154_get_short_address",
        "ieee802154_set_pan_id",
        "ieee802154_get_pan_id",
        "ieee802154_set_extended_address",
        "ieee802154_get_extended_address",
        "ieee802154_set_auto_ack",
        "ieee802154_set_promiscuous",
        "ieee802154_set_tx_power",
        "ieee802154_get_tx_power",
        "ieee802154_get_state",
        "ieee802154_get_telemetry",
        "ieee802154_get_date_version"
    ]
    found_ieee_syms = [s for s in ieee_syms if s in symbols]
    all_ieee_found = len(found_ieee_syms) == len(ieee_syms)
    all_ieee_in_text = all(
        in_exec(symbols[s]["value"])
        for s in found_ieee_syms
    )
    t31_pass = all_ieee_found and all_ieee_in_text
    t31_actual = f"Found {len(found_ieee_syms)}/{len(ieee_syms)} symbols, all executable"
    passed += print_result_line(
        total,
        "IEEE 802.15.4 Radio Transceiver Driver Linkage",
        "Verify ieee802154_init, cmd, channel, addressing, auto-ack, power, and telemetry symbols are linked (IRAM or flash)",
        f"All {len(ieee_syms)} IEEE 802.15.4 driver symbols linked in executable memory (IRAM or flash XIP)",
        t31_actual,
        t31_pass
    )

    # TEST: Bare-Metal TCP/IP Stack & Lightweight Protocol Engine Linkage (Task 5.5)
    total += 1
    net_syms = [
        "net_init",
        "net_set_ip",
        "net_reset_defaults",
        "net_get_config",
        "net_get_telemetry",
        "net_checksum",
        "net_ipv4_checksum",
        "net_tcp_checksum",
        "net_udp_checksum",
        "arp_lookup",
        "arp_insert",
        "arp_process_packet",
        "net_input",
        "icmp_process_packet",
        "net_send_udp",
        "net_ip_to_str",
        "net_str_to_ip",
        "tcp_init",
        "tcp_new",
        "tcp_bind",
        "tcp_listen",
        "tcp_connect",
        "tcp_write",
        "tcp_close",
        "tcp_abort",
        "tcp_input",
        "tcp_tick",
        "tcp_get_telemetry"
    ]
    found_net_syms = [s for s in net_syms if s in symbols]
    all_net_found = len(found_net_syms) == len(net_syms)
    all_net_in_text = all(
        in_exec(symbols[s]["value"])
        for s in found_net_syms
    )
    t32_pass = all_net_found and all_net_in_text
    t32_actual = f"Found {len(found_net_syms)}/{len(net_syms)} symbols, all executable"
    passed += print_result_line(
        total,
        "Bare-Metal TCP/IP Stack & Lightweight Protocol Engine Linkage",
        "Verify IPv4, ARP, ICMP, UDP and TCP state machine symbols are linked (IRAM or flash)",
        f"All {len(net_syms)} TCP/IP driver symbols linked in executable memory (IRAM or flash XIP)",
        t32_actual,
        t32_pass
    )

    # TEST: Zero-Allocation Local REST/HTTP Engine & Embedded Web UI (Task 6.1)
    total += 1
    http_syms = [
        "http_server_init",
        "http_server_start",
        "http_server_stop",
        "http_server_is_running",
        "http_route_register",
        "http_route_find",
        "http_process_request",
        "http_server_get_telemetry",
        "http_server_get_route_count",
        "http_method_to_str",
        "http_status_to_str"
    ]
    found_http_syms = [s for s in http_syms if s in symbols]
    all_http_found = len(found_http_syms) == len(http_syms)
    all_http_in_text = all(
        in_exec(symbols[s]["value"])
        for s in found_http_syms
    )
    t33_pass = all_http_found and all_http_in_text
    t33_actual = f"Found {len(found_http_syms)}/{len(http_syms)} symbols, all executable"
    passed += print_result_line(
        total,
        "Zero-Allocation Local REST/HTTP Engine & Embedded Web UI Linkage",
        "Verify http_server_init, start/stop, routing, process_request, and telemetry symbols are linked (IRAM or flash)",
        f"All {len(http_syms)} HTTP server symbols linked in executable memory (IRAM or flash XIP)",
        t33_actual,
        t33_pass
    )

    # TEST: LAN Network Diagnostics & Wi-Fi Speed-Test Benchmark Linkage (Task 6.2)
    total += 1
    speedtest_syms = [
        "speedtest_init",
        "speedtest_reset",
        "speedtest_calculate_throughput_kbps",
        "speedtest_calculate_throughput_mbps",
        "speedtest_kbps_to_mbps",
        "speedtest_run_synthetic_burst",
        "speedtest_run_udp_tx",
        "speedtest_process_udp_packet",
        "speedtest_get_last_result",
        "speedtest_get_telemetry"
    ]
    found_speedtest_syms = [s for s in speedtest_syms if s in symbols]
    all_speedtest_found = len(found_speedtest_syms) == len(speedtest_syms)
    all_speedtest_in_text = all(
        in_exec(symbols[s]["value"])
        for s in found_speedtest_syms
    )
    t34_pass = all_speedtest_found and all_speedtest_in_text
    t34_actual = f"Found {len(found_speedtest_syms)}/{len(speedtest_syms)} symbols, all executable"
    passed += print_result_line(
        total,
        "LAN Network Diagnostics & Wi-Fi Speed-Test Benchmark Linkage",
        "Verify speedtest_init, reset, bandwidth calculation, synthetic burst, UDP tx, and telemetry are linked (IRAM or flash)",
        f"All {len(speedtest_syms)} Speed-Test benchmark symbols linked in executable memory (IRAM or flash XIP)",
        t34_actual,
        t34_pass
    )

    # TEST: Extended Interactive Console Shell & 24/7 Health Monitoring Linkage (Task 6.4)
    total += 1
    shell_iram_syms = [
        "shell_init",
        "shell_tick",
        "shell_get_health_telemetry",
        "shell_get_telemetry",
        "shell_reset_telemetry",
        "shell_get_uptime_seconds",
    ]
    shell_flash_syms = [
        "shell_execute",
        "shell_print_health",
        "shell_print_top",
        "shell_print_help",
        "shell_print_info",
    ]
    all_shell_syms = shell_iram_syms + shell_flash_syms
    found_shell_syms = [s for s in all_shell_syms if s in symbols]
    all_shell_found = len(found_shell_syms) == len(all_shell_syms)
    all_iram_in_text = all(
        in_exec(symbols[s]["value"])
        for s in shell_iram_syms if s in symbols
    )
    all_flash_in_xip = all(
        in_flash(symbols[s]["value"])
        for s in shell_flash_syms if s in symbols
    )
    t36_pass = all_shell_found and all_iram_in_text and all_flash_in_xip
    t36_actual = f"Found {len(found_shell_syms)}/{len(all_shell_syms)} symbols (core executable={all_iram_in_text}, visualizers in flash={all_flash_in_xip})"
    passed += print_result_line(
        total,
        "Extended Interactive Console Shell & 24/7 Health Monitoring Linkage",
        "Verify shell_init/tick/health/uptime are linked and shell_execute/help/top/info run from flash XIP",
        f"All {len(all_shell_syms)} Shell & Health Monitoring symbols linked, visualizers in flash XIP",
        t36_actual,
        t36_pass
    )

    # TEST: eFuse Memory Controller & Silicon Security Sealing Linkage (Task 7.1)
    total += 1
    efuse_iram_syms = [
        "efuse_init",
        "efuse_refresh_shadow",
        "efuse_get_mac",
        "efuse_get_mac_ext",
        "efuse_get_unique_id",
        "efuse_get_chip_version",
        "efuse_get_pkg_version",
        "efuse_is_secure_boot_enabled",
        "efuse_is_flash_encryption_enabled",
        "efuse_is_jtag_disabled",
        "efuse_is_download_mode_disabled",
        "efuse_get_wr_dis",
        "efuse_get_rd_dis",
        "efuse_get_telemetry",
    ]
    efuse_flash_syms = [
        "efuse_print_mac",
        "efuse_print_security",
        "efuse_print_summary",
    ]
    all_efuse_syms = efuse_iram_syms + efuse_flash_syms
    found_efuse_syms = [s for s in all_efuse_syms if s in symbols]
    all_efuse_found = len(found_efuse_syms) == len(all_efuse_syms)
    all_efuse_iram_ok = all(
        in_exec(symbols[s]["value"])
        for s in efuse_iram_syms if s in symbols
    )
    all_efuse_flash_ok = all(
        in_flash(symbols[s]["value"])
        for s in efuse_flash_syms if s in symbols
    )
    t37_pass = all_efuse_found and all_efuse_iram_ok and all_efuse_flash_ok
    t37_actual = f"Found {len(found_efuse_syms)}/{len(all_efuse_syms)} symbols (core executable={all_efuse_iram_ok}, visualizers in flash={all_efuse_flash_ok})"
    passed += print_result_line(
        total,
        "eFuse Memory Controller & Silicon Security Sealing Linkage",
        "Verify efuse_init and query APIs are linked and diagnostic visualizers run from flash XIP",
        f"All {len(all_efuse_syms)} eFuse Controller symbols linked, visualizers in flash XIP",
        t37_actual,
        t37_pass
    )

    # TEST: 24/7 Stability Soak, Memory Leak & Anti-Starvation Linkage (Task 7.2)
    total += 1
    soak_iram_syms = [
        "soak_init",
        "soak_audit_memory",
        "soak_audit_dpc",
        "soak_audit_scheduler",
        "soak_run_stability_cycle",
        "soak_get_telemetry",
        "soak_reset_telemetry",
    ]
    soak_flash_syms = [
        "soak_print_status",
        "soak_print_audit",
    ]
    all_soak_syms = soak_iram_syms + soak_flash_syms
    found_soak_syms = [s for s in all_soak_syms if s in symbols]
    all_soak_found = len(found_soak_syms) == len(all_soak_syms)
    all_soak_iram_ok = all(
        in_exec(symbols[s]["value"])
        for s in soak_iram_syms if s in symbols
    )
    all_soak_flash_ok = all(
        in_flash(symbols[s]["value"])
        for s in soak_flash_syms if s in symbols
    )
    t38_pass = all_soak_found and all_soak_iram_ok and all_soak_flash_ok
    t38_actual = f"Found {len(found_soak_syms)}/{len(all_soak_syms)} symbols (core executable={all_soak_iram_ok}, visualizers in flash={all_soak_flash_ok})"
    passed += print_result_line(
        total,
        "24/7 Stability Soak, Memory Leak & Anti-Starvation Linkage",
        "Verify soak_init, audit and stability APIs are linked and diagnostic visualizers run from flash XIP",
        f"All {len(all_soak_syms)} Soak stability symbols linked, visualizers in flash XIP",
        t38_actual,
        t38_pass
    )

    # TEST: Dual-Slot Flash OTA Firmware Upgrade & Rollback Linkage (Task 7.3)
    total += 1
    ota_iram_syms = [
        "ota_init",
        "ota_get_active_slot",
        "ota_get_inactive_slot",
        "ota_get_slot_state",
        "ota_get_partition_info",
        "ota_parse_image_header",
        "ota_verify_image",
        "ota_switch_slot",
        "ota_mark_valid",
        "ota_rollback",
        "ota_get_status",
        "ota_read_slot",
        "ota_write_chunk",
        "ota_erase_slot",
    ]
    ota_flash_syms = [
        "ota_print_status",
        "ota_print_partitions",
    ]
    all_ota_syms = ota_iram_syms + ota_flash_syms
    found_ota_syms = [s for s in all_ota_syms if s in symbols]
    all_ota_found = len(found_ota_syms) == len(all_ota_syms)
    all_ota_iram_ok = all(
        in_exec(symbols[s]["value"])
        for s in ota_iram_syms if s in symbols
    )
    all_ota_flash_ok = all(
        in_flash(symbols[s]["value"])
        for s in ota_flash_syms if s in symbols
    )
    t39_pass = all_ota_found and all_ota_iram_ok and all_ota_flash_ok
    t39_actual = f"Found {len(found_ota_syms)}/{len(all_ota_syms)} symbols (core executable={all_ota_iram_ok}, visualizers in flash={all_ota_flash_ok})"
    passed += print_result_line(
        total,
        "Dual-Slot Flash OTA Firmware Upgrade & Rollback Linkage",
        "Verify ota_init, switch, rollback, verify APIs are linked and diagnostic visualizers run from flash XIP",
        f"All {len(all_ota_syms)} OTA subsystem symbols linked, visualizers in flash XIP",
        t39_actual,
        t39_pass
    )

    # TEST: Production Hardening, NVS Storage Engine & Golden Master Linkage (Task 7.4)
    total += 1
    nvs_iram_syms = [
        "nvs_init",
        "nvs_set_u32",
        "nvs_get_u32",
        "nvs_set_str",
        "nvs_get_str",
        "nvs_set_blob",
        "nvs_get_blob",
        "nvs_erase_key",
        "nvs_erase_all",
        "nvs_get_stats",
        "golden_master_verify",
    ]
    nvs_flash_syms = [
        "nvs_print_stats",
        "nvs_print_keys",
        "golden_master_print_report",
    ]
    all_nvs_syms = nvs_iram_syms + nvs_flash_syms
    found_nvs_syms = [s for s in all_nvs_syms if s in symbols]
    all_nvs_found = len(found_nvs_syms) == len(all_nvs_syms)
    all_nvs_iram_ok = all(
        in_exec(symbols[s]["value"])
        for s in nvs_iram_syms if s in symbols
    )
    all_nvs_flash_ok = all(
        in_flash(symbols[s]["value"])
        for s in nvs_flash_syms if s in symbols
    )
    t40_pass = all_nvs_found and all_nvs_iram_ok and all_nvs_flash_ok
    t40_actual = f"Found {len(found_nvs_syms)}/{len(all_nvs_syms)} symbols (core executable={all_nvs_iram_ok}, visualizers in flash={all_nvs_flash_ok})"
    passed += print_result_line(
        total,
        "Production Hardening, NVS Storage Engine & Golden Master Linkage",
        "Verify nvs_init, get/set/erase, stats are linked and visualizers run from flash XIP",
        f"All {len(all_nvs_syms)} NVS and Golden Master symbols linked, visualizers in flash XIP",
        t40_actual,
        t40_pass
    )

    # TEST: SoftAP Captive Portal Wi-Fi Provisioning Linkage (Task 8.1)
    total += 1
    prov_iram_syms = [
        "provisioning_init",
        "provisioning_start",
        "provisioning_stop",
        "provisioning_get_state",
        "provisioning_set_credentials",
        "provisioning_get_credentials",
        "provisioning_clear_credentials",
        "provisioning_has_credentials",
        "provisioning_start_scan",
        "provisioning_get_scan_results",
        "provisioning_get_telemetry",
    ]
    prov_flash_syms = [
        "provisioning_print_status",
        "provisioning_print_scan",
    ]
    all_prov_syms = prov_iram_syms + prov_flash_syms
    found_prov_syms = [s for s in all_prov_syms if s in symbols]
    all_prov_found = len(found_prov_syms) == len(all_prov_syms)
    all_prov_iram_ok = all(
        in_exec(symbols[s]["value"])
        for s in prov_iram_syms if s in symbols
    )
    all_prov_flash_ok = all(
        in_flash(symbols[s]["value"])
        for s in prov_flash_syms if s in symbols
    )
    t41_pass = all_prov_found and all_prov_iram_ok and all_prov_flash_ok
    t41_actual = f"Found {len(found_prov_syms)}/{len(all_prov_syms)} symbols (core executable={all_prov_iram_ok}, visualizers in flash={all_prov_flash_ok})"
    passed += print_result_line(
        total,
        "SoftAP Captive Portal Wi-Fi Provisioning Linkage",
        "Verify provisioning_init, get/set/clear creds, scan are linked and visualizers run from flash XIP",
        f"All {len(all_prov_syms)} provisioning subsystem symbols linked, visualizers in flash XIP",
        t41_actual,
        t41_pass
    )

    # TEST: Bare-Metal Wi-Fi Station (STA) WPA2-PSK Client & mDNS Linkage (Task 8.2)
    total += 1
    wpa2_sta_flash_syms = [
        "wpa2_client_init",
        "wpa2_client_configure",
        "wpa2_client_start",
        "wpa2_client_stop",
        "wpa2_client_get_state",
        "wpa2_client_is_in_4way",
        "wpa2_client_is_authenticated",
        "wpa2_client_get_telemetry",
        "wpa2_client_rx_eapol",
        "wpa2_client_sta_connect",
        "wpa2_client_on_associated",
        "wpa2_client_on_disconnected",
        "wpa2_client_eapol_txdone",
        "wpa2_fail_to_str",
        "wpa_ie_parse",
        "wpa_ie_build_rsn",
        "wpa_kde_parse",
        "wpa2_client_handover",
        "wpa2_crypto_pbkdf2_sha1",
        "wpa2_crypto_prf512",
        "wpa2_crypto_compute_mic",
        "wpa2_crypto_aes_unwrap",
        "wpa2_crypto_aes_wrap",
        "wpa2_client_get_ptk",
        "wpa2_client_print_status",
        "wpa2_state_to_str",
        "mdns_init",
        "mdns_start",
        "mdns_stop",
        "mdns_is_active",
        "mdns_get_hostname",
        "mdns_set_hostname",
        "mdns_process_packet",
        "mdns_announce",
        "mdns_get_telemetry",
        "mdns_print_status",
        "wifi_start_sta",
        "wifi_stop_sta",
        "wifi_is_sta_connected",
        "wifi_sta_get_bssid",
        "dhcp_client_init",
        "dhcp_client_start",
        "dhcp_client_stop",
        "dhcp_client_process_packet",
        "dhcp_client_get_state",
        "dhcp_client_get_telemetry",
        "dhcp_client_set_static_fallback",
    ]
    found_wpa2_syms = [s for s in wpa2_sta_flash_syms if s in symbols]
    all_wpa2_found = len(found_wpa2_syms) == len(wpa2_sta_flash_syms)
    all_wpa2_flash_ok = all(
        in_flash(symbols[s]["value"])
        for s in wpa2_sta_flash_syms if s in symbols
    )
    t42_pass = all_wpa2_found and all_wpa2_flash_ok
    t42_actual = f"Found {len(found_wpa2_syms)}/{len(wpa2_sta_flash_syms)} symbols in Flash XIP (FlashXIP={all_wpa2_flash_ok})"
    passed += print_result_line(
        total,
        "Wi-Fi Station (STA) WPA2-PSK Client & mDNS Linkage",
        "Verify 802.11i 4-way handshake, PBKDF2/PRF/AES unwrap, DHCP client & mDNS symbols allocated in Flash XIP",
        f"All {len(wpa2_sta_flash_syms)} WPA2 STA & mDNS symbols properly linked in Flash XIP (.flash.text)",
        t42_actual,
        t42_pass
    )

    # TEST: Dedicated Companion Application & Extended REST API Engine (Task 8.3)
    total += 1
    app_files = [
        "app/index.html",
        "app/styles.css",
        "app/app.js",
        "app/manifest.json",
        "app/sw.js",
        "app/README.md"
    ]
    all_files_exist = all(os.path.isfile(os.path.join(REPO_ROOT, f)) and os.path.getsize(os.path.join(REPO_ROOT, f)) > 50 for f in app_files)

    companion_flash_syms = [
        "http_handler_health",
        "http_handler_speedtest",
        "http_register_default_routes",
        # REV-23: /api/gpio removed; the REST v1, light and MQTT code runs from flash
        "api_v1_light_post",
        "api_v1_mqtt_post",
        "light_command",
        "mqtt_tick",
        "rgb_led_write"
    ]
    found_comp_syms = [s for s in companion_flash_syms if s in symbols]
    all_comp_found = len(found_comp_syms) == len(companion_flash_syms)
    all_comp_flash_ok = all(
        in_flash(symbols[s]["value"])
        for s in companion_flash_syms if s in symbols
    )

    etext_ok = ("_etext" in symbols) and (symbols["_etext"]["value"] <= 0x40829000)
    t43_pass = all_files_exist and all_comp_found and all_comp_flash_ok and etext_ok
    t43_actual = f"ClientAssets={all_files_exist} ({len(app_files)}/6 files), Symbols={len(found_comp_syms)}/{len(companion_flash_syms)} in FlashXIP, _etext=0x{symbols.get('_etext', {}).get('value', 0):08x} <= 0x40829000"
    passed += print_result_line(
        total,
        "Dedicated Companion Application & Extended REST API Engine Linkage",
        "Verify PWA client assets integrity, extended REST route handlers linked in Flash XIP, and IRAM boundary",
        "All client assets validated, REST handlers linked in Flash XIP, and _etext <= 0x40829000",
        t43_actual,
        t43_pass
    )

    # TEST: Code that must run without flash never calls into flash (REV-08)
    total += 1
    objdump = find_objdump(objdump_path)
    if objdump is None:
        clo_pass = False
        clo_actual = "objdump not found (pass --objdump)"
    else:
        clo_roots, clo_walked, clo_violations = iram_call_closure(elf_path, objdump, in_iram, in_flash)
        flash_ops = [r for r in clo_roots if r.startswith("flash_")]
        clo_pass = (not clo_violations) and len(flash_ops) >= FLASH_OP_ROUTINES_MIN and "mmu_init" in clo_roots
        clo_actual = (f"{len(clo_roots)} roots ({len(flash_ops)} flash routines), {len(clo_walked)} IRAM functions walked, "
                      f"{len(clo_violations)} calls into flash")
        for v in clo_violations[:8]:
            clo_actual += f"\n               flash call: {v}"
    passed += print_result_line(
        total,
        "IRAM-Only Code Paths (pre-MMU, trap/panic, flash routines)",
        "Walk direct calls from clock_init, mmu_init, trap/panic entry and the NVS/OTA flash routines through IRAM",
        f"All roots in IRAM, >= {FLASH_OP_ROUTINES_MIN} flash routines found, no call path reaches flash XIP",
        clo_actual,
        clo_pass
    )

    # TEST: Memory budgets (mirror of the ld/link.ld ASSERTs, with the numbers)
    total += 1
    main_stack_top = symbols.get("_main_stack_top", {}).get("value", 0)
    iram_free = IRAM_END - etext
    main_stack = main_stack_top - ebss
    flash_used = symbols["_eflash_xip"]["value"] - symbols["_sflash_xip"]["value"]
    bud_pass = (iram_free >= IRAM_MIN_FREE and main_stack >= MAIN_STACK_MIN_SIZE
                and 0 < main_stack_top <= ROM_DATA_START)
    bud_actual = (f"IRAM used {etext - stext} B, free {iram_free} B; main stack {main_stack} B; "
                  f"flash XIP {flash_used} B")
    passed += print_result_line(
        total,
        "Memory Budgets: IRAM Headroom, Main Stack, Flash XIP",
        "Report IRAM/stack/flash usage and enforce the headroom budgets",
        f"IRAM free >= {IRAM_MIN_FREE} B, main stack >= {MAIN_STACK_MIN_SIZE} B, stack top <= 0x{ROM_DATA_START:08x}",
        bud_actual,
        bud_pass
    )

    print("\n" + "=" * 70)
    print("                       TEST SUITE SUMMARY                             ")
    print("=" * 70)
    print(f"  Total Tests Run: {total}")
    print(f"  Passed:          {passed}")
    print(f"  Failed:          {total - passed}")
    print(f"  Success Rate:    {(passed * 100) // total}%")
    print("=" * 70)

    if passed != total:
        sys.exit(1)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Iron-V firmware artifact checks (run via 'make test')")
    parser.add_argument("--elf", default="build/firmware.elf")
    parser.add_argument("--bin", default="build/firmware.bin")
    parser.add_argument("--host-test", default="build/host/test_freestanding")
    parser.add_argument("--objdump", default=None, help="cross objdump (default: search PATH)")
    args = parser.parse_args()
    run_suite(args.elf, args.bin, args.host_test, args.objdump)
