#!/usr/bin/env python3
# Pristrizna pamat pre beh serie: pri nedostatku pozastavi najvacsi beh (SIGSTOP), po uvolneni ho pusti. Nic nezabija.
import os,signal,subprocess,sys,time
O=sys.argv[1]; LOW,HIGH=2000,5000
def mem():
    d={l.split(':')[0]:int(l.split()[1])//1024 for l in open('/proc/meminfo')}
    return d['MemAvailable']+d['SwapFree']
def sims():
    r=[]
    for p in subprocess.run(['pgrep','-f','ns3-dev-xgregorm-hybrid-optimized'],capture_output=True,text=True).stdout.split():
        try:
            st=open(f'/proc/{p}/stat').read().rsplit(')',1)[1].split()
            if 'hybrid-optimized' in open(f'/proc/{p}/cmdline').read().split('\0')[0]:
                r.append((int(st[21])*4096//2**20,int(p),st[0]))
        except OSError: pass
    return r
def log(m):
    open(f'{O}/guard.log','a').write(time.strftime('%F_%T ')+m+'\n')
stopped=set(); last=0
log('start')
while not os.path.exists(f'{O}/dev5Done'):
    h=mem(); s=sims(); alive={p for _,p,_ in s}; stopped&=alive
    run=[x for x in s if x[1] not in stopped]
    if h<LOW and len(run)>2:
        _,p,_=max(run); os.kill(p,signal.SIGSTOP); stopped.add(p); log(f'STOP {p} headroom {h} MB')
    elif stopped and time.time()-last>60 and (h>HIGH or len(run)<2):
        p=stopped.pop(); os.kill(p,signal.SIGCONT); last=time.time(); log(f'CONT {p} headroom {h} MB')
    time.sleep(20)
for p in stopped: os.kill(p,signal.SIGCONT)
log('end')
