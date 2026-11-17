#!/usr/bin/env python3
# xgregorm: porovnanie s metodami z literatury.
#  1. mesto (city-center, 140 aut, bez zon, seedy 201-205): KH (Khalid a kol. 2024) a LD (podla vytazenia kanala,
#     Mir a kol. 2015) zo serie lit, FW, FN, MIX a Q z idea1 (rovnaky zaklad a seedy),
#  2. Bologna Acosta (seria acosta, seedy 1-3): Q, MIX, KH, LD.
# Parovy t-test (n = pocet seedov, kde su hotove vsetky behy). Spustit z ns-3-dev.
import sys, os, math, statistics as st
sys.path.insert(0, '.')
import xgregorm_eval as e

T = {2: 12.706, 3: 4.303, 4: 3.182, 5: 2.776, 6: 2.571, 7: 2.447, 8: 2.365, 9: 2.306, 10: 2.262}
COLS = ["prr", "cpm_all", "ul", "spat_all", "moves_cm", "pathsw_cm", "wall_s"]


def ci(d):
    n = len(d)
    md, sd = st.mean(d), st.stdev(d)
    h = T[n] * sd / math.sqrt(n)
    return f"{md:+.4f} ({md - h:+.4f}, {md + h:+.4f}) p={e.pval(md, sd, n):.3f}"


def blok(nazov, src, seeds, pary):
    seeds = tuple(s for s in seeds if all(os.path.isfile(f"{d}{n}_s{s}.log") and e.parse(f"{d}{n}_s{s}.log")['done']
                                          for n, d in src.items()))
    print(f"\n===== {nazov}, seedy {seeds}")
    if len(seeds) < 2:
        print("malo hotovych seedov")
        return
    R = {n: [e.parse(f"{d}{n}_s{s}.log") for s in seeds] for n, d in src.items()}
    print("name   " + " ".join(f"{c:>9}" for c in COLS))
    for n in R:
        def m(c):
            v = [x.get(c) for x in R[n] if x.get(c) is not None]
            return st.mean(v) if v else float('nan')
        print(f"{n:6} " + " ".join(f"{m(c):9.4f}" if abs(m(c)) < 100 else f"{m(c):9.0f}" for c in COLS))
    for c in ("prr", "cpm_all", "ul", "spat_all"):
        print(f"\n{c}")
        for a, b in pary:
            d = [R[a][i][c] - R[b][i][c] for i in range(len(seeds))]
            print(f"  {a:>4} - {b:<4} {ci(d)}   po seedoch " + " ".join(f"{x:+.4f}" for x in d))


I, L, A = "xgregorm_logs/idea1/", "xgregorm_logs/lit/", "xgregorm_logs/acosta/"
blok("mesto bez zon (city-center, 140 aut)",
     {"FW": I, "FN": I, "MIX": I, "Q": I, "KH": L, "LD": L, "DC": L}, range(201, 206),
     [("Q", "KH"), ("Q", "LD"), ("Q", "DC"), ("DC", "MIX"), ("KH", "MIX"), ("LD", "MIX"), ("KH", "FW"), ("KH", "FN"), ("LD", "FW"), ("Q", "MIX")])
blok("Bologna Acosta (iTETRIS, ~550 aut v sieti)",
     {"BMIX": A, "BQ": A, "BKH": A, "BLD": A, "BDC": A}, range(1, 4),
     [("BQ", "BMIX"), ("BQ", "BKH"), ("BQ", "BLD"), ("BQ", "BDC"), ("BKH", "BMIX"), ("BLD", "BMIX")])
