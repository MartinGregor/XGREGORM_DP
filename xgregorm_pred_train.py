#!/usr/bin/env python3
"""xgregorm: ucenie a overenie predikcneho modelu spolahlivosti technologii.

Vstup: CSV z behov s --feat-log (jeden riadok za auto a technologiu raz za 1 s).
Ciel: podiel CAM susedov do 100 m dorucenych nacas pocas nasledujucich H sekund, kazda technologia zvlast.
Model: logisticka regresia (jednoducha, vysvetlitelna, v C++ jeden riadok koeficientov).
Trenovacie a testovacie behy MUSIA byt z roznych seedov.

  python3 xgregorm_pred_train.py --train a.csv b.csv --test c.csv --out xgregorm_pred_model.txt
"""
import argparse
import csv
import numpy as np

NAMES = ["bias", "logit_r_now", "logit_r_map", "map_known", "cbr_now", "cbr_map", "nb", "nb_on_rat", "nb_future",
         "speed", "on_approach", "dist_tls"]


def logit(p):
    p = np.clip(p, 0.02, 0.98)
    return np.log(p / (1 - p))


def features(d):
    """ROVNAKE ako xgstats::predFeatures v xgregorm-stats.h"""
    lr = logit(d["r"])
    known = d["remn"] > 0
    return np.column_stack([np.ones(len(lr)), lr, np.where(known, logit(d["remr"]), lr), known.astype(float), d["cbr"],
                            d["remc"], np.minimum(d["nb"], 40) / 20, np.minimum(d["nbk"], 40) / 20,
                            np.minimum(d["nbf"], 40) / 20, d["v"] / 14, d["appr"], np.minimum(d["dist"], 300) / 300])


def load(paths, h, min_exp):
    """riadky s cielom; vrati slovnik stlpcov pre k = 0, 1"""
    out = {0: [], 1: []}
    for fi, p in enumerate(paths):
        rows = {}
        with open(p) as f:
            for r in csv.DictReader(f):
                try:
                    rows[(int(r["sid"]), int(r["k"]), round(float(r["t"])))] = r
                except (ValueError, TypeError):
                    continue
        for (sid, k, t), r in rows.items():
            q = rows.get((sid, k, t + h))
            if q is None:
                continue
            e = float(q["cumexp"]) - float(r["cumexp"])
            if e < min_exp:
                continue
            y = (float(q["cumok"]) - float(r["cumok"])) / e
            out[k].append([float(r[c]) for c in ("r", "qexp", "cbr", "remr", "remn", "remc", "nb", "nbk", "nbf", "v", "appr", "dist")]
                          + [y, e, fi, t])
    cols = ("r", "qexp", "cbr", "remr", "remn", "remc", "nb", "nbk", "nbf", "v", "appr", "dist", "y", "e", "file", "t")
    res = {}
    for k, v in out.items():
        a = np.array(v, dtype=float) if v else np.zeros((0, len(cols)))
        res[k] = {c: a[:, i] for i, c in enumerate(cols)}
    return res


def fit(x, y, w, l2=1e-3, iters=30):
    """vazena logisticka regresia s podielovym cielom (IRLS)"""
    b = np.zeros(x.shape[1])
    for _ in range(iters):
        p = 1 / (1 + np.exp(-x @ b))
        s = w * np.maximum(p * (1 - p), 1e-4)
        g = x.T @ (w * (p - y)) + l2 * b * w.sum()
        hess = (x * s[:, None]).T @ x + l2 * w.sum() * np.eye(x.shape[1])
        step = np.linalg.solve(hess, g)
        b -= step
        if np.abs(step).max() < 1e-6:
            break
    return b


def wmae(p, y, w):
    return float(np.sum(w * np.abs(p - y)) / max(w.sum(), 1e-9))


def report(k, d, b, label):
    if not len(d["y"]):
        print(f"  {label} k={k}: no data")
        return
    x = features(d)
    pm = 1 / (1 + np.exp(-x @ b)) if b is not None else None
    pnow = d["r"]
    # mapa: priemer sucasneho merania a mapy, ako doterajsia verzia predikcie
    pmap = np.where(d["remn"] > 0, (d["r"] + d["remn"] * d["remr"]) / (1 + d["remn"]), d["r"])
    w = d["e"]
    tech = "802.11p" if k == 0 else "NR"
    line = f"  {label:5s} {tech:8s} n={len(w):7d}  persistence {wmae(pnow, d['y'], w):.4f}  map avg {wmae(pmap, d['y'], w):.4f}"
    if pm is not None:
        line += f"  model {wmae(pm, d['y'], w):.4f}"
    print(line)
    # kde na predikcii zalezi: kvalita sa o viac nez 0.1 zmeni
    m = np.abs(d["y"] - d["r"]) > 0.1
    if m.sum() > 20:
        line = f"        changing (|dq|>0.1) n={int(m.sum()):6d}  persistence {wmae(pnow[m], d['y'][m], w[m]):.4f}  map avg {wmae(pmap[m], d['y'][m], w[m]):.4f}"
        if pm is not None:
            line += f"  model {wmae(pm[m], d['y'][m], w[m]):.4f}"
        print(line)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--train", nargs="+", required=True)
    ap.add_argument("--test", nargs="*", default=[])
    ap.add_argument("--out", default="xgregorm_pred_model.txt")
    ap.add_argument("--horizon", type=int, default=3)
    ap.add_argument("--min-exp", type=float, default=10)
    ap.add_argument("--warmup", type=float, default=10, help="ignore rows before this time [s]")
    a = ap.parse_args()
    if set(a.train) & set(a.test):
        raise SystemExit("train and test must not overlap")
    tr, te = load(a.train, a.horizon, a.min_exp), load(a.test, a.horizon, a.min_exp) if a.test else None
    lines = []
    for k in (0, 1):
        d = {c: v[tr[k]["t"] >= a.warmup] for c, v in tr[k].items()}
        if not len(d["y"]):
            raise SystemExit(f"no training data for technology {k}")
        b = fit(features(d), d["y"], d["e"])
        lines.append(" ".join(f"{v:.6g}" for v in b))
        print(f"technology {k}: " + ", ".join(f"{n}={v:+.3f}" for n, v in zip(NAMES, b)))
        report(k, d, b, "train")
        if te:
            dt = {c: v[te[k]["t"] >= a.warmup] for c, v in te[k].items()}
            report(k, dt, b, "TEST")
    with open(a.out, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("model written to", a.out)


if __name__ == "__main__":
    main()
