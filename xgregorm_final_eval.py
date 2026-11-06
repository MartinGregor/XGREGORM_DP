#!/usr/bin/env python3
# xgregorm: zaverecne porovnanie final1 + final2 (seedy 1-10), parovy t-test, Holm pre vedlajsie metriky. Spustit z ns-3-dev.
import sys,math,statistics as st,glob,re,json
sys.path.insert(0,'.')
import xgregorm_eval as e
seeds=range(1,11)
D={}
for n in json.load(open('xgregorm_logs/final1.json'))['configs']: D[n]='xgregorm_logs/final1/'
for n in json.load(open('xgregorm_logs/final2.json'))['configs']: D[n]='xgregorm_logs/final2/'
R={n:[e.parse(f'{D[n]}{n}_s{s}.log') for s in seeds] for n in D}
for n in R: assert all(x['done'] for x in R[n]),n
cols=["prr","cpm_all","ul","spat_all","lat95_l","age_near","sec_lt09","load_l","cam_l","moves_cm","probes_cs","wall_s"]
print('name '+' '.join(f'{c[:8]:>8}' for c in cols))
for n in sorted(R):
    def m(c):
        v=[x.get(c) for x in R[n] if x.get(c) is not None];return st.mean(v) if v else float('nan')
    print(f'{n:5}'+' '.join(f'{m(c):8.3f}' if abs(m(c))<100 else f'{m(c):8.0f}' for c in cols))
print('viol',sum(int(a)+int(b) for d in ('final1','final2') for f in glob.glob(f'xgregorm_logs/{d}/*_s*.log') for a,b in re.findall(r'violations < 100 ms: (\d+), > 1000 ms: (\d+)',open(f,errors='replace').read())))
SEC=("cpm_all","ul","spat_all","lat95_l","age_near","sec_lt09","load_l","cam_l")
t=2.262
def pair(a,b):
    rows=[]
    for c in ("prr",)+SEC+("moves_cm",):
        x=[R[a][i].get(c) for i in range(10)];y=[R[b][i].get(c) for i in range(10)]
        if None in x or None in y: continue
        d=[p-q for p,q in zip(x,y)];md=st.mean(d);sd=st.stdev(d);h=t*sd/math.sqrt(10)
        rows.append([c,md,md-h,md+h,e.pval(md,sd,10)])
    sec=sorted((r for r in rows if r[0] in SEC),key=lambda r:r[4]);run=0
    for i,r in enumerate(sec): run=max(run,min(1,r[4]*(len(sec)-i)));r.append(run)
    print(f'\n{a} - {b}')
    for r in rows: print(f'  {r[0]:9} {r[1]:+.4f} ({r[2]:+.4f},{r[3]:+.4f}) p={r[4]:.3f}'+(f' Holm={r[5]:.3f}' if len(r)>5 else ''))
for k in 'ACDT':
    pair(k+'Q',k+'MIX'); pair(k+'PM',k+'Q'); pair(k+'PM',k+'MIX')
    if k!='T':
        pair(k+'O0',k+'Q'); pair(k+'O3',k+'O0'); pair(k+'O3',k+'Q')
for k in 'DT': pair(k+'GS',k+'Q'); pair(k+'GS',k+'MIX')
