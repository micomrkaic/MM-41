#!/usr/bin/env python3
"""Out-of-memory test: fail allocations one at a time and see what happens.

For every command, a short session is run again and again: some things on
the stack and in the registers, the command's own example, then a few lines
that use what was there before. On the k-th run the k-th allocation made
while the example runs is failed (tests/nomem_shim.c, loaded with
LD_PRELOAD), for k = 1, 2, ... until the example has no more allocations.

Whatever is failed, the calculator has to stay standing: an error message,
the stack as it was before the command, and no invalid memory access when
the stack and the registers are used afterwards. A crash, or a report from
the address sanitizer, is a failure. Memory the abandoned command had
allocated is not expected back, so leaks are not counted here.

    make asan && python3 tests/nomem.py              every command
    python3 tests/nomem.py svd kron rcl               these commands
    python3 tests/nomem.py --scope all                also the interpreter's own allocations
    python3 tests/nomem.py --run 1000000              every allocation from the k-th on fails

Exit status 1 if anything failed.
"""
import argparse
import multiprocessing
import os
import pathlib
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import fuzz  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent

# what is on the stack and in the registers when the command runs, and what is done with it afterwards
SETUP = '[1 2;3 4] 1 sto "abc" 2 sto 1.5d 3 sto (1,2) 4 sto [(1,2) (3,4)] 5 sto [5 6;7 8] "s" 2.5 (0,1) [1 2 3]'
AFTER = ["depth drop", "1 rcl 2 rcl 3 rcl 4 rcl 5 rcl", "depth drop", "dup + drop", "1 rcl dup * 1 sto", "undo", "clst"]

BAD = re.compile(r"AddressSanitizer|runtime error:|LeakSanitizer|Segmentation|double free|corrupted|munmap_chunk|malloc\(\)|free\(\)")


def run(binary, shim, lines, at, scope, length, timeout=60):
    with tempfile.TemporaryDirectory(prefix="mm41nomem") as tmp:
        (pathlib.Path(tmp) / "s.rpn").write_text("\n".join(lines) + "\n")
        env = dict(os.environ, HOME=tmp, XDG_CONFIG_HOME=tmp + "/cfg", LC_ALL="C.UTF-8", PAGER="cat", TERM="dumb",
                   LD_PRELOAD=shim, MM41_NOMEM_AT=str(at), MM41_NOMEM_RUN=str(length), MM41_NOMEM_SCOPE=scope,
                   MALLOC_CHECK_="3", MALLOC_PERTURB_="165",
                   ASAN_OPTIONS="verify_asan_link_order=0:detect_leaks=0:abort_on_error=0:exitcode=99",
                   UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0")
        try:
            p = subprocess.run([binary, "-n", "-q", "-f", "s.rpn"], cwd=tmp, env=env, capture_output=True, timeout=timeout,
                               stdin=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            return "hang", "", 0, True
        err = p.stderr.decode("utf-8", "replace")
    m = re.search(r"NOMEM: counted (\d+)", err)
    counted = int(m.group(1)) if m else 0
    fired = "NOMEM: failed" in err
    if p.returncode < 0:
        return f"killed by signal {-p.returncode}", err, counted, fired
    if p.returncode not in (0, 1) or BAD.search(err):
        m = BAD.search(err)
        return (m.group(0) if m else f"exit status {p.returncode}"), err, counted, fired
    return None, err, counted, fired


def work(args):
    binary, shim, name, example, scope, length, base, most = args
    lines = [SETUP, example] + AFTER
    found, tried, at, step = [], 0, base + 1, 1.0
    while tried < most:
        bad, err, counted, fired = run(binary, shim, lines, at, scope, length)
        if not fired and not bad:
            break
        tried += 1
        if bad:
            found.append((at, bad, err))
            if len(found) >= 3:
                break
        # a command that calls back into the interpreter (a simulation, an integral) allocates thousands
        # of times: the first sixty one by one, then at widening intervals
        if tried > 60:
            step *= 1.15
        at += int(step)
    return name, example, tried, found


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("commands", nargs="*", help="only these commands (default: all)")
    ap.add_argument("--binary", default=str(ROOT / "bin/asan/mm41"))
    ap.add_argument("--scope", choices=("cmd", "all"), default="cmd")
    ap.add_argument("--run", type=int, default=1, help="how many allocations in a row fail (default 1)")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--most", type=int, default=120, help="allocations tried per command at most (default 120)")
    args = ap.parse_args()

    if not pathlib.Path(args.binary).exists():
        sys.exit(f"{args.binary}: not built (make asan)")
    build = tempfile.mkdtemp(prefix="mm41shim")
    shim = build + "/nomem_shim.so"
    subprocess.run(["cc", "-O1", "-g", "-fPIC", "-shared", "-o", shim, str(ROOT / "tests/nomem_shim.c"), "-ldl"], check=True)

    cmds = [(n, e) for n, e in fuzz.vocabulary(args.binary) if e and (not args.commands or n in args.commands)]
    _, _, base, _ = run(args.binary, shim, [SETUP], 0, args.scope, 1)       # allocations made before the example
    jobs = [(args.binary, shim, n, e, args.scope, args.run, base, args.most) for n, e in cmds]
    total, failures = 0, []
    with multiprocessing.Pool(args.j) as pool:
        for name, example, tried, found in pool.imap_unordered(work, jobs, chunksize=2):
            total += tried
            if found:
                failures.append((name, example, found))
                print(f"  {name}: {found[0][1]}", flush=True)
    print(f"{len(cmds)} commands, {total} failed allocations tried; {len(failures)} command(s) did not survive")
    for name, example, found in sorted(failures):
        at, bad, err = found[0]
        print(f"\n{name}: {bad}  (allocation {at}; example: {example})")
        keep = [l for l in err.split("\n") if re.search(r"NOMEM|ERROR|SUMMARY|^\s+#[0-9] .*mm-41|runtime error", l)]
        print("\n".join("    " + l.strip()[:170] for l in keep[:14]))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
