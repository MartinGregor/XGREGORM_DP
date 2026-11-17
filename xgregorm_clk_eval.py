#!/usr/bin/env python3
# xgregorm: citlivost vyberu podla kvality na casovu synchronizaciu (seria clk, seedy 201-205, 140 aut, bez zon).
# Q = idea1 (presne hodiny GNSS, rozpocet 100 ms), QNODL = bez rozpoctu (pocita sa kazda dorucena CAM),
# QC10 / QC50 = hodiny kazdej stanice posunute o N(0, 10 ms) / N(0, 50 ms). Parovy t-test (n = pocet hotovych seedov). Spustit z ns-3-dev.
import sys, math, statistics as st, re
sys.path.insert(0, '.')
import xgregorm_eval as e

src = {"Q": "xgregorm_logs/idea1/", "QNODL": "xgregorm_logs/clk/", "QC10": "xgregorm_logs/clk/", "QC50": "xgregorm_logs/clk/"}
# seedy, ktore maju vsetky behy hotove (201-203, po doplneni 2026-11-17 az 205)
import os
seeds = tuple(sd for sd in range(201, 206) if all(os.path.isfile(f"{d}{n}_s{sd}.log") and e.parse(f"{d}{n}_s{sd}.log")['done']
                                                  for n, d in src.items()))
R = {n: [e.parse(f"{d}{n}_s{s}.log") for s in seeds] for n, d in src.items()}
for n in R:
    assert all(x['done'] for x in R[n]), n


def qest(n):
    # odhad kvality v autach (QW, QN = ocakavane / prijate / nacas) a podiel prijatych, ktore prisli neskoro (> 100 ms)
    out = []
    for s in seeds:
        t = open(f"{src[n]}{n}_s{s}.log", errors="replace").read()
        w = [tuple(map(int, m)) for m in re.findall(r"QW:(\d+)/(\d+)/(\d+)", t)]
        r = [tuple(map(int, m)) for m in re.findall(r"QN:(\d+)/(\d+)/(\d+)", t)]
        out.append([sum(x[i] for x in w) for i in range(3)] + [sum(x[i] for x in r) for i in range(3)])
    return [sum(o[i] for o in out) for i in range(6)]


cols = ["prr", "cpm_all", "ul", "spat_all", "moves_cm", "probes_cs"]
print("name   " + " ".join(f"{c:>9}" for c in cols) + "   late802.11p  lateNR  est802.11p  estNR")
for n in R:
    m = lambda c: st.mean(x.get(c) for x in R[n] if x.get(c) is not None)
    q = qest(n)
    print(f"{n:6} " + " ".join(f"{m(c):9.4f}" for c in cols)
          + f"   {(q[1] - q[2]) / q[1] * 100:9.3f} % {(q[4] - q[5]) / q[4] * 100:6.2f} %  {q[2] / q[0]:9.4f}  {q[5] / q[3]:.4f}")
N = len(seeds)
T = {3: 4.303, 4: 3.182, 5: 2.776}[N]
print("seedy:", seeds)
for a in ("QNODL", "QC10", "QC50"):
    print(f"\n{a} - Q")
    for c in ("prr", "cpm_all", "ul", "moves_cm"):
        d = [R[a][i][c] - R["Q"][i][c] for i in range(N)]
        md, sd = st.mean(d), st.stdev(d)
        h = T * sd / math.sqrt(N)
        print(f"  {c:9} {md:+.4f} ({md - h:+.4f}, {md + h:+.4f}) p={e.pval(md, sd, N):.3f}   po seedoch "
              + " ".join(f"{x:+.4f}" for x in d))
