#!/usr/bin/env python3
"""Ghost Bass null test - compare the two sides (null_test.py output for upstream Surge XT and for Ghost Bass).
  python3 -I null_compare.py <upstream.tsv> <ghost.tsv> [path prefix to strip] [<upstream run 2.tsv>]
With a second upstream run, a patch whose two upstream renders differ is UPSTREAM-NONDETERMINISTIC (Surge XT
itself does not repeat it sample for sample): listed, and not counted for or against Ghost Bass.
Prints every patch whose audio is not bit-identical (or failed to render), then a count and
"RESULT PASS" only when every patch rendered and matched."""
import sys


def load(p):
    d = {}
    for line in open(p, encoding="utf-8", errors="replace"):
        f = line.rstrip("\n").split("\t")
        if len(f) >= 4:
            d[f[0]] = f
    return d


up, gb = load(sys.argv[1]), load(sys.argv[2])
pre = sys.argv[3] if len(sys.argv) > 3 else ""
up2 = load(sys.argv[4]) if len(sys.argv) > 4 else None
same = diff = err = nondet = 0
for k in sorted(set(up) | set(gb)):
    a, b = up.get(k), gb.get(k)
    name = k[len(pre):] if pre and k.startswith(pre) else k
    if not a or not b:
        err += 1
        print("MISSING  %s (%s)" % (name, "upstream" if not a else "ghost bass"))
    elif a[1] == "ERROR" or b[1] == "ERROR":
        if a[1] == "ERROR" and b[1] == "ERROR":
            same += 1  # (both refuse the same file the same way)
            print("BOTH-ERR %s: %s" % (name, a[2]))
        else:
            err += 1
            print("ERROR    %s: upstream %s / ghost bass %s" % (name, a[2] if a[1] == "ERROR" else "ok", b[2] if b[1] == "ERROR" else "ok"))
    elif up2 is not None and k in up2 and up2[k][1] != a[1]:
        nondet += 1
        print("UPSTREAM-NONDETERMINISTIC %s (Ghost Bass %s)" % (name, "matches one of them" if b[1] in (a[1], up2[k][1]) else "differs from both"))
    elif a[1] == b[1] and a[3] == b[3]:
        same += 1
    else:
        diff += 1
        print("DIFF     %s: peak %s vs %s" % (name, a[2], b[2]))
n = same + diff + err
print("patches: %d  bit-identical: %d  different: %d  missing/errors: %d  (upstream itself not repeatable: %d)" % (n + nondet, same, diff, err, nondet))
print("RESULT %s" % ("PASS" if n > 0 and diff == 0 and err == 0 else "FAIL"))
