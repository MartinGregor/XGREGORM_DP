#!/usr/bin/env python3
"""xgregorm: ten isty scenar SUMO a tie iste parametre v ns-3 (VaN3Twin) aj v OMNeT++ (Veins), vysledky vedla seba.

Spolocna konfiguracia (to, co vedia oba simulatory rovnako): len 802.11p (ITS-G5), CAM podla ETSI, podpis ako
model velkosti (--security 1), 6 Mbit/s, kanal LogDistance (exponent 3, 46.68 dB v 1 m = "authors" v ns-3), bez
budov, semafory podla pevneho programu SUMO (--tls-radius -1, aby radio neovplyvnilo pohyb aut - inak by sa pohyb
v simulatoroch lisil). Vystup: dorucenie auto-auto a auto-RSU po 50 m, PRR do 100 m, oneskorenie, pocty ramcov.

  cd ~/VaN3Twin/ns-3-dev
  python3 xgregorm_omnet/xgregorm_compare.py --vehicles 140 --sim-time 60 --warmup 20 --seed 10 --tx-power 30

Predpoklady: ns-3 prelozene (./ns3 build), OMNeT++ 6.0.3 a Veins 5.2 (pozri xgregorm_omnet/README.md),
projekt prelozeny (make v xgregorm_omnet). veins_launchd spusti skript sam, ak nebezi.
"""
import argparse, os, re, shutil, socket, subprocess, sys, time

ap = argparse.ArgumentParser()
ap.add_argument("--name", default="CMP")
ap.add_argument("--map", default="city-center")
ap.add_argument("--cc-grid", type=int, default=3)
ap.add_argument("--vehicles", type=int, default=140)
ap.add_argument("--sim-time", type=float, default=60)
ap.add_argument("--warmup", type=float, default=20)
ap.add_argument("--seed", type=int, default=10)
ap.add_argument("--tx-power", type=float, default=30, help="[dBm]")
ap.add_argument("--rx-min", type=float, default=-88, help="citlivost 802.11p [dBm]")
ap.add_argument("--channel", default="authors", choices=["authors", "urban"],
                help="authors = LogDistance bez budov; urban = 3GPP TR 37.885 V2V Urban s budovami a unikmi (ako v praci)")
ap.add_argument("--cam-bytes", type=int, default=121, help="ramec CAM bez podpisu v OMNeT [B] (kalibrovane na ns-3)")
ap.add_argument("--omnet", default=os.path.expanduser("~/omnet/omnetpp-6.0.3"))
ap.add_argument("--veins", default=os.path.expanduser("~/omnet/veins-veins-5.2"))
ap.add_argument("--skip-ns3", action="store_true", help="pouzi existujuci log ns-3")
a = ap.parse_args()

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OM = os.path.join(ROOT, "xgregorm_omnet")
os.chdir(ROOT)
os.environ.setdefault("SUMO_HOME", "/usr/local/share/sumo")
logs = os.path.join(OM, f"run_{a.name}")
os.makedirs(logs, exist_ok=True)
ns3log = os.path.join(logs, "ns3.log")
omlog = os.path.join(logs, "omnet.log")

# 1. ns-3
ns3args = (f"--map {a.map} --vehicles {a.vehicles} --sim-time {a.sim_time} --warmup {a.warmup} --seed {a.seed} "
           f"--run hybrid --tech 80211p --policy all --security 1 --wifi-bcast-rate 6 --wifi-rx-min {a.rx_min} "
           f"--tx-power {a.tx_power} --tls-radius -1 --range-stats 1 --channel {a.channel} --name {a.name}"
           + (f" --cc-grid {a.cc_grid}" if a.map == "city-center" and a.cc_grid != 3 else ""))
if not a.skip_ns3:
    print("ns-3:", ns3args, flush=True)
    t = time.time()
    with open(ns3log, "w") as f:
        subprocess.run(f"python3 xgregorm_gen_scenario.py {ns3args}", shell=True, stdout=f, stderr=subprocess.STDOUT)
    print(f"  {time.time() - t:.0f} s", flush=True)

# 2. OMNeT++ / Veins na tom istom scenari
scen = os.path.join(ROOT, "src/automotive/examples", f"xgregorm_{a.name}")
if a.skip_ns3 and not os.path.isdir(scen) and os.path.isfile(os.path.join(logs, "omnetpp.ini")):
    print("scenar ns-3 uz neexistuje, pouzijem existujuci beh OMNeT++", logs)
else:
  subprocess.run([sys.executable, os.path.join(OM, "export_scenario.py"), "--scenario", scen, "--out", logs,
                "--sim-time", str(a.sim_time), "--warmup", str(a.warmup), "--seed", str(a.seed),
                "--tx-power", str(a.tx_power), "--rx-min", str(a.rx_min), "--cam-bytes", str(a.cam_bytes),
                "--channel", a.channel], check=True)
env = dict(os.environ)
env["PATH"] = f"{a.omnet}/bin:" + env["PATH"]
env["LD_LIBRARY_PATH"] = f"{a.omnet}/lib:{a.veins}/src:" + env.get("LD_LIBRARY_PATH", "")
s = socket.socket()
if s.connect_ex(("localhost", 9999)) != 0:
    subprocess.Popen([sys.executable, f"{a.veins}/bin/veins_launchd", "-c", "sumo", "-p", "9999"], env=env,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    time.sleep(2)
s.close()
print("OMNeT++ / Veins", flush=True)
t = time.time()
with open(omlog, "w") as f:
    subprocess.run([os.path.join(OM, "src", "xgregorm_omnet"), "-u", "Cmdenv", "-n", f"{OM}/src:{a.veins}/src/veins",
                    "omnetpp.ini"], cwd=logs, env=env, stdout=f, stderr=subprocess.STDOUT)
print(f"  {time.time() - t:.0f} s", flush=True)


# 3. porovnanie
def parse(path):
    txt = open(path, errors="replace").read()
    r = {}
    m = re.search(r"^XGREGORM-V2V,EXP:(\d+),RX:(\d+)", txt, re.M)
    r["v2v"] = (int(m.group(1)), int(m.group(2))) if m else None
    m = re.search(r"^XGREGORM-V2VLAT,W_SUM:([0-9.e+-]+),W_N:(\d+)", txt, re.M)
    r["lat"] = float(m.group(1)) / max(1, int(m.group(2))) * 1000 if m else None
    m = re.search(r"^XGREGORM-WIFIFRAMES,N:(\d+),BYTES:(\d+)", txt, re.M)
    r["frames"] = (int(m.group(1)), int(m.group(2))) if m else None
    m = re.search(r"^XGREGORM-RANGE,(.*)$", txt, re.M)
    r["bins"] = [tuple(map(int, b.split(":")[1].split("/"))) for b in m.group(1).split(",")] if m else []
    m = re.search(r"Wall clock: (\d+) s", txt)
    r["wall"] = int(m.group(1)) if m else None
    return r


n, o = parse(ns3log), parse(omlog)
if not n["bins"] or not o["bins"]:
    sys.exit(f"chyba: chyba vysledok (pozri {ns3log} a {omlog})")
print(f"\n{'':22} {'ns-3':>18} {'OMNeT++/Veins':>18}")
pr = lambda v: f"{v[1] / v[0]:.4f} ({v[0]})" if v and v[0] else "-"
print(f"{'PRR auto-auto <100 m':22} {pr(n['v2v']):>18} {pr(o['v2v']):>18}")
print(f"{'oneskorenie [ms]':22} {n['lat']:>18.3f} {o['lat']:>18.3f}")
print(f"{'ramce / B na ramec':22} {n['frames'][0]:>10} / {n['frames'][1] / n['frames'][0]:5.1f} {o['frames'][0]:>10} / {o['frames'][1] / o['frames'][0]:5.1f}")
print("\nvzdialenost   auto-auto ns-3 / OMNeT       auto-RSU ns-3 / OMNeT   (ocakavane prijmy ns-3 / OMNeT)")
for b, (x, y) in enumerate(zip(n["bins"], o["bins"])):
    if x[0] == 0 and y[0] == 0:
        continue
    f = lambda e, rx: f"{rx / e:.3f}" if e else "  -  "
    print(f"{b * 50:4d}-{b * 50 + 50:4d} m   {f(x[0], x[1]):>7} / {f(y[0], y[1]):<7}          {f(x[2], x[3]):>7} / {f(y[2], y[3]):<7}"
          f"    ({x[0]} / {y[0]})")
print(f"\nlogy: {ns3log}, {omlog}")
# scenar ns-3 uz netreba (kopia suborov SUMO je v priecinku behu OMNeT++)
shutil.rmtree(scen, ignore_errors=True)
shutil.rmtree(os.path.join(ROOT, f"xgregorm_tmp_{a.name}"), ignore_errors=True)
