#!/usr/bin/env python3
"""xgregorm: spustac serie behov hybridu podla JSON spec. Po preruseni preskoci hotove behy.

  spec = {"out": "dev6", "jobs": 6, "seeds": [101,102,103], "base": "...", "configs": {"Q": "--clean 1 ...", ...}}
  V argumentoch {seed} = seed, {out} = priecinok vystupu, {name} = meno behu. Poradie: po seedoch, aby sa dali
  parove porovnania vyhodnocovat priebezne. Pamat: spustat v cgroup (xgregorm_logs/xgregorm_start.sh).
"""
import json, os, shutil, subprocess, sys, time

spec = json.load(open(sys.argv[1]))
L = os.path.dirname(os.path.abspath(__file__))
O = os.path.join(L, spec["out"])
os.makedirs(O, exist_ok=True)
os.chdir(os.path.join(L, ".."))
os.environ.setdefault("SUMO_HOME", "/usr/local/share/sumo")
JOBS = int(os.environ.get("JOBS", spec.get("jobs", 6)))


def ok(n):
    try:
        s = open(f"{O}/{n}.log", errors="replace").read()
    except OSError:
        return False
    return "\nXGREGORM-V2V," in s and "Wall clock" in s


def clean(n):
    shutil.rmtree(f"src/automotive/examples/xgregorm_{n}", ignore_errors=True)
    shutil.rmtree(f"xgregorm_tmp_{n}", ignore_errors=True)


todo = []
for sd in spec["seeds"]:
    for c, args in spec["configs"].items():
        n = f"{c}_s{sd}"
        if not ok(n):
            todo.append((n, args.replace("{seed}", str(sd)).replace("{out}", O).replace("{name}", n) + f" --seed {sd}"))
print(len(todo), "runs to do", flush=True)
run, memt = {}, 0
while todo or run:
    for n, p in list(run.items()):
        if p.poll() is not None:
            del run[n]
            if ok(n):
                clean(n)
    if time.time() - memt > 60:
        memt = time.time()
        m = {l.split(":")[0]: int(l.split()[1]) // 1024 for l in open("/proc/meminfo")}
        open(f"{O}/mem.log", "a").write(f"{time.strftime('%F_%T')} avail {m['MemAvailable']} swapfree {m['SwapFree']} sim {len(run)}\n")
    if todo and len(run) < JOBS:
        n, a = todo.pop(0)
        clean(n)
        f = open(f"{O}/{n}.log", "w")
        run[n] = subprocess.Popen(f"timeout 172800 python3 xgregorm_gen_scenario.py {spec['base']} {a} --name {n}", shell=True, stdout=f, stderr=subprocess.STDOUT)
        time.sleep(20)  # volny port SUMO
    time.sleep(5)
fail = [f"{c}_s{sd}" for sd in spec["seeds"] for c in spec["configs"] if not ok(f"{c}_s{sd}")]
open(f"{O}/{spec['out']}Fail", "w").write("\n".join(fail) + "\n") if fail else None
open(f"{O}/{spec['out']}Done", "w").write("FINISHED\n")
