#!/usr/bin/env python3
"""fuzz.py — random sessions for MM-41 under the sanitizers.

    make fuzz                       2000 sessions against bin/asan/mm41
    tests/fuzz.py -n 20000 -j 8     more of them, on eight cores
    tests/fuzz.py --seed 7 -n 1     one session, reproducibly
    tests/fuzz.py --keep DIR        where the failing sessions are written
    tests/fuzz.py --files -n 3000   damaged files instead of odd sessions

The functional tests show that the calculator computes the right thing when
it is used properly. This looks for the other kind of fault: a crash, a read
or write outside an allocation, undefined behaviour, a leak or a hang, on
input nobody would type on purpose.

A session is a file of lines run in batch mode. The lines are not noise: the
calculator rejects noise at the lexer and nothing behind it is reached. They
are built from the dispatch table itself (`mm41 --commands`, which gives every
command with a worked example): an example as it stands, an example with its
operands replaced by awkward ones (zero, negative, huge, nan, an empty or
ragged matrix, a string where a number belongs), or commands drawn at random
onto a stack of random operands. Words are defined and called, including by
execution token.

Every failing session is cut down, line by line and then token by token, to
the shortest one that fails the same way, and written to the --keep directory
with what the sanitizer said. Failures are grouped by where they happen, so a
hundred sessions hitting one bug are one line of the report. The exit status
is the number of distinct failures (0 if none).

With --files the session is fixed and the files are what varies. The
calculator reads back what it wrote: the saved stack (a binary file), the
registers, the user's words, its config; and it reads what the user gives it:
tables, programs, scripts, holiday lists. A good copy of each is made by the
calculator itself, one of them is then damaged (cut short, bytes flipped,
numbers replaced by absurd ones, a piece repeated a thousand times, emptied)
and everything is loaded again, with and without the automatic loading at
startup. A file that was half written when the machine lost power should cost
the user that file, not the session.
"""
import argparse
import multiprocessing
import os
import pathlib
import random
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Commands that wait for the keyboard, page, leave the session or need a file that is not there:
# they have their own tests, and here they would only stop a session early.
SKIP = {"exit", "q", "tutor", "manual", "examples", "clrhist", "batch", "load", "loadwords", "use", "unuse", "env",
        "readmat", "readvec", "readcol", "show"}

REALS = ["0", "1", "-1", "2", "0.5", "-0.5", "3", "10", "100", "1e-300", "1e300", "-1e300", "1e-10", "3.7", "-2.5",
         "1e9", "2147483647", "2147483648", "-2147483649", "9007199254740993", "1e19", "-1e19", "0.1", "360", "12", "7",
         "inf", "nan", "-0", "1e308 10 *", "pi", "e", "40", "64", "65", "-64", "1000000", "20260101", "99999999"]
DECIMALS = ["0d", "1d", "0.1d", "-2.5d", "1e30d", "1e-30d", "12345678901234567890123456789012345d", "100d", "0.005d"]
COMPLEX = ["(0,0)", "(1,2)", "(-1,0)", "(0,1)", "(1e300,1e300)", "(3,-4)", "(nan,1)"]
STRINGS = ['""', '"a"', '"hello world"', '"0"', '"-1"', '"%s%s%n"', '"' + "x" * 300 + '"', '"/nonexistent/file"', '"e"',
           '"gompertz"', '"dagum"', '"SI"', '"US"', '"2026-02-30"', '"sin"', '"ω"']
MATRICES = ["[1 1 $ 5]", "[2 2 $ 1 2 3 4]", "[2 2 $ 4 7 2 6]", "[3 1 $ 1 2 3]", "[1 3 $ 1 2 3]", "[3 3 $ 2 0 0 0 3 4 0 4 9]",
            "[2 2 $ 1 2 2 4]", "[2 3 $ 1 2 3 4 5 6]", "[2 2 $ 0 0 0 0]", "[1 5 $ 1 2 3 4 5]", "[5 1 $ 3 1 4 1 5]",
            "[2 2 $ (1,1) (0,2) (3,0) (1,-1)]", "[2 2 $ 1 2 3]", "[0 0 $ ]", "[2 2 $ 1 2 3 4 5]", "[-1 2 $ 1 2]", "[2 2 $ nan 1 1 inf]",
            "[1 1 $ 0]", "[4 2 $ 1 2 2 4 3 5 4 9]", "[6 1 $ 2 3 5 7 11 13]", "[1 2 $ 1e300 1e300]", "[10 10 $ 1]", "[2 2 $ 1e-300 0 0 1e-300]"]
WORDS = [": w1 dup * ;", ": w2 1 + ;", ": w3 drop ;", ": w4 w4 ;", ": w5 2 / ;", ": w6 swap ;", ": w7 dup dup dup ;", ": w8 ;",
         ": w9 0 / ;", ": wa sin ;", ": wb + ;", ": wc 1 0 rcl + ;"]
TOKENS = ["@w1", "@w2", "@w3", "@w4", "@w5", "@w7", "@w8", "@w9", "@wa", "@wb", "@sin", "@exp", "@ln", "@sqrt", "@drop", "@dup",
          "@+", "@nosuch", "w1", "w2", "w4", "w7", "w9"]


def vocabulary(binary):
    """(name, example) for every command in the dispatch table."""
    out = subprocess.run([binary, "--commands"], capture_output=True, text=True).stdout
    cmds = []
    for line in out.split("\n"):
        m = re.match(r"\| `(.+?)` \| `(.*?)` \| .* \| `(.*)` \|$", line)
        if m and m.group(1).replace("\\|", "|") not in SKIP:
            cmds.append((m.group(1).replace("\\|", "|"), m.group(3).replace("\\|", "|")))
    return cmds


def operand(rng):
    kind = rng.random()
    if kind < 0.45:
        return rng.choice(REALS)
    if kind < 0.55:
        return str(rng.randint(-5, 400))
    if kind < 0.62:
        return f"{rng.uniform(-1e3, 1e3):.6g}"
    if kind < 0.70:
        return rng.choice(DECIMALS)
    if kind < 0.77:
        return rng.choice(COMPLEX)
    if kind < 0.85:
        return rng.choice(STRINGS)
    if kind < 0.97:
        return rng.choice(MATRICES)
    r, c = rng.randint(0, 6), rng.randint(0, 6)
    return f"[{r} {c} $ " + " ".join(f"{rng.uniform(-9, 9):.3g}" for _ in range(max(0, r * c + rng.choice([0, 0, 0, -1, 1])))) + "]"


def mutate(example, rng):
    """An example with some of its operands replaced, dropped or doubled."""
    toks = re.findall(r'"[^"]*"|\[[^\]]*\]|\([^)]*\)|\S+', example)
    out = []
    for t in toks:
        literal = re.match(r'^[-+]?[\d.]|^"|^\[|^\(', t)
        roll = rng.random()
        if literal and roll < 0.35:
            out.append(operand(rng))
        elif literal and roll < 0.42:
            continue
        elif roll < 0.05:
            out.extend([t, t])
        else:
            out.append(t)
    return " ".join(out)


def session(seed, cmds, lines=30):
    rng = random.Random(seed)
    out = [rng.choice(WORDS) for _ in range(rng.randint(0, 3))]
    for _ in range(lines):
        roll = rng.random()
        if roll < 0.25:
            out.append(rng.choice(cmds)[1])
        elif roll < 0.60:
            out.append(mutate(rng.choice(cmds)[1], rng))
        elif roll < 0.65:
            out.append(rng.choice(WORDS))
        else:
            toks = [operand(rng) for _ in range(rng.randint(0, 4))]
            for _ in range(rng.randint(1, 5)):
                toks.append(rng.choice(cmds)[0] if rng.random() < 0.85 else rng.choice(TOKENS))
                if rng.random() < 0.3:
                    toks.append(operand(rng))
            out.append(" ".join(toks))
    return [line for line in out if line.strip()]


def run(binary, lines, timeout, prepare=None, flags=("-n", "-q")):
    """Run a session; return (signature, report) if it failed, else None."""
    with tempfile.TemporaryDirectory(prefix="mm41fuzz") as tmp:
        script = pathlib.Path(tmp) / "s.rpn"
        script.write_text("\n".join(lines) + "\n", encoding="utf-8")
        if prepare:
            prepare(binary, pathlib.Path(tmp))
        env = dict(os.environ, HOME=tmp, XDG_CONFIG_HOME=tmp + "/cfg", LC_ALL="C.UTF-8", PAGER="cat", TERM="dumb",
                   ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:exitcode=99:allocator_may_return_null=1:max_allocation_size_mb=64",
                   UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0")
        try:
            p = subprocess.run([binary, *flags, "-f", str(script)], cwd=tmp, env=env, capture_output=True,
                               timeout=timeout, stdin=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            return "hang: no answer in %d s" % timeout, ""
        err = (p.stdout + p.stderr).decode("utf-8", "replace")
    # where it happened: the first frame of the report that is in the calculator's own code
    own = [m for m in re.findall(r"#\d+ 0x[0-9a-f]+ in (\S+) (?:\S*/)?((?:src|modules)/\S+?):(\d+)", err) if "sanitizer" not in m[1]]
    here = f"{own[0][0]} ({pathlib.Path(own[0][1]).name}:{own[0][2]})" if own else "a library"
    m = re.search(r"ERROR: AddressSanitizer: ([\w-]+)", err)
    if m:
        return f"{m.group(1)} in {here}", err
    m = re.search(r"(\S+?):(\d+):\d+: runtime error: (.*)", err)
    if m:
        what = re.sub(r"0x[0-9a-f]+|-?\d[\d.]*(e[-+]?\d+)?", "N", m.group(3))[:70]
        return f"undefined behaviour at {pathlib.Path(m.group(1)).name}:{m.group(2)}: {what}", err
    if "LeakSanitizer" in err:
        # a command abandoned for want of memory does not give back what it had allocated (errors.h):
        # that is by design, and no single allocation here may be larger than 64 MB
        if "out of memory:" in err and p.returncode in (0, 1, 99):
            return None
        return f"leak allocated in {here}", err
    if p.returncode < 0 or p.returncode in (99, 134, 139):
        last = re.sub(r"==\d+==", "", err.strip().split("\n")[-1])[:80] if err.strip() else ""
        return f"killed by signal or abort (status {p.returncode}): {last}", err
    return None


def shrink(binary, lines, signature, timeout):
    """The shortest session that still fails in the same place."""
    if signature.startswith("hang"):
        # Every trial of a hang costs the time limit, so there are few of them: a session hangs
        # from some line on, which bisection finds; then that line is tried on its own.
        quick = lambda ls: (lambda r: r is not None and r[0].startswith("hang"))(run(binary, ls, 3))
        lo, hi = 1, len(lines)                        # the shortest prefix that hangs has between lo and hi lines
        while lo < hi:
            mid = (lo + hi) // 2
            if quick(lines[:mid]):
                hi = mid
            else:
                lo = mid + 1
        return [lines[lo - 1]] if quick([lines[lo - 1]]) else lines[:lo]
    same = lambda ls: (lambda r: r is not None and r[0] == signature)(run(binary, ls, timeout))
    changed = True
    while changed and len(lines) > 1:
        changed = False
        for i in range(len(lines) - 1, -1, -1):
            trial = lines[:i] + lines[i + 1:]
            if trial and same(trial):
                lines, changed = trial, True
    for i in range(len(lines)):                       # then tokens, within each line that is left
        toks = re.findall(r'"[^"]*"|\[[^\]]*\]|\([^)]*\)|\S+', lines[i])
        j = len(toks) - 1
        while j >= 0 and len(toks) > 1:
            trial = toks[:j] + toks[j + 1:]
            if same(lines[:i] + [" ".join(trial)] + lines[i + 1:]):
                toks = trial
                lines[i] = " ".join(toks)
            j -= 1
    return lines


# ---- damaged files ----------------------------------------------------------------------

SETUP = ['1 2.5 (1,2) "str" [2 2 $ 1 2 3 4] [1 2 $ (1,1) (2,2)] 0.1d -7', "savestack", '3 0 sto "x" 1 sto [1 2 $ 5 6] 2 sto 0.5d 3 sto (1,2) 4 sto',
         "saveregs", ": sq dup * ;", ": cube dup dup * * ;", ": hyp dup * swap dup * + sqrt ;", "savewords"]
RELOAD = ["loadstack", "print", "depth", "clst", "loadregs", "0 rcl", "1 rcl", "2 rcl", "3 rcl", "4 rcl", "clst", "loadwords", "listwords",
          "3 sq", "2 cube", "3 4 hyp", "clst", '"prog.prg" run', "clst", '"table.txt" readmat', '"table.txt" readvec', '"table.txt" 1 readcol',
          '[2,2,"table.txt"]', "clst", '"script.rpn" load', '"xx" usehol', "20260101 20260201 workdays", "savestack", "saveregs", "savewords"]
TABLE = "# a table\nage lx dx\n0 100000 500\n1 99500 40\n2 99460 30\n3 99430 25\n"
ABSURD = [b"1e999", b"-1e999", b"nan", b"-1", b"0", b"99999999999999999999", b"4294967296", b"-2147483649", b"1e-999", b"0x7fffffff", b"\x00", b""]


def damage(data, rng):
    kind = rng.randrange(8)
    if kind == 0 or not data:
        return b""
    if kind == 1:
        return data[:rng.randrange(len(data))]
    if kind == 2:
        out = bytearray(data)
        for _ in range(rng.randint(1, 8)):
            out[rng.randrange(len(out))] = rng.randrange(256)
        return bytes(out)
    if kind == 3:
        at = rng.randrange(len(data))
        return data[:at] + bytes(rng.randrange(256) for _ in range(rng.randint(1, 40))) + data[at:]
    if kind == 4:                                     # a number becomes an absurd one
        spots = [m for m in re.finditer(rb"-?\d+(\.\d+)?", data)]
        if spots:
            m = rng.choice(spots)
            return data[:m.start()] + rng.choice(ABSURD) + data[m.end():]
        return data + rng.choice(ABSURD)
    if kind == 5:
        a = rng.randrange(len(data)); b = min(len(data), a + rng.randint(1, 30))
        return data[:a] + data[a:b] * rng.choice([50, 1000, 20000]) + data[b:]
    if kind == 6:                                     # four bytes of a binary file become a huge count
        at = rng.randrange(max(1, len(data) - 4))
        return data[:at] + rng.choice([b"\xff\xff\xff\xff", b"\xff\xff\xff\x7f", b"\x00\x00\x00\x80", b"\x00\x00\x00\x00"]) + data[at + 4:]
    return bytes(rng.randrange(256) for _ in range(rng.randint(1, 600)))


def files_prepare(seed):
    def prepare(binary, tmp):
        rng = random.Random(seed)
        (tmp / "setup.rpn").write_text("\n".join(SETUP) + "\n")
        env = dict(os.environ, HOME=str(tmp), XDG_CONFIG_HOME=str(tmp / "cfg"), LC_ALL="C.UTF-8", ASAN_OPTIONS="detect_leaks=0")
        subprocess.run([binary, "-n", "-q", "-f", "setup.rpn"], cwd=tmp, env=env, capture_output=True, timeout=60)
        cfg = tmp / "cfg" / "mm41"
        (cfg / "holidays").mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / "data" / "prog1.prg", tmp / "prog.prg")
        shutil.copy(ROOT / "data" / "config.txt", cfg / "config.txt")
        (tmp / "table.txt").write_text(TABLE)
        (tmp / "script.rpn").write_text("1 2 +\n: dbl 2 * ;\n5 dbl\n")
        (cfg / "holidays" / "xx.txt").write_text("# test holidays\n2026-01-01 New Year\n2026-01-19\n20260704\n")
        targets = [cfg / "stack_backup.dat", cfg / "registers.txt", cfg / "user_words.txt", cfg / "config.txt", tmp / "prog.prg",
                   tmp / "table.txt", tmp / "script.rpn", cfg / "holidays" / "xx.txt"]
        for victim in rng.sample(targets, rng.choice([1, 1, 1, 2])):
            victim.write_bytes(damage(victim.read_bytes() if victim.exists() else b"", rng))
            (tmp / "damaged").mkdir(exist_ok=True)
            shutil.copy(victim, tmp / "damaged" / victim.name)
    return prepare


def work_files(args):
    binary, seed, timeout, keep = args
    flags = ("-n", "-q") if seed % 3 else ("-q",)      # one run in three also loads words and config at startup
    r = run(binary, RELOAD, timeout, prepare=files_prepare(seed), flags=flags)
    if r is not None and keep:                        # the files are part of the failure: make them again and keep them
        d = pathlib.Path(keep) / f"files{seed}"
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)
        files_prepare(seed)(binary, d)
        (d / "s.rpn").write_text("\n".join(RELOAD) + "\n")
    return (seed, RELOAD, r)


def work(args):
    binary, seed, cmds, timeout, nlines = args
    lines = session(seed, cmds, nlines)
    r = run(binary, lines, timeout)
    return (seed, lines, r)


def main():
    ap = argparse.ArgumentParser(description="random sessions for MM-41 under the sanitizers")
    ap.add_argument("-n", type=int, default=2000, help="sessions to run (default 2000)")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 2, help="processes")
    ap.add_argument("--seed", type=int, default=1, help="first seed; session k uses seed + k")
    ap.add_argument("--lines", type=int, default=30, help="lines in a session")
    ap.add_argument("--timeout", type=int, default=30, help="seconds before a session counts as hung")
    ap.add_argument("--bin", default=str(ROOT / "bin" / "asan" / "mm41"))
    ap.add_argument("--keep", default=str(ROOT / "build" / "fuzz"), help="directory for the failing sessions")
    ap.add_argument("--files", action="store_true", help="damage the files the calculator reads, instead of the session")
    a = ap.parse_args()
    if not os.access(a.bin, os.X_OK):
        sys.exit(f"fuzz: {a.bin} is not there; build it with `make asan`")
    cmds = vocabulary(a.bin)
    if len(cmds) < 100:
        sys.exit("fuzz: could not read the command table from --commands")
    keep = pathlib.Path(a.keep)
    shutil.rmtree(keep, ignore_errors=True)
    found = {}
    if a.files:
        with multiprocessing.Pool(a.j) as pool:
            jobs = [(a.bin, a.seed + k, a.timeout, str(keep)) for k in range(a.n)]
            for done, (seed, lines, r) in enumerate(pool.imap_unordered(work_files, jobs, chunksize=4), 1):
                if r is not None:
                    found.setdefault(r[0], []).append((seed, r[1]))
                if done % 500 == 0:
                    print(f"  {done} runs, {len(found)} distinct failure(s)", flush=True)
        print(f"fuzz: {a.n} runs over damaged files; {len(found)} distinct failure(s)")
        for k, (sig, hits) in enumerate(sorted(found.items(), key=lambda kv: -len(kv[1])), 1):
            seed, report = hits[0]
            keep.mkdir(parents=True, exist_ok=True)
            (keep / f"files{seed}.txt").write_text(f"{sig}\nseed {seed}; {len(hits)} run(s)\n\n{report}", encoding="utf-8")
            print(f"  {k:2d}. {sig}   [{len(hits)} run(s); {keep.name}/files{seed}/ holds the session and the damaged file]")
        for d in keep.glob("files*"):                 # keep one example of each failure, not every run
            if d.is_dir() and not any(d.name == f"files{hits[0][0]}" for hits in found.values()):
                shutil.rmtree(d, ignore_errors=True)
        sys.exit(min(len(found), 100))
    with multiprocessing.Pool(a.j) as pool:
        jobs = [(a.bin, a.seed + k, cmds, a.timeout, a.lines) for k in range(a.n)]
        for done, (seed, lines, r) in enumerate(pool.imap_unordered(work, jobs, chunksize=4), 1):
            if r is not None:
                found.setdefault(r[0], []).append((seed, lines, r[1]))
            if done % 500 == 0:
                print(f"  {done} sessions, {len(found)} distinct failure(s)", flush=True)
    # hangs all look alike from outside: cut a few of them down and tell them apart by what is left
    hangs = [(sig, hits) for sig, hits in found.items() if sig.startswith("hang")]
    for sig, hits in hangs:
        del found[sig]
        for seed, lines, report in hits[:10]:
            small = shrink(a.bin, list(lines), sig, a.timeout)
            found.setdefault("hang at: " + small[-1][:90], []).append((seed, small, report))
    print(f"fuzz: {a.n} sessions of {a.lines} lines over {len(cmds)} commands; {len(found)} distinct failure(s)")
    for k, (sig, hits) in enumerate(sorted(found.items(), key=lambda kv: -len(kv[1])), 1):
        seed, lines, report = hits[0]
        small = lines if sig.startswith("hang at") else shrink(a.bin, list(lines), sig, a.timeout)
        keep.mkdir(parents=True, exist_ok=True)
        (keep / f"fail{k:02d}.rpn").write_text("\n".join(small) + "\n", encoding="utf-8")
        (keep / f"fail{k:02d}.txt").write_text(f"{sig}\nseed {seed}; {len(hits)} session(s)\n\n{report}", encoding="utf-8")
        print(f"  {k:2d}. {sig}   [{len(hits)} session(s); {keep.name}/fail{k:02d}.rpn]")
        for line in small[:6]:
            print(f"        {line[:150]}")
    sys.exit(min(len(found), 100))


if __name__ == "__main__":
    main()
