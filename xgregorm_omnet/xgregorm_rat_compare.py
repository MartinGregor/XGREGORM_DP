#!/usr/bin/env python3
# xgregorm 2026-11-17: vyber technologie (FW, MIX, Q, PM, PX) v OMNeT++ / Veins na tom istom scenari SUMO ako v ns-3.
#
# OMNeT++ nema NR-V2X sidelink, druha priama technologia je preto druhe radio 802.11p na samostatnom kanali (XgCar.nic2,
# connectionManager2): bez vzajomneho rusenia, rovnaky model sirenia. Kvalitu, mapu a rozhodnutie pocita to iste jadro
# ako ns-3 (src/automotive/model/Applications/xgregorm-qos-core.h). Porovnava sa teda vyber technologie a jeho poradie,
# nie fyzika NR. Zony = rovnaky kontrolovany experiment ako --zones v ns-3 (strata prijmu technologie podla polohy prijimaca).
#
# Pouzitie (z ns-3-dev):
#   python3 xgregorm_omnet/xgregorm_rat_compare.py --seeds 1 2 3 --zones C --policies FW MIX Q PM PX
# Vystup: xgregorm_omnet/run_RAT_<zony>_s<seed>/<politika>.log (riadky XGREGORM-V2V, XGREGORM-OMNET-RAT, ...).
# Zodpovedajuca seria ns-3: xgregorm_logs/ratcmp.json.
import argparse, os, re, socket, subprocess, sys, time

ZONES = {
    "A": "0:150:0:999:W:0.7+2:150:0:999:N:0.7+5:150:0:999:W:0.7+8:150:0:999:N:0.7",
    "C": "0:150:0:60:W:0.7+2:150:0:60:N:0.7+5:150:0:60:W:0.7+8:150:0:60:N:0.7+0:150:60:90:N:0.7+2:150:60:90:W:0.7"
         "+5:150:60:90:N:0.7+8:150:60:90:W:0.7+1:150:90:999:W:0.7+3:150:90:999:N:0.7+6:150:90:999:W:0.7+7:150:90:999:N:0.7",
    "D": "0:150:0:50:N:0.7+3:150:0:50:W:0.7+6:150:0:50:N:0.7+8:150:0:50:W:0.7+1:150:50:85:N:0.7+2:150:50:85:N:0.7"
         "+5:150:50:85:W:0.7+7:150:50:85:W:0.7+0:150:85:999:W:0.7+3:150:85:999:N:0.7+4:150:85:999:W:0.7+7:150:85:999:N:0.7",
    "T": "",
}
ap = argparse.ArgumentParser()
ap.add_argument("--seeds", type=int, nargs="+", default=[1, 2, 3])
ap.add_argument("--zones", default="C", choices=list(ZONES))
ap.add_argument("--policies", nargs="+", default=["FW", "MIX", "Q", "PM", "PX"])
ap.add_argument("--vehicles", type=int, default=140)
ap.add_argument("--sim-time", type=float, default=120)
ap.add_argument("--warmup", type=float, default=30)
ap.add_argument("--tx-power", type=float, default=23)
ap.add_argument("--cam-bytes", type=int, default=121)
ap.add_argument("--omnet", default=os.path.expanduser("~/omnet/omnetpp-6.0.3"))
ap.add_argument("--veins", default=os.path.expanduser("~/omnet/veins-veins-5.2"))
a = ap.parse_args()

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OM = os.path.join(ROOT, "xgregorm_omnet")
os.chdir(ROOT)
os.environ.setdefault("SUMO_HOME", "/usr/local/share/sumo")
env = dict(os.environ)
env["PATH"] = f"{a.omnet}/bin:" + env["PATH"]
env["LD_LIBRARY_PATH"] = f"{a.omnet}/lib:{a.veins}/src:" + env.get("LD_LIBRARY_PATH", "")
s = socket.socket()
if s.connect_ex(("localhost", 9999)) != 0:
    subprocess.Popen([sys.executable, f"{a.veins}/bin/veins_launchd", "-c", "sumo", "-p", "9999"], env=env,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    time.sleep(2)
s.close()

for seed in a.seeds:
    name = f"RAT_{a.zones}_s{seed}"
    run = os.path.join(OM, f"run_{name}")
    if not os.path.isfile(os.path.join(run, "omnetpp.ini")):
        # scenar ako ns-3 (rovnaky generator, mapa, auta, RSU, budovy), bez simulacie ns-3
        subprocess.run(f"python3 xgregorm_gen_scenario.py --map city-center --vehicles {a.vehicles} --seed {seed} "
                       f"--channel urban --tls-radius -1 --name {name}", shell=True, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        scen = os.path.join(ROOT, "src/automotive/examples", f"xgregorm_{name}")
        subprocess.run([sys.executable, os.path.join(OM, "export_scenario.py"), "--scenario", scen, "--out", run,
                        "--sim-time", str(a.sim_time), "--warmup", str(a.warmup), "--seed", str(seed),
                        "--tx-power", str(a.tx_power), "--rx-min", "-88", "--cam-bytes", str(a.cam_bytes),
                        "--channel", "urban"], check=True)
        subprocess.run(["rm", "-rf", scen, os.path.join(ROOT, f"xgregorm_tmp_{name}")])
    base = open(os.path.join(run, "omnetpp.ini")).read()
    # nastavenia radia nic -> rovnake pre nic2 (druha technologia), connectionManager2 ako connectionManager
    nic2 = "\n".join(l.replace(".nic.", ".nic2.") for l in base.splitlines() if ".nic." in l)
    for pol in a.policies:
        ini = os.path.join(run, f"omnetpp_{pol}.ini")
        with open(ini, "w") as f:
            # prve zhodne nastavenie vyhrava: vlastne riadky pred include
            f.write(f"""[General]
*.manager.moduleType = "xgregorm.XgCar"
*.node[*].nic2.phy80211p.antennaOffsetY = 0 m
*.node[*].nic2.phy80211p.antennaOffsetZ = 1.5 m
*.connectionManager2.sendDirect = true
*.connectionManager2.maxInterfDist = 2000m
*.connectionManager2.drawMaxIntfDist = false
*.**.nic2.mac1609_4.useServiceChannel = false
*.**.nic2.mac1609_4.bitrate = 6Mbps
*.**.nic2.mac1609_4.headerLength = 0 bit
*.**.nic2.phy80211p.useNoiseFloor = true
*.**.nic2.phy80211p.noiseFloor = -97dBm
*.**.nic2.phy80211p.usePropagationDelay = true
*.**.nic2.phy80211p.antenna = xmldoc("../antenna.xml", "/root/Antenna[@id='isotropic']")
{nic2}
*.node[*].appl.twoRat = true
*.node[*].appl.policy = "{pol}"
*.node[*].appl.zones = "{ZONES[a.zones]}"
*.node[*].appl.remPeriod = 0.1s
include omnetpp.ini
""")
        log = os.path.join(run, f"{pol}.log")
        t = time.time()
        with open(log, "w") as f:
            subprocess.run([os.path.join(OM, "src", "xgregorm_omnet"), "-u", "Cmdenv", "-n", f"{OM}/src:{a.veins}/src/veins",
                            os.path.basename(ini)], cwd=run, env=env, stdout=f, stderr=subprocess.STDOUT)
        txt = open(log, errors="replace").read()
        m = re.search(r"^XGREGORM-V2V,EXP:(\d+),RX:(\d+)", txt, re.M)
        r = re.search(r"^XGREGORM-OMNET-RAT,(.*)$", txt, re.M)
        prr = f"{int(m.group(2)) / int(m.group(1)):.4f}" if m and int(m.group(1)) else "-"
        print(f"{name} {pol:4} PRR {prr}  {r.group(1) if r else 'chyba, pozri ' + log}  {time.time() - t:.0f} s", flush=True)
