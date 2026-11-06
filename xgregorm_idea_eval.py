#!/usr/bin/env python3
# xgregorm: overenie napadov 1-6 (idea1 + idea26, seedy 201-205), parovy t-test n = 5, Holm cez varianty napadu (PRR)
# a cez vedlajsie metriky. Kriterium (plan, cast 9): PRR >= +0.01, 95 % interval bez nuly, nehorsie CPM a LTE. Spustit z ns-3-dev.
import sys, math, statistics as st, glob, re, json
sys.path.insert(0, '.')
import xgregorm_eval as e
seeds = range(201, 206)
D = {}
for n in json.load(open('xgregorm_logs/idea1.json'))['configs']: D[n] = 'xgregorm_logs/idea1/'
for n in json.load(open('xgregorm_logs/idea26.json'))['configs']: D[n] = 'xgregorm_logs/idea26/'
R = {n: [e.parse(f'{D[n]}{n}_s{s}.log') for s in seeds] for n in D}
for n in R: assert all(x['done'] for x in R[n]), n
cols = ["prr", "cpm_all", "ul", "spat_all", "lat95_l", "age_near", "sec_lt09", "load_l", "cam_l", "moves_cm", "probes_cs", "wall_s"]
print('name  ' + ' '.join(f'{c[:8]:>8}' for c in cols))
for n in sorted(R):
    def m(c):
        v = [x.get(c) for x in R[n] if x.get(c) is not None]; return st.mean(v) if v else float('nan')
    print(f'{n:5} ' + ' '.join(f'{m(c):8.3f}' if abs(m(c)) < 100 else f'{m(c):8.0f}' for c in cols))
print('viol', sum(int(a) + int(b) for d in ('idea1', 'idea26') for f in glob.glob(f'xgregorm_logs/{d}/*_s*.log')
                  for a, b in re.findall(r'violations < 100 ms: (\d+), > 1000 ms: (\d+)', open(f, errors='replace').read())))
SEC = ("cpm_all", "ul", "spat_all", "lat95_l", "age_near", "sec_lt09", "load_l", "cam_l")
T = 2.776
N = 5


def diff(a, b, c):
    x = [R[a][i].get(c) for i in range(N)]; y = [R[b][i].get(c) for i in range(N)]
    if None in x or None in y: return None
    d = [p - q for p, q in zip(x, y)]; md = st.mean(d); sd = st.stdev(d); h = T * sd / math.sqrt(N)
    return [c, md, md - h, md + h, e.pval(md, sd, N)]


def holm(rows):
    rows = sorted(rows, key=lambda r: r[4]); run = 0
    for i, r in enumerate(rows): run = max(run, min(1, r[4] * (len(rows) - i))); r.append(run)


def pair(a, b, prr_holm=None):
    rows = [r for r in (diff(a, b, c) for c in ("prr",) + SEC + ("moves_cm",)) if r]
    holm([r for r in rows if r[0] in SEC])
    print(f'\n{a} - {b}')
    for r in rows:
        extra = f' Holm={r[5]:.3f}' if len(r) > 5 else ''
        if r[0] == 'prr' and prr_holm is not None: extra = f' Holm(varianty)={prr_holm:.3f}'
        print(f'  {r[0]:9} {r[1]:+.4f} ({r[2]:+.4f},{r[3]:+.4f}) p={r[4]:.3f}{extra}')


# Holm cez varianty jedneho napadu (PRR)
def idea(variants, ref):
    pr = [diff(v, ref, 'prr') for v in variants]
    for r, v in zip(pr, variants): r.append(v)
    srt = sorted(pr, key=lambda r: r[4]); run = 0; adj = {}
    for i, r in enumerate(srt): run = max(run, min(1, r[4] * (len(srt) - i))); adj[r[5]] = run
    for v in variants: pair(v, ref, adj[v])


print('\n===== referencie bez reaktivnej logiky a Q (bez zon) =====')
for a, b in (("Q", "FW"), ("Q", "MIX"), ("FN", "FW"), ("MIX", "FW"), ("ALL", "FW"), ("ALL", "Q")): pair(a, b)
print('\n===== napad 1: DCC offload, ref FW =====')
idea(["I1a", "I1b", "I1c"], "FW")
for v in ("I1a", "I1b", "I1c"): pair(v, "Q")
print('\n===== napad 2: vybava, ref EF1 / EF2 =====')
idea(["I2a"], "EF1"); idea(["I2b"], "EF2")
print('\n===== napad 3: RSU podla trasy, ref RF1 / RF2 =====')
idea(["I3a"], "RF1"); idea(["I3b"], "RF2")
print('\n===== napad 4: duplikat blizko, ref FW =====')
idea(["I4a", "I4b", "I4c"], "FW")
for v in ("I4a", "I4b", "I4c"): pair(v, "Q")
print('\n===== napad 5: geo RAT, ref FW =====')
idea(["I5a", "I5b", "I5c"], "FW")
for v in ("I5a", "I5b", "I5c"): pair(v, "Q")
print('\n===== napad 6: CAM cez NR, CPM cez 802.11p =====')
for b in ("FW", "FN", "MIX", "Q"): pair("I6R", b)
pair("DQ", "DMIX"); pair("DI6R", "DMIX"); pair("DI6R", "DQ")
