#!/usr/bin/env python3
"""xgregorm: vyber predikcneho algoritmu na zaznamoch V2X (plan, cast 9, krok 1 a 2). Bez behu siete.

Ciel: podiel CAM susedov do 100 m dorucenych nacas pocas nasledujucich H s, kazda technologia zvlast.
Rozhodujuca metrika nie je chyba, ale ROZHODNUTIE: ktoru technologiu by algoritmus zvolil a aku spolahlivost by mala.
Suradnice nie su vstupom. Pravda zo simulatora je len ciel.
  ~/xg_venv/bin/python xgregorm_algo_select.py --split seed --glob 'xgregorm_logs/dev6/AQ_s*.csv' ...
"""
import argparse, csv, glob, os, re, sys, warnings
import numpy as np
warnings.filterwarnings("ignore")
BASE = ["r", "qexp", "cbr", "remr", "remn", "remc", "nb", "nbk", "nbf", "v", "appr", "dist"]
MSG = ["spatr", "mapage", "cpmn", "spatn", "cpmobj", "sfresh", "tsw", "isg", "nb30", "nbstop", "nbclose", "nbfar", "nbf30", "dv", "tstop"]


def load(path, h, min_exp, t0):
    with open(path) as f:
        rd = csv.DictReader(f)
        cols = [c for c in BASE + MSG if c in rd.fieldnames]
        rows = {}
        for r in rd:
            try:
                rows[(int(r["sid"]), int(r["k"]), round(float(r["t"])))] = r
            except (ValueError, TypeError):
                pass
    out = []
    for (sid, k, t), r in rows.items():
        q = rows.get((sid, k, t + h))
        p = rows.get((sid, 1 - k, t))
        if q is None or p is None or t < t0:
            continue
        e = float(q["cumexp"]) - float(r["cumexp"])
        if e < min_exp:
            continue
        y = (float(q["cumok"]) - float(r["cumok"])) / e
        # posledna sekunda (vstup pre Kalmana a GRU)
        l = rows.get((sid, k, t - 1))
        e1 = float(r["cumexp"]) - float(l["cumexp"]) if l else 0
        o1 = float(r["cumok"]) - float(l["cumok"]) if l else 0
        out.append([sid, k, t, y, e, e1, o1, float(r["main"]) == k] + [float(r[c]) for c in cols] + [float(p["r"]), float(p["qexp"]), float(p["cbr"])])
    names = ["sid", "k", "t", "y", "e", "e1", "o1", "onmain"] + cols + ["or", "oqexp", "ocbr"]
    a = np.array(out, float) if out else np.zeros((0, len(names)))
    return {n: a[:, i] for i, n in enumerate(names)}


def merge(ds):
    keys = set.intersection(*(set(d) for d in ds))
    r = {}
    for k in keys:
        r[k] = np.concatenate([d[k] for d in ds])
    r["file"] = np.concatenate([np.full(len(d["y"]), i) for i, d in enumerate(ds)])
    return r


NO_MAP = False  # --no-map: bez vstupov z mapy kvality (bez hlaseni cez LTE)


def X(d, msg):
    cols = [c for c in BASE + (MSG if msg else []) if c in d and not (NO_MAP and c in ("remr", "remn", "remc"))] + ["or", "oqexp", "ocbr", "k"]
    return np.column_stack([d[c] for c in cols]), cols


def kalman(d, q, rn):
    """miestna uroven pre kazde auto a technologiu; pozorovanie = dorucenie za posledu sekundu, sum podla poctu CAM"""
    p = np.array(d["r"], float)
    order = np.lexsort((d["t"], d["sid"], d["k"], d["file"]))
    last = None
    x = v = 0
    for i in order:
        key = (d["file"][i], d["sid"][i], d["k"][i])
        if key != last:
            x, v, last = d["r"][i], 0.05, key
        v += q
        if d["e1"][i] > 0:
            s = v + rn / d["e1"][i]
            g = v / s
            x += g * (d["o1"][i] / d["e1"][i] - x)
            v *= 1 - g
        p[i] = x
    return p


def gru_seqs(d, cols, L=10):
    """sekvencie poslednych L s pre auto a technologiu"""
    idx = {}
    for i in range(len(d["y"])):
        idx.setdefault((d["file"][i], d["sid"][i], d["k"][i]), []).append(i)
    Xa = np.column_stack([d[c] for c in cols])
    s = np.zeros((len(d["y"]), L, Xa.shape[1]), np.float32)
    for key, v in idx.items():
        v = sorted(v, key=lambda i: d["t"][i])
        for j, i in enumerate(v):
            w = v[max(0, j - L + 1):j + 1]
            s[i, -len(w):] = Xa[w]
    return s


def fit_gru(tr, te, cols, epochs=8):
    import torch
    torch.set_num_threads(3)
    torch.manual_seed(1)
    Xtr, Xte = gru_seqs(tr, cols), gru_seqs(te, cols)
    mu, sd = Xtr.reshape(-1, Xtr.shape[2]).mean(0), Xtr.reshape(-1, Xtr.shape[2]).std(0) + 1e-6
    Xtr, Xte = (Xtr - mu) / sd, (Xte - mu) / sd
    class M(torch.nn.Module):
        def __init__(s):
            super().__init__()
            s.g = torch.nn.GRU(Xtr.shape[2], 24, batch_first=True)
            s.o = torch.nn.Linear(24, 1)
        def forward(s, x):
            return torch.sigmoid(s.o(s.g(x)[0][:, -1])).squeeze(1)
    m = M()
    opt = torch.optim.Adam(m.parameters(), 3e-3)
    a, y, w = torch.tensor(Xtr), torch.tensor(tr["y"], dtype=torch.float32), torch.tensor(tr["e"], dtype=torch.float32)
    for ep in range(epochs):
        p = torch.randperm(len(y))
        for i in range(0, len(y), 512):
            b = p[i:i + 512]
            opt.zero_grad()
            l = (w[b] * torch.abs(m(a[b]) - y[b])).sum() / w[b].sum()
            l.backward()
            opt.step()
    with torch.no_grad():
        return m(torch.tensor(Xte)).numpy()


def decide(d, p):
    """z predikcii oboch technologii zvol lepsiu; hodnota = skutocna spolahlivost zvolenej (vahy: pocet CAM)"""
    pairs = {}
    for i in range(len(p)):
        pairs.setdefault((d["file"][i], d["sid"][i], d["t"][i]), {})[int(d["k"][i])] = i
    val = opt = wors = wt = hit = n = 0
    for v in pairs.values():
        if len(v) < 2:
            continue
        a, b = v[0], v[1]
        w = min(d["e"][a], d["e"][b])
        ch = a if p[a] >= p[b] else b
        ya, yb = d["y"][a], d["y"][b]
        val += w * d["y"][ch]
        opt += w * max(ya, yb)
        wors += w * min(ya, yb)
        wt += w
        if abs(ya - yb) > 0.05:
            n += 1
            hit += (ya > yb) == (p[a] >= p[b])
    return (val / wt if wt else float("nan"), opt / wt if wt else float("nan"), wors / wt if wt else float("nan"), hit / n if n else float("nan"), n)


def evaluate(tr, te, msgset, gru=True):
    from sklearn.neural_network import MLPRegressor
    from sklearn.preprocessing import StandardScaler
    import lightgbm as lgb
    res = {}
    y, w = te["y"], te["e"]
    res["persistence"] = te["r"]
    pm = np.where(te["remn"] > 0, (te["r"] + te["remn"] * te["remr"]) / (1 + te["remn"]), te["r"])
    res["map avg"] = pm
    # Kalman: sum a proces nastavene na trenovacich datach
    best, bq = 1e9, None
    for q in (0.001, 0.003, 0.01, 0.03):
        for rn in (0.1, 0.2, 0.4):
            e = np.sum(tr["e"] * np.abs(kalman(tr, q, rn) - tr["y"])) / tr["e"].sum()
            if e < best:
                best, bq = e, (q, rn)
    res["kalman"] = kalman(te, *bq)
    for msg in msgset:
        Xt, cols = X(tr, msg)
        Xe, _ = X(te, msg)
        tag = "+msg" if msg else ""
        g = lgb.LGBMRegressor(n_estimators=200, learning_rate=0.05, num_leaves=31, min_child_samples=50, subsample=0.8, subsample_freq=1,
                              colsample_bytree=0.8, objective="l1", n_jobs=3, verbose=-1, random_state=1)
        g.fit(Xt, tr["y"], sample_weight=tr["e"])
        res["gbm" + tag] = g.predict(Xe)
        if msg:
            imp = sorted(zip(g.booster_.feature_importance("gain"), cols), reverse=True)[:8]
            print("    gbm+msg importance:", ", ".join(f"{c}" for _, c in imp))
        sc = StandardScaler().fit(Xt)
        m = MLPRegressor(hidden_layer_sizes=(32, 16), max_iter=60, early_stopping=True, random_state=1)
        m.fit(sc.transform(Xt), tr["y"])
        res["mlp" + tag] = m.predict(sc.transform(Xe))
        # logisticka regresia
        from sklearn.linear_model import LogisticRegression
        Xl = np.vstack([sc.transform(Xt)] * 2)
        yl = np.r_[np.ones(len(Xt)), np.zeros(len(Xt))]
        wl = np.r_[tr["e"] * tr["y"], tr["e"] * (1 - tr["y"])]
        lr = LogisticRegression(max_iter=300, C=10).fit(Xl, yl, sample_weight=wl)
        res["logit" + tag] = lr.predict_proba(sc.transform(Xe))[:, 1]
        if gru:
            res["gru" + tag] = fit_gru(tr, te, [c for c in cols])
    for n, p in res.items():
        p = np.clip(p, 0, 1)
        mae = np.sum(w * np.abs(p - y)) / w.sum()
        ch = np.abs(y - te["r"]) > 0.1
        mae_ch = np.sum(w[ch] * np.abs(p[ch] - y[ch])) / max(w[ch].sum(), 1e-9)
        v, o, wr, hit, nn = decide(te, p)
        print(f"    {n:14s} MAE {mae:.4f}  (changing {mae_ch:.4f})  chosen {v:.4f}  acc {hit:.3f} (n={nn})")
    print(f"    {'bounds':14s} best {o:.4f}  worst {wr:.4f}")
    return res


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--splits", nargs="+", required=True, help="name=trainglob,trainglob:testglob,...")
    ap.add_argument("--horizon", type=int, default=3)
    ap.add_argument("--min-exp", type=float, default=10)
    ap.add_argument("--t0", type=float, default=30)
    ap.add_argument("--no-gru", action="store_true")
    ap.add_argument("--no-map", action="store_true")
    a = ap.parse_args()
    NO_MAP = a.no_map
    for sp in a.splits:
        name, rest = sp.split("=")
        trg, teg = rest.split(":")
        trf = [f for g in trg.split(",") for f in sorted(glob.glob(g))]
        tef = [f for g in teg.split(",") for f in sorted(glob.glob(g))]
        assert not set(trf) & set(tef), "train and test overlap"
        tr, te = merge([load(f, a.horizon, a.min_exp, a.t0) for f in trf]), merge([load(f, a.horizon, a.min_exp, a.t0) for f in tef])
        has = "spatr" in tr and "spatr" in te
        print(f"== {name}: train {len(tr['y'])} rows from {len(trf)} files, test {len(te['y'])} from {len(tef)}, H={a.horizon}", flush=True)
        evaluate(tr, te, [False, True] if has else [False], gru=not a.no_gru)
        sys.stdout.flush()
