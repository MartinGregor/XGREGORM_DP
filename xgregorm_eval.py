#!/usr/bin/env python3
"""xgregorm: tabulka metrik z logov behov hybridu a parove porovnanie politik po seedoch.

  python3 xgregorm_eval.py xgregorm_logs/C6A.log xgregorm_logs/C6E.log
  python3 xgregorm_eval.py --pair E P --dir xgregorm_logs/run --pattern 'S1_{policy}_s{seed}.log'
"""
import argparse
import glob
import math
import os
import re
import statistics as st

T95 = {1: 12.71, 2: 4.30, 3: 3.18, 4: 2.78, 5: 2.57, 6: 2.45, 7: 2.36, 8: 2.31, 9: 2.26, 10: 2.23, 14: 2.14, 19: 2.09}


def num(t):
    try:
        return float(t)
    except (TypeError, ValueError):
        return None


def parse(path):
    """vyber metrik; chybajuce = None. Vsetky hodnoty su z useku po --warmup."""
    s = open(path, errors="replace").read()
    m = {}
    def g(name, rx, f=float):
        r = re.search(rx, s)
        m[name] = f(r.group(1)) if r else None
    g("prr", r"true PRR car -> car[^:]*: ([\d.]+)")
    g("ul", r"uplink CAM -> RSU \(<100 m\) : ([\d.]+)")
    g("pir_p95", r"PIR car->car[^\n]*?p95 (\d+)")
    g("pir_p99", r"PIR car->car[^\n]*?p99 (\d+)")
    r = re.search(r"latency vehicle -> vehicle : 802\.11p ([\d.]+) ms \((\d+)\), NR ([\d.]+) ms \((\d+)\)", s)
    if r:
        n1, n2 = int(r.group(2)), int(r.group(4))
        m["lat_ms"] = (float(r.group(1)) * n1 + float(r.group(3)) * n2) / max(n1 + n2, 1)
    else:
        m["lat_ms"] = None
    for tech, key in (("802.11p", "w"), ("LTE", "l"), (r"NR-V2X", "n")):
        r = re.search(rf"\n\s+{tech}\s+\d+\s+([\d.]+)\s+[\d.]+\s+[\d.]+\s+[\d.]+\s+[\d.]+\s+([\d.]+)\s+([\d.]+)", s)
        m[f"load_{key}"] = float(r.group(1)) if r else None
        m[f"over03_{key}"] = float(r.group(2)) if r else None
    r = re.search(r"CAMs per technology\s*: 802\.11p (\d+), LTE (\d+) \(([\d.]+)/s[^)]*\), NR (\d+); path switches (\d+), probes of 802\.11p (\d+),[^,]*due to load (\d+)(?:, due to quality (\d+), by prediction (\d+))?", s)
    if r:
        m.update(cam_w=int(r.group(1)), cam_l=int(r.group(2)), cam_n=int(r.group(4)), probes=int(r.group(6)),
                 moves=int(r.group(8) or 0) + int(r.group(9) or 0))
    r = re.search(r"XGREGORM-SPAT[^\n]*", s)
    g("spat_w", r"SPATEM[^\n]*?802\.11p[^\d]*([\d.]+)")
    r = re.search(r"average speed\s*: ([\d.]+) m/s", s)
    m["speed"] = float(r.group(1)) * 3.6 if r else None
    r = re.search(r"REMTX:(\d+),REMB:(\d+)", s)
    m["rem_bytes"] = int(r.group(2)) if r else 0
    g("wall_s", r"Wall clock: (\d+) s")
    m["done"] = "XGREGORM-V2V," in s and "Wall clock" in s
    return m


COLS = ["prr", "ul", "lat_ms", "pir_p95", "load_w", "over03_w", "load_n", "over03_n", "load_l", "cam_w", "cam_n", "cam_l", "probes", "moves", "rem_bytes", "speed", "wall_s"]


def table(paths):
    rows = {os.path.basename(p).replace(".log", ""): parse(p) for p in paths}
    w = max(len(k) for k in rows) if rows else 4
    print(" " * w + " " + " ".join(f"{c:>9s}" for c in COLS))
    for k, m in rows.items():
        def f(v):
            return f"{'-':>9s}" if v is None else (f"{v:9.3f}" if isinstance(v, float) and v < 100 else f"{v:9.0f}")
        print(f"{k:{w}s} " + " ".join(f(m.get(c)) for c in COLS) + ("" if m["done"] else "  (UNFINISHED)"))


def paired(dirn, pattern, a, b, metrics=("prr", "ul", "lat_ms", "pir_p95", "over03_w", "over03_n", "load_l", "cam_w", "cam_n", "cam_l", "moves")):
    rx = re.compile(pattern.replace("{policy}", re.escape(a)).replace("{seed}", r"(\d+)").replace(".", r"\.") + "$")
    seeds = sorted(int(rx.match(os.path.basename(p)).group(1)) for p in glob.glob(os.path.join(dirn, "*")) if rx.match(os.path.basename(p)))
    res = {}
    used = []
    for sd in seeds:
        pa = os.path.join(dirn, pattern.format(policy=a, seed=sd))
        pb = os.path.join(dirn, pattern.format(policy=b, seed=sd))
        if not os.path.exists(pb):
            continue
        ma, mb = parse(pa), parse(pb)
        if not (ma["done"] and mb["done"]):
            continue
        used.append(sd)
        for c in metrics:
            if ma.get(c) is not None and mb.get(c) is not None:
                res.setdefault(c, []).append((ma[c], mb[c]))
    print(f"{b} - {a}, seeds {used} (n = {len(used)}); 95 % CI by paired t")
    print(f"{'metric':10s} {a:>10s} {b:>10s} {'diff':>10s} {'CI low':>10s} {'CI high':>10s}  verdict")
    for c, v in res.items():
        d = [y - x for x, y in v]
        n = len(d)
        ma, mb, md = st.mean(x for x, _ in v), st.mean(y for _, y in v), st.mean(d)
        h = T95.get(n - 1, 2.0) * st.stdev(d) / math.sqrt(n) if n > 1 else float("nan")
        verdict = "n/a" if n < 2 else "excludes 0" if md - h > 0 or md + h < 0 else "includes 0"
        print(f"{c:10s} {ma:10.4f} {mb:10.4f} {md:+10.4f} {md - h:+10.4f} {md + h:+10.4f}  {verdict}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("logs", nargs="*")
    ap.add_argument("--pair", nargs=2)
    ap.add_argument("--dir", default="xgregorm_logs")
    ap.add_argument("--pattern", default="{policy}_s{seed}.log")
    x = ap.parse_args()
    if x.pair:
        paired(x.dir, x.pattern, *x.pair)
    else:
        table(x.logs)
