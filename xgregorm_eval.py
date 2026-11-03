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
    # --- chybajuce metriky (plan, cast 2), 2026-11-03 ---
    for tn, key in (("W", "w"), ("NR", "n"), ("LTE", "l")):
        r = re.search(rf"XGREGORM-LATP,TECH:{tn},N:\d+,P50_MS:[\d.naN-]+,P95_MS:([\d.]+)", s)
        m[f"lat95_{key}"] = float(r.group(1)) if r else None
    wu = re.search(r"XGREGORM-SWLOSS,WARMUP:(\d+)", s)
    r = re.search(r"sim-time (\d+) s", s)
    simt = int(r.group(1)) if r else None
    m["simt"] = simt
    tl = parse_losst(s)
    if tl:
        w0 = int(wu.group(1)) if wu else 30
        sec = [v for t, v in tl.items() if t >= w0 and v[2] > 0]
        if sec:
            m["sec_lt09"] = sum(v[3] / v[2] < 0.9 for v in sec) / len(sec)
        m["_timeline"] = tl
    r = re.search(r"XGCSV-HYB: 80211p[^,\n]*,[^,\n]*,\d+,(\d+),(?:[^,\n]*,){8}([\d.]+),", s)
    if r:
        m["veh"], m["age_near"] = int(r.group(1)), float(r.group(2))
    if m.get("veh") and simt and wu:
        mins = (simt - int(wu.group(1))) / 60
        m["moves_cm"] = (m.get("moves") or 0) / m["veh"] / mins  # presuny na auto za minutu
        m["probes_cs"] = (m.get("probes") or 0) / m["veh"] / (mins * 60)  # skusky na auto za sekundu
        # odhad bajtov mapy na downlinku: na kazde hlasenie jedna odpoved s bunkami 3 bodov trasy (113 + 16 B/bunku)
        r = re.search(r"REMTX:(\d+)", s)
        m["rem_dl_est"] = int(r.group(1)) * (20 + 16 * 3 + 93) if r else 0
    for name, t in (("spat", "SPATEM"), ("map", "MAPEM"), ("cpm", "CPM")):
        e = d = 0
        for k in ("W", "NR"):
            r = re.search(rf"^XGREGORM-BG,{t},{k},TX:\d+,BYTES:(\d+),EXP:(\d+),DLV:(\d+)", s, re.M)
            if r:
                e += int(r.group(2))
                d += int(r.group(3))
                m[f"{name}_{k.lower()}_b"] = int(r.group(1))
        m[f"{name}_all"] = d / e if e else None
    r = re.search(r"XGREGORM-WIFIFRAMES,N:(\d+),BYTES:(\d+)", s)
    m["wifi_b"] = int(r.group(2)) if r else None
    m["done"] = "XGREGORM-V2V," in s and "Wall clock" in s
    return m


def parse_losst(s):
    """casova os po 1 s: t -> [ul_exp, ul_rx, v2v_exp, v2v_rx, vzorky, nevie, vek_ms, chyba]"""
    r = re.search(r"XGREGORM-LOSST,(.*)", s)
    if not r:
        return {}
    return {int(t): [int(x) for x in v.split("/")] for t, v in re.findall(r"T(\d+):([\d/]+)", r.group(1))}


def window_prr(path, changes, half=5):
    """PRR auto-auto v okne +-half s okolo zmien kvality; pred a po zmene zvlast"""
    tl = parse_losst(open(path, errors="replace").read())
    out = {}
    for name, rng in (("pre", (-half, 0)), ("post", (0, half))):
        e = d = 0
        for c in changes:
            for t in range(c + rng[0], c + rng[1]):
                if t in tl:
                    e += tl[t][2]
                    d += tl[t][3]
        out[name] = d / e if e else None
    return out


def zone_timing(csv_path, zones, stations):
    """H2. zones = [(rsu, r, t0, t1, 'W'|'N', p)], stations = [(x, y)] podla indexu RSU. Pre kazdy vstup auta do zony:
    bolo na postihnutej technologii? kedy z nej odislo voci vstupu? + podiel sekund v zone na postihnutej technologii."""
    import csv
    import math
    rows = {}
    with open(csv_path) as f:
        for r in csv.DictReader(f):
            if r["k"] == "0":
                rows.setdefault(int(r["sid"]), []).append((float(r["t"]), int(r["main"]), float(r["x"]), float(r["y"])))
    def bad(t, x, y):
        for rsu, rad, t0, t1, k, _ in zones:
            if t0 <= t < t1 and math.hypot(x - stations[rsu][0], y - stations[rsu][1]) <= rad:
                return 0 if k == "W" else 1
        return None
    ins = onbad = entries = before = 0
    leads = []
    for sid, v in rows.items():
        prev = None
        for i, (t, main, x, y) in enumerate(v):
            b = bad(t, x, y)
            if b is not None:
                ins += 1
                onbad += main == b
            if b is not None and prev != b:
                entries += 1
                # posledny odchod z postihnutej technologie v okne -15..+15 s okolo vstupu; zaporne = pred vstupom
                sw = [v[j][0] - t for j in range(max(i - 15, 1), min(i + 16, len(v))) if v[j - 1][1] == b and v[j][1] != b]
                if sw:
                    leads.append(min(sw, key=abs))
                    before += leads[-1] <= 0
            prev = b
    leads.sort()
    return {"entries": entries, "in_zone_s": ins, "share_on_bad": onbad / ins if ins else None,
            "switches": len(leads), "before_entry": before, "lead_median_s": leads[len(leads) // 2] if leads else None}


COLS = ["prr", "ul", "lat_ms", "lat95_w", "lat95_n", "lat95_l", "sec_lt09", "age_near", "spat_all", "cpm_all", "moves_cm", "probes_cs", "pir_p95", "load_w", "over03_w", "load_n", "over03_n", "load_l", "cam_w", "cam_n", "cam_l", "probes", "moves", "rem_bytes", "speed", "wall_s"]


def pval(md, sd, n):
    """dvojstranna p-hodnota parovaneho t-testu (presne cez incomplete beta, bez scipy)"""
    if sd == 0:
        return 0.0 if md != 0 else 1.0
    t, df = abs(md) / (sd / math.sqrt(n)), n - 1
    x = df / (df + t * t)
    # numericka integracia hustoty beta(df/2, 1/2) na [0, x]
    a, b = df / 2, 0.5
    bf = math.exp(math.lgamma(a) + math.lgamma(b) - math.lgamma(a + b))
    N = 2000
    # substitucia u = sqrt(1 - z) odstrani singularitu v z = 1
    tot = 0.0
    ulo = math.sqrt(1 - x)
    for i in range(N):
        u = ulo + (1 - ulo) * (i + 0.5) / N
        tot += (1 - u * u) ** (a - 1) * 2
    return min(1.0, tot * (1 - ulo) / N / bf)


def table(paths):
    rows = {os.path.basename(p).replace(".log", ""): parse(p) for p in paths}
    w = max(len(k) for k in rows) if rows else 4
    print(" " * w + " " + " ".join(f"{c:>9s}" for c in COLS))
    for k, m in rows.items():
        def f(v):
            return f"{'-':>9s}" if v is None else (f"{v:9.3f}" if isinstance(v, float) and v < 100 else f"{v:9.0f}")
        print(f"{k:{w}s} " + " ".join(f(m.get(c)) for c in COLS) + ("" if m["done"] else "  (UNFINISHED)"))


def paired(dirn, pattern, a, b, metrics=("prr", "ul", "cpm_all", "spat_all", "lat_ms", "lat95_w", "lat95_n", "pir_p95", "sec_lt09", "age_near", "over03_w", "over03_n", "load_l", "cam_w", "cam_n", "cam_l", "moves_cm", "probes_cs", "rem_bytes", "rem_dl_est")):
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
    rows = []
    for c, v in res.items():
        d = [y - x for x, y in v]
        n = len(d)
        ma, mb, md = st.mean(x for x, _ in v), st.mean(y for _, y in v), st.mean(d)
        h = T95.get(n - 1, 2.0) * st.stdev(d) / math.sqrt(n) if n > 1 else float("nan")
        verdict = "n/a" if n < 2 else "excludes 0" if md - h > 0 or md + h < 0 else "includes 0"
        pv = pval(md, st.stdev(d), n) if n > 1 else 1.0
        rows.append([c, ma, mb, md, h, verdict, pv])
    # Holmova korekcia pre vedlajsie metriky (hlavna prr sa nekoriguje)
    sec = sorted((r for r in rows if r[0] != "prr"), key=lambda r: r[6])
    run = 0
    for i, r in enumerate(sec):
        run = max(run, min(1, r[6] * (len(sec) - i)))
        r.append(run)
    for r in rows:
        c, ma, mb, md, h, verdict, pv = r[:7]
        holm = r[7] if len(r) > 7 else pv
        print(f"{c:10s} {ma:10.4f} {mb:10.4f} {md:+10.4f} {md - h:+10.4f} {md + h:+10.4f}  {verdict}  p={pv:.3f} Holm p={holm:.3f}")


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
