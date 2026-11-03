import sys,re,glob,os,statistics as st
sys.path.insert(0,os.getcwd())
import importlib.util
sp=importlib.util.spec_from_file_location('e','xgregorm_eval.py');e=importlib.util.module_from_spec(sp);sp.loader.exec_module(e)
O=sys.argv[1]; cfgs=sys.argv[2].split(','); seeds=sys.argv[3].split(',')
def bg(path):
    s=open(path,errors='replace').read(); r={}
    for m in ('SPATEM','MAPEM','CPM'):
        for t,k in (('W','w'),('NR','n')):
            x=re.search(rf'^XGREGORM-BG,{m},{t},TX:\d+,BYTES:(\d+),EXP:(\d+),DLV:(\d+)',s,re.M)
            r[f'{m}_{k}']=int(x.group(3))/max(int(x.group(2)),1) if x else None
            r[f'{m}_{k}_B']=int(x.group(1)) if x else 0
    x=re.search(r'XGREGORM-WIFIFRAMES,N:(\d+),BYTES:(\d+)',s); r['wbytes']=int(x.group(2)) if x else None
    x=re.search(r'messages sent at once ([\d.]+) %, queued ([\d.]+) % \(mean wait (\d+) ms\), replaced by a newer one ([\d.]+)',s)
    r['dcc_now'],r['dcc_wait'],r['dcc_repl']=(float(x.group(1)),float(x.group(3)),float(x.group(4))) if x else (None,None,None)
    x=re.search(r'NR CR limit[^:]*: copies dropped ([\d.]+)',s); r['cr_drop']=float(x.group(1)) if x else None
    x=re.search(r'XGREGORM-OFFLOAD,N:(\d+)',s); r['off']=int(x.group(1)) if x else 0
    return r
M=['prr','ul','lat_ms','SPATEM_w','SPATEM_n','CPM_w','CPM_n','load_w','over03_w','load_n','over03_n','load_l','cam_w','cam_n','cam_l','dcc_now','dcc_wait','cr_drop','moves','probes','wall_s']
data={}
for c in cfgs:
    for sd in seeds:
        p=f'{O}/{c}_s{sd}.log'
        if os.path.exists(p):
            m=e.parse(p); m.update(bg(p)); data[(c,sd)]=m
print(f"{'cfg':6s}"+''.join(f'{x[:8]:>9s}' for x in M))
for c in cfgs:
    row=[]
    for x in M:
        v=[data[(c,s)].get(x) for s in seeds if (c,s) in data and data[(c,s)].get(x) is not None]
        row.append(st.mean(v) if v else None)
    print(f'{c:6s}'+''.join((f'{v:9.3f}' if v is not None and abs(v)<100 else (f'{v:9.0f}' if v is not None else f"{'-':>9s}")) for v in row))
print('per seed prr:')
for c in cfgs:
    print(f'{c:6s}',' '.join(f"{data[(c,s)]['prr']:.3f}" if (c,s) in data and data[(c,s)]['prr'] else '-' for s in seeds))
