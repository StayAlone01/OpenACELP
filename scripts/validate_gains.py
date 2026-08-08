#!/usr/bin/env python3
# ------------------------------------------------------------------
# validate_gains.py - Validate the gain quantization (cl. 4.2.2.6)
#
# Usage:
#   # build the encoder with the gain debug hook, run it, then:
#   make clean && make CFLAGS="-Wall -Wextra -O2 -std=c99 -DGAIN_DEBUG"
#   ./openacelp speech.raw > gains.txt 2>&1
#   python3 scripts/validate_gains.py gains.txt
#   make clean && make
#
# Parses the GAIN_DEBUG lines (frame/sf/idx/gp_q/gc_q/gp/gc/e_p/e_c/err)
# and reports:
#   * gain tracking error (log2 + dB) for gp and gc
#   * codebook usage (distinct indices out of 64)
#   * bounds (gp_q <= 1.2, gc_q range, no NaN)
#   * quiet-frame behaviour (gains near zero)
#   * gc_q over-shoot (gc_q vs unquantized gc ratio)
#
# Exit code: 0 = OK, 1 = problems found (NaN, gp_q out of bounds).
# ------------------------------------------------------------------

import math
import re
import sys
from collections import Counter

LINE_RE = re.compile(r"^frame=")


def parse_line(line):
    d = {}
    for tok in line.split():
        if "=" not in tok:
            continue
        key, val = tok.split("=", 1)
        try:
            d[key] = float(val)
        except ValueError:
            # err=(a,b) style tokens
            m = re.match(r"\((-?[\d.]+),(-?[\d.]+)\)", val)
            if m:
                d[key] = (float(m.group(1)), float(m.group(2)))
    return d


def log2_err(a, b):
    """|log2(a/b)| in bits, if both are meaningful (>1e-3)."""
    if a > 1e-3 and b > 1e-3:
        return abs(math.log2(a) - math.log2(b))
    return None


def main():
    if len(sys.argv) < 2:
        print("usage: %s gains.txt" % sys.argv[0], file=sys.stderr)
        sys.exit(1)

    rows = []
    with open(sys.argv[1]) as f:
        for line in f:
            if "gp_q" not in line:
                continue
            d = parse_line(line)
            if "gp_q" in d and "gp" in d:
                rows.append(d)

    if not rows:
        print(
            "no GAIN_DEBUG lines found (build with -DGAIN_DEBUG and run the "
            "encoder on a raw file)",
            file=sys.stderr,
        )
        sys.exit(1)

    n = len(rows)
    problems = []

    # ---- gain tracking error ----
    gp_err = [e for r in rows for e in [log2_err(r["gp"], r["gp_q"])] if e]
    gc_err = [e for r in rows for e in [log2_err(r["gc"], r["gc_q"])] if e]

    def stats(name, errs):
        if errs:
            mean = sum(errs) / len(errs)
            print(
                "%-8s tracking error: mean %.3f bit (%.2f dB), "
                "p95 %.3f, max %.3f  (n=%d)"
                % (
                    name,
                    mean,
                    6.0206 * mean,
                    sorted(errs)[int(0.95 * len(errs)) - 1],
                    max(errs),
                    len(errs),
                )
            )
        else:
            print("%-8s tracking error: n/a" % name)

    print("=== gain validation: %d sub-frames ===" % n)
    stats("gp", gp_err)
    stats("gc", gc_err)

    # ---- codebook usage ----
    c = Counter(int(r["idx"]) for r in rows)
    unused = [k for k in range(64) if k not in c]
    print(
        "codebook usage: %d / 64 entries used" % len(c),
        "(unused: %s)" % (",".join(map(str, unused)) if unused else "none"),
    )
    print("  top entries:", ", ".join("%d(%d)" % kv for kv in c.most_common(5)))

    # ---- bounds / NaN ----
    gpq = [r["gp_q"] for r in rows]
    gcq = [r["gc_q"] for r in rows]
    nan = sum(1 for x in gpq + gcq if math.isnan(x) or math.isinf(x))
    over = sum(1 for x in gpq if x > 1.2 + 1e-6)
    print("gp_q range: %.4f .. %.4f (limit 1.2)" % (min(gpq), max(gpq)))
    print("gc_q range: %.4f .. %.4f" % (min(gcq), max(gcq)))
    print("NaN/inf: %d,  gp_q>1.2: %d" % (nan, over))
    if nan:
        problems.append("NaN/inf in gains")
    if over:
        problems.append("gp_q exceeds 1.2")

    # ---- quiet frames ----
    quiet = [r for r in rows if r["gp"] < 0.01 and r["gc"] < 50.0]
    if quiet:
        qgpq = [r["gp_q"] for r in quiet]
        qgcq = [r["gc_q"] for r in quiet]
        print(
            "quiet frames (%d): gp_q mean %.4f, gc_q mean %.2f, "
            "max gc_q %.2f"
            % (len(quiet), sum(qgpq) / len(qgpq), sum(qgcq) / len(qgcq), max(qgcq))
        )

    # ---- gc over-shoot ----
    ratios = [r["gc_q"] / max(r["gc"], 1e-9) for r in rows if r["gc"] > 1e-3]
    if ratios:
        big = sum(1 for x in ratios if x > 3.0)
        print(
            "gc_q/gc over-shoot: mean %.2f, max %.1f, ratio>3: %d/%d"
            % (sum(ratios) / len(ratios), max(ratios), big, len(ratios))
        )
        if big > 0.02 * len(ratios):
            print(
                "  NOTE: gc over-shoot on >2%% of frames - re-check the "
                "codebook coverage"
            )

    print()
    if problems:
        print("RESULT: PROBLEMS ->", "; ".join(problems))
        sys.exit(1)
    print("RESULT: OK")


if __name__ == "__main__":
    main()
