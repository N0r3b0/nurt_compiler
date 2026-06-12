#!/usr/bin/env python3
"""benchmark.py - empirical benchmarking suite for the Nurt compiler.

Compares 5 algorithmically identical programs compiled by:
  * nurtc  (Nurt -> NASM x86_64 -> nasm -f elf64 -> gcc -no-pie link)
  * gcc    (C, -O0, no optimizations)

Collected metrics per benchmark:
  1. Static instruction counts parsed from the emitted assembly
     (.asm = NASM/Intel syntax from nurtc, .s = AT&T syntax from gcc -O0 -S):
       - total hardware instructions (labels/comments/directives excluded)
       - jumps & branches (jmp + all conditional jcc)
       - memory/data movement ops (mov*, push*, pop*)
  2. Memory footprint: average "Maximum resident set size" over N runs
     of each binary under /usr/bin/time -v.
  3. Executable size: exact byte size of the stripped binaries.
  4. Compilation speed: average wall-clock milliseconds for the
     source -> assembly step (nurtc vs gcc -O0 -S).

Usage: python3 scripts/benchmark.py [--runs 50] [--compile-reps 30]
"""

import argparse
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCH_DIR = ROOT / "benchmarks"
NURTC = ROOT / "build" / "nurtc"
TIME_BIN = "/usr/bin/time"

BENCHMARKS = ["witaj", "euklides", "kalkulator", "liczba_pierwsza", "rok_przestepny"]

# x86-64 jump/branch mnemonics: jmp and the whole jcc family.
JUMP_RE = re.compile(r"^j[a-z]{1,4}$")
# Data-movement mnemonics (both Intel and AT&T suffixed forms):
# mov, movq/movl/movzx/movsx/movabs..., push/pushq, pop/popq.
MEMOP_RE = re.compile(r"^(mov|push|pop)[a-z]*$")

# NASM pseudo-instructions that define data, not code.
NASM_DATA_DEFS = {"db", "dw", "dd", "dq", "resb", "resw", "resd", "resq", "equ", "times"}
NASM_DIRECTIVES = {"bits", "default", "global", "extern", "section", "align", "%define"}


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw)


# --------------------------------------------------------------------------
# 1. Static analysis of the emitted assembly
# --------------------------------------------------------------------------

def parse_nasm(path: Path) -> dict:
    """Count instructions in NASM (Intel syntax) output of nurtc."""
    stats = {"total": 0, "jumps": 0, "memops": 0}
    for raw in path.read_text().splitlines():
        line = raw.split(";", 1)[0].strip()          # strip comments
        if not line:
            continue
        # Strip an optional leading label ("napis_0:", ".petla_1:" ...).
        m = re.match(r"^[.\w]+:\s*(.*)$", line)
        if m:
            line = m.group(1).strip()
            if not line:
                continue
        mnemonic = line.split()[0].lower()
        if mnemonic in NASM_DIRECTIVES or mnemonic in NASM_DATA_DEFS:
            continue
        stats["total"] += 1
        if JUMP_RE.match(mnemonic):
            stats["jumps"] += 1
        elif MEMOP_RE.match(mnemonic):
            stats["memops"] += 1
    return stats


def parse_gas(path: Path) -> dict:
    """Count instructions in AT&T syntax output of gcc -S."""
    stats = {"total": 0, "jumps": 0, "memops": 0}
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()          # strip comments
        if not line:
            continue
        if line.startswith("."):                     # assembler directive
            continue
        if re.match(r"^[.\w$]+:$", line):            # label
            continue
        m = re.match(r"^[.\w$]+:\s*(.*)$", line)     # label + code on one line
        if m:
            line = m.group(1).strip()
            if not line or line.startswith("."):
                continue
        mnemonic = line.split()[0].lower()
        stats["total"] += 1
        if JUMP_RE.match(mnemonic):
            stats["jumps"] += 1
        elif MEMOP_RE.match(mnemonic):
            stats["memops"] += 1
    return stats


# --------------------------------------------------------------------------
# 2. Compilation speed (source -> assembly), milliseconds
# --------------------------------------------------------------------------

def time_compile(cmd, reps: int) -> float:
    """Average wall-clock ms of `cmd` over `reps` runs (after 3 warmups)."""
    for _ in range(3):
        run(cmd)
    samples = []
    for _ in range(reps):
        t0 = time.perf_counter()
        run(cmd)
        samples.append((time.perf_counter() - t0) * 1000.0)
    return statistics.mean(samples)


# --------------------------------------------------------------------------
# 3. Memory footprint via /usr/bin/time -v
# --------------------------------------------------------------------------

RSS_RE = re.compile(r"Maximum resident set size \(kbytes\):\s*(\d+)")


def measure_rss(binary: Path, runs: int) -> float:
    """Average max RSS (kB) of `binary` over `runs` executions."""
    samples = []
    for _ in range(runs):
        proc = subprocess.run(
            [TIME_BIN, "-v", str(binary)],
            check=True, capture_output=True, text=True,
        )
        m = RSS_RE.search(proc.stderr)
        if not m:
            raise RuntimeError(f"no RSS line in /usr/bin/time output for {binary}")
        samples.append(int(m.group(1)))
    return statistics.mean(samples)


# --------------------------------------------------------------------------
# Build pipeline
# --------------------------------------------------------------------------

def build_all(name: str, compile_reps: int) -> dict:
    nrt = BENCH_DIR / f"{name}.nrt"
    c_src = BENCH_DIR / f"{name}.c"
    asm_nurt = BENCH_DIR / f"{name}.asm"
    asm_c = BENCH_DIR / f"{name}_c.s"
    obj_nurt = BENCH_DIR / f"{name}_nurt.o"
    bin_nurt = BENCH_DIR / f"{name}_nurt"
    bin_c = BENCH_DIR / f"{name}_c"

    # source -> assembly (the timed, comparable step)
    nurt_ms = time_compile([str(NURTC), str(nrt), "-o", str(asm_nurt)], compile_reps)
    gcc_ms = time_compile(["gcc", "-O0", "-S", str(c_src), "-o", str(asm_c)], compile_reps)

    # assembly -> binary
    run(["nasm", "-f", "elf64", str(asm_nurt), "-o", str(obj_nurt)])
    run(["gcc", "-no-pie", str(obj_nurt), "-o", str(bin_nurt)])
    run(["gcc", "-O0", str(c_src), "-o", str(bin_c)])

    # behavioural equivalence gate: outputs must be byte-identical
    out_nurt = subprocess.run([str(bin_nurt)], capture_output=True).stdout
    out_c = subprocess.run([str(bin_c)], capture_output=True).stdout
    if out_nurt != out_c:
        raise RuntimeError(f"output mismatch for benchmark '{name}'")

    # stripped copies for exact size measurement
    strip_nurt = BENCH_DIR / f"{name}_nurt.stripped"
    strip_c = BENCH_DIR / f"{name}_c.stripped"
    shutil.copy2(bin_nurt, strip_nurt)
    shutil.copy2(bin_c, strip_c)
    run(["strip", str(strip_nurt)])
    run(["strip", str(strip_c)])

    return {
        "asm_nurt": asm_nurt, "asm_c": asm_c,
        "bin_nurt": bin_nurt, "bin_c": bin_c,
        "size_nurt": strip_nurt.stat().st_size,
        "size_c": strip_c.stat().st_size,
        "compile_ms_nurt": nurt_ms, "compile_ms_gcc": gcc_ms,
    }


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------

def ratio(a, b):
    return f"{a / b:.2f}x" if b else "n/a"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--runs", type=int, default=50,
                    help="executions per binary for the RSS average (default 50)")
    ap.add_argument("--compile-reps", type=int, default=30,
                    help="repetitions for compile-time averaging (default 30)")
    args = ap.parse_args()

    if not NURTC.exists():
        print(f"error: nurtc not found at {NURTC}; build it first", file=sys.stderr)
        return 1

    results = {}
    for name in BENCHMARKS:
        print(f"[*] {name}: compiling and measuring ...", file=sys.stderr)
        art = build_all(name, args.compile_reps)
        ins_nurt = parse_nasm(art["asm_nurt"])
        ins_c = parse_gas(art["asm_c"])
        rss_nurt = measure_rss(art["bin_nurt"], args.runs)
        rss_c = measure_rss(art["bin_c"], args.runs)
        results[name] = {
            "ins_nurt": ins_nurt, "ins_c": ins_c,
            "rss_nurt": rss_nurt, "rss_c": rss_c,
            **art,
        }

    # ---- Markdown report ----
    print(f"\n# Wyniki benchmarków: Nurt (nurtc) vs C (gcc -O0)\n")
    print(f"Liczba uruchomień na pomiar RSS: {args.runs}; "
          f"powtórzeń kompilacji: {args.compile_reps}\n")

    for name in BENCHMARKS:
        r = results[name]
        inN, inC = r["ins_nurt"], r["ins_c"]
        print(f"## {name}\n")
        print("| Metryka | Nurt (nurtc) | C (gcc -O0) | Nurt / C |")
        print("| --- | ---: | ---: | ---: |")
        print(f"| Instrukcje ogółem | {inN['total']} | {inC['total']} | "
              f"{ratio(inN['total'], inC['total'])} |")
        print(f"| Skoki / rozgałęzienia (jmp, jcc) | {inN['jumps']} | {inC['jumps']} | "
              f"{ratio(inN['jumps'], inC['jumps'])} |")
        print(f"| Operacje pamięciowe (mov, push, pop) | {inN['memops']} | {inC['memops']} | "
              f"{ratio(inN['memops'], inC['memops'])} |")
        print(f"| Średni max RSS [kB] | {r['rss_nurt']:.1f} | {r['rss_c']:.1f} | "
              f"{ratio(r['rss_nurt'], r['rss_c'])} |")
        print(f"| Rozmiar binarium (stripped) [B] | {r['size_nurt']} | {r['size_c']} | "
              f"{ratio(r['size_nurt'], r['size_c'])} |")
        print(f"| Czas kompilacji do asm [ms] | {r['compile_ms_nurt']:.2f} | "
              f"{r['compile_ms_gcc']:.2f} | "
              f"{ratio(r['compile_ms_nurt'], r['compile_ms_gcc'])} |")
        print()

    # ---- aggregate summary ----
    print("## Podsumowanie zbiorcze\n")
    print("| Benchmark | Instr. Nurt | Instr. C | RSS Nurt [kB] | RSS C [kB] | "
          "Rozmiar Nurt [B] | Rozmiar C [B] | Kompilacja Nurt [ms] | Kompilacja C [ms] |")
    print("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for name in BENCHMARKS:
        r = results[name]
        print(f"| {name} | {r['ins_nurt']['total']} | {r['ins_c']['total']} | "
              f"{r['rss_nurt']:.1f} | {r['rss_c']:.1f} | "
              f"{r['size_nurt']} | {r['size_c']} | "
              f"{r['compile_ms_nurt']:.2f} | {r['compile_ms_gcc']:.2f} |")
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
