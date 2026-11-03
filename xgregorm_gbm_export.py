#!/usr/bin/env python3
"""xgregorm: nauci GBM zo zaznamov --feat-log a ulozi ho pre C++ (--pred-gbm). Seedy ucenia sa nesmu zhodovat s testom.

  ~/xg_venv/bin/python xgregorm_gbm_export.py --train a.csv b.csv --out model.txt [--check c.csv c_pred.txt]
Bez mapy kvality: auto nepotrebuje hlasenia cez LTE. Poradie priznakov kontroluje aj C++ pri nacitani.
"""
import argparse
import numpy as np
import lightgbm as lgb
import xgregorm_algo_select as A


def flat(tree):
    out = []
    def rec(n):
        i = len(out)
        out.append(None)
        if "leaf_value" in n:
            out[i] = (-1, 0.0, -1, -1, n["leaf_value"])
        else:
            assert n["decision_type"] == "<=" and n["missing_type"] == "None", n["missing_type"]
            l = rec(n["left_child"])
            r = rec(n["right_child"])
            out[i] = (n["split_feature"], n["threshold"], l, r, 0.0)
        return i
    rec(tree)
    return out


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--train", nargs="+", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--horizon", type=int, default=3)
    ap.add_argument("--check", nargs=2, help="csv, kam zapisat vstupy a predikciu Pythonu na kontrolu C++")
    x = ap.parse_args()
    A.NO_MAP = True
    tr = A.merge([A.load(f, x.horizon, 10, 30) for f in x.train])
    Xt, cols = A.X(tr, True)
    g = lgb.LGBMRegressor(n_estimators=200, learning_rate=0.05, num_leaves=31, min_child_samples=50, subsample=0.8, subsample_freq=1,
                          colsample_bytree=0.8, objective="l1", n_jobs=3, verbose=-1, random_state=1)
    g.fit(Xt, tr["y"], sample_weight=tr["e"])
    d = g.booster_.dump_model()
    with open(x.out, "w") as f:
        f.write(f"GBM {len(cols)} {len(d['tree_info'])}\n{','.join(cols)}\n")
        for t in d["tree_info"]:
            nodes = flat(t["tree_structure"])
            f.write(f"T {len(nodes)}\n")
            for n in nodes:
                f.write(f"{n[0]} {n[1]:.17g} {n[2]} {n[3]} {n[4]:.17g}\n")
    print(f"{x.out}: {len(tr['y'])} rows, {len(d['tree_info'])} trees, {sum(t['num_leaves'] for t in d['tree_info'])} leaves")
    if x.check:
        te = A.load(x.check[0], x.horizon, 10, 30)
        Xe, _ = A.X(te, True)
        p = g.predict(Xe)
        np.savetxt(x.check[1], np.column_stack([Xe, p]), fmt="%.17g")
