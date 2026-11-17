#!/usr/bin/env python3
# xgregorm: overenie zjednoduseni pre velke scenare (seria scal140, 140 aut, bez zon, seedy 201-203).
# QS / MIXS = Q / MIX z idea1 + --nr-max-loss 120 --nr-cache-ttl 1 --nr-fading 0. Otazky:
#  1. ako sa zmeni absolutna uroven (QS - Q, MIXS - MIX),
#  2. ci ostane zachovany rozdiel politik (QS - MIXS oproti Q - MIX), na ktorom stoji vysledok prace.
# Parovy t-test (n = pocet hotovych seedov). Spustit z ns-3-dev.
import sys, os, math, statistics as st
sys.path.insert(0, '.')
import xgregorm_eval as e

src = {"Q": "xgregorm_logs/idea1/", "MIX": "xgregorm_logs/idea1/", "QS": "xgregorm_logs/scal140/", "MIXS": "xgregorm_logs/scal140/"}
seeds = tuple(sd for sd in range(201, 206) if all(os.path.isfile(f"{d}{n}_s{sd}.log") and e.parse(f"{d}{n}_s{sd}.log")['done']
                                                  for n, d in src.items()))
if len(seeds) < 2:
    sys.exit(f"malo hotovych seedov: {seeds}")
R = {n: [e.parse(f"{d}{n}_s{s}.log") for s in seeds] for n, d in src.items()}
N = len(seeds)
T = {2: 12.706, 3: 4.303, 4: 3.182, 5: 2.776}[N]
cols = ["prr", "cpm_all", "ul", "spat_all", "moves_cm", "wall_s"]
print("seedy:", seeds)
print("name   " + " ".join(f"{c:>9}" for c in cols))
for n in R:
    m = lambda c: st.mean(x.get(c) for x in R[n] if x.get(c) is not None)
    print(f"{n:6} " + " ".join(f"{m(c):9.4f}" if abs(m(c)) < 100 else f"{m(c):9.0f}" for c in cols))


def ci(d):
    md, sd = st.mean(d), st.stdev(d)
    h = T * sd / math.sqrt(N)
    return f"{md:+.4f} ({md - h:+.4f}, {md + h:+.4f}) p={e.pval(md, sd, N):.3f}   po seedoch " + " ".join(f"{x:+.4f}" for x in d)


for c in ("prr", "cpm_all", "ul"):
    print(f"\n{c}")
    print("  QS - Q              ", ci([R["QS"][i][c] - R["Q"][i][c] for i in range(N)]))
    print("  MIXS - MIX          ", ci([R["MIXS"][i][c] - R["MIX"][i][c] for i in range(N)]))
    print("  Q - MIX             ", ci([R["Q"][i][c] - R["MIX"][i][c] for i in range(N)]))
    print("  QS - MIXS           ", ci([R["QS"][i][c] - R["MIXS"][i][c] for i in range(N)]))
    print("  (QS-MIXS) - (Q-MIX) ", ci([(R["QS"][i][c] - R["MIXS"][i][c]) - (R["Q"][i][c] - R["MIX"][i][c]) for i in range(N)]))
