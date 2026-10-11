#!/usr/bin/env python3
"""
xgregorm_gen_scenario.py - generates a VaN3Twin scenario and optionally runs the simulation
with a final summary. The VaN3Twin source code is NOT modified, the original applications are used.

Map:      3x3 city intersections (50 km/h, 1 lane, 200 m apart -> 400x400 m town centre)
          + circular bypass (90 km/h, 2 lanes, 250 m from the city edge, radius 450 m)
          connected to the middle of each side of the city by 70 km/h roads.
          - traffic lights are given ONLY to 4-arm intersections (the authors' trafficManager
            indexes exactly 4 approaches and reads out of range on a 3-arm intersection),
          - the bypass gates use "right_before_left", otherwise vehicles coming from the city
            wait up to 40 s to merge into the dense bypass traffic.
Traffic:  a fixed number of vehicles, all created at t=0, driving in closed loops forever,
          so the number of vehicles in the network stays constant.
Output:   no CSV is left on disk - helper files are written to a temporary folder, parsed
          and deleted; everything important is printed to the screen.

Run from ~/VaN3Twin/ns-3-dev:
    python3 xgregorm_gen_scenario.py --diag                    # generate + check traffic flow
    python3 xgregorm_gen_scenario.py --run v2i-speed-80211p    # generate + run + summary
    python3 xgregorm_gen_scenario.py --run v2i-traffic-80211p --gui
    python3 xgregorm_gen_scenario.py --run all
"""
import argparse, math, os, re, shutil, subprocess, sys, time
import xml.etree.ElementTree as ET

P = "xgregorm_"

ap = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
ap.add_argument("--name", default="mesto", help="scenario name -> src/automotive/examples/xgregorm_<name>/")
# map
ap.add_argument("--grid", type=int, default=3, help="number of intersections per row")
ap.add_argument("--step", type=float, default=200,
                help="distance between intersections [m]; 150-250 m is a typical urban block between "
                     "signalised junctions, and with the default 802.11p channel (~200 m range at 30 dBm) "
                     "RSUs on neighbouring junctions just overlap")
ap.add_argument("--odstup", type=float, default=250, help="distance of the bypass from the city edge [m]")
ap.add_argument("--v-mesto", type=float, default=13.89, help="city speed [m/s] (13.89 = 50 km/h)")
ap.add_argument("--v-obchvat", type=float, default=25.0, help="bypass speed [m/s] (25 = 90 km/h)")
ap.add_argument("--v-privod", type=float, default=19.44,
                help="speed on the roads joining the city to the bypass [m/s] (19.44 = 70 km/h)")
ap.add_argument("--lanes-mesto", type=int, default=1, help="city lanes (per direction)")
ap.add_argument("--lanes-obchvat", type=int, default=2, help="bypass lanes (per direction)")
ap.add_argument("--zelena", type=float, default=30, help="green time of the fixed traffic light program [s]")
ap.add_argument("--zlta", type=float, default=3, help="yellow time [s]")
ap.add_argument("--kruh", type=int, default=10,
                help="number of geometry points per 45 deg bypass segment; higher = smoother circle")
ap.add_argument("--brany", default="right_before_left",
                choices=["right_before_left", "priority", "allway_stop"],
                help="junction type where the city connects to the bypass")
# traffic
ap.add_argument("--vehicles", type=int, default=10, help="number of vehicles (all created at t=0)")
ap.add_argument("--emergency", type=float, default=0.1, help="share of ambulances 0..1 (0.2 = every 5th)")
ap.add_argument("--repeat", type=int, default=100, help="how many times a vehicle repeats its loop")
ap.add_argument("--rsu", type=int, default=5, help="number of RSUs")
# simulation
ap.add_argument("--sim-time", type=float, default=120, help="simulation duration [s]")
ap.add_argument("--baseline", type=float, default=100, help="reference distance for the PRR computation [m]")
ap.add_argument("--tx-power", type=float, default=30, help="transmission power [dBm] (802.11p only)")
ap.add_argument("--srs", type=int, default=320, choices=[40, 80, 160, 320],
                help="LTE only: ns3::LteEnbRrc::SrsPeriodicity - the cell can serve roughly this "
                     "many UEs; the default 40 in ns-3 crashes with more than ~39 vehicles")
ap.add_argument("--sumo-updates", type=float, default=0.1,
                help="ns-3 to SUMO synchronisation interval [s]; smaller = finer, but bigger netstate dump")
# trafficManager (the authors' application)
ap.add_argument("--phase-time", type=int, default=30,
                help="base phase duration for trafficManager [s]; green = phase-time * load ratio")
ap.add_argument("--threshold", type=float, default=70,
                help="load threshold in %% at which trafficManager switches the lights immediately")
ap.add_argument("--tls-radius", type=float, default=50,
                help="trafficManager 802.11p: every RSU controls only the traffic lights within this "
                     "radius [m] (its own intersection, as in reality; also the default of the example); "
                     "0 = every RSU controls all of them (authors' original). LTE always has 1 server for all")
ap.add_argument("--tls-coord", type=int, default=0, choices=[0, 1],
                help="trafficManager 802.11p: 1 = RSUs share vehicle positions over a wired backhaul "
                     "(coordination of neighbouring intersections)")
ap.add_argument("--coord-lookahead", type=float, default=0,
                help="with --tls-coord 1: a vehicle announced by a neighbouring RSU is counted only this many "
                     "seconds before its estimated arrival; 0 = immediately")
ap.add_argument("--seed", type=int, default=None,
                help="random seed: ns-3 --RngRun for all programs, SUMO seed only for v2i-traffic-80211p "
                     "(--sumo-seed); not set = defaults (SUMO seed 10), identical to the reference results")
ap.add_argument("--run", default="nic",
                choices=["nic", "v2i-speed-80211p", "v2i-speed-lte", "v2i-traffic-80211p", "v2i-traffic-lte",
                         "v2v-80211p", "all"],
                help="which simulation to run after generating the scenario")
ap.add_argument("--gui", action="store_true", help="enable the SUMO GUI")
ap.add_argument("--diag", action="store_true",
                help="before ns-3, run plain SUMO and report how smoothly the traffic flows")
a = ap.parse_args()

if not os.path.isfile("./ns3"):
    sys.exit("Error: run this from the ns-3-dev folder (next to the ./ns3 file)")

out = os.path.join("src/automotive/examples", f"{P}{a.name}")
os.makedirs(out, exist_ok=True)
nod_f, edg_f = os.path.join(out, f"{P}map.nod.xml"), os.path.join(out, f"{P}map.edg.xml")
net_f, rou_f = os.path.join(out, f"{P}map.net.xml"), os.path.join(out, f"{P}cars.rou.xml")
cfg_f = os.path.join(out, f"{P}map.sumo.cfg")
st_f = os.path.join(out, "stations.xml")  # fixed name, VaN3Twin has it hardcoded

# =============================================================== 1) ROAD NETWORK
MAXC = (a.grid - 1) * a.step
MID = MAXC / 2
R = MID + a.odstup
g, m = a.grid - 1, a.grid // 2
BRANY = {f"C{g}{m}": "B0", f"C{m}{g}": "B2", f"C0{m}": "B4", f"C{m}0": "B6"}

stupen = {f"C{i}{j}": 0 for i in range(a.grid) for j in range(a.grid)}
for i in range(a.grid):
    for j in range(a.grid):
        if i + 1 < a.grid: stupen[f"C{i}{j}"] += 1; stupen[f"C{i + 1}{j}"] += 1
        if j + 1 < a.grid: stupen[f"C{i}{j}"] += 1; stupen[f"C{i}{j + 1}"] += 1
for c in BRANY:
    stupen[c] += 1

nodes, edges = [], []


def edge(x, y, lanes, speed, prio, shape_ab="", shape_ba=""):
    edges.append((x, y, lanes, speed, prio, shape_ab))
    edges.append((y, x, lanes, speed, prio, shape_ba))


n_tls = 0
for i in range(a.grid):
    for j in range(a.grid):
        n = f"C{i}{j}"
        typ = "traffic_light" if stupen[n] == 4 else "priority"  # TM handles 4-arm junctions only
        if typ == "traffic_light": n_tls += 1
        nodes.append((n, i * a.step, j * a.step, typ))
for i in range(a.grid):
    for j in range(a.grid):
        if i + 1 < a.grid: edge(f"C{i}{j}", f"C{i + 1}{j}", a.lanes_mesto, a.v_mesto, 2)
        if j + 1 < a.grid: edge(f"C{i}{j}", f"C{i}{j + 1}", a.lanes_mesto, a.v_mesto, 2)

napojene = set(BRANY.values())
for k, ang in enumerate(range(0, 360, 45)):
    # where the city joins the bypass, the bypass must not have absolute priority,
    # otherwise vehicles from the city cannot merge (measured waits of up to 38 s)
    typ = a.brany if f"B{k}" in napojene else "priority"
    nodes.append((f"B{k}", round(MID + R * math.cos(math.radians(ang)), 1),
                  round(MID + R * math.sin(math.radians(ang)), 1), typ))
def oblouk(uhol_od, uhol_do, body):
    """geometry of a circular arc - without it the bypass would be a straight-sided octagon"""
    pts = []
    for st in range(body + 1):
        ang = uhol_od + (uhol_do - uhol_od) * st / body
        pts.append(f"{MID + R * math.cos(math.radians(ang)):.2f},"
                   f"{MID + R * math.sin(math.radians(ang)):.2f}")
    return " ".join(pts)


for k in range(8):
    u1, u2 = k * 45, (k + 1) * 45
    edge(f"B{k}", f"B{(k + 1) % 8}", a.lanes_obchvat, a.v_obchvat, 5,
         oblouk(u1, u2, a.kruh), oblouk(u2, u1, a.kruh))
for c, b in BRANY.items():
    edge(c, b, a.lanes_mesto, a.v_privod, 4)

with open(nod_f, "w") as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<nodes>\n')
    for nid, x, y, t in nodes:
        f.write(f'    <node id="{nid}" x="{x}" y="{y}" type="{t}"/>\n')
    f.write('</nodes>\n')
with open(edg_f, "w") as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<edges>\n')
    for x, y, lanes, speed, prio, tvar in edges:
        sh = f' shape="{tvar}"' if tvar else ""
        f.write(f'    <edge id="{x}_{y}" from="{x}" to="{y}" numLanes="{lanes}" '
                f'speed="{speed}" priority="{prio}"{sh}/>\n')
    f.write('</edges>\n')

r = subprocess.run(["netconvert", "-n", nod_f, "-e", edg_f, "-o", net_f,
                    "--offset.disable-normalization", "--tls.default-type", "static",
                    "--no-turnarounds.except-deadend",
                    "--tls.green.time", str(a.zelena), "--tls.yellow.time", str(a.zlta)],
                   capture_output=True, text=True)
if r.returncode != 0 or not os.path.isfile(net_f):
    print(r.stdout[-2000:], r.stderr[-2000:])
    sys.exit("Error: netconvert failed")

_n = open(net_f).read()
if 'projParameter="!"' in _n:  # geo-reference needed for CAM, DENM and GeoArea
    _n = re.sub(r'netOffset="[^"]*"', 'netOffset="-394175.96,-4989734.74"', _n, count=1)
    _n = re.sub(r'projParameter="!"',
                'projParameter="+proj=utm +zone=32 +ellps=WGS84 +datum=WGS84 +units=m +no_defs"', _n, count=1)
    open(net_f, "w").write(_n)


# ======================================================= 2) LOOPS AND VEHICLES
def E(x, y): return f"{x}_{y}"


okruhy = [[E(f"B{k}", f"B{(k + 1) % 8}") for k in range(8)],
          [E(f"B{(k + 1) % 8}", f"B{k}") for k in range(7, -1, -1)]]
# block loops run clockwise = right turns only
for i in range(a.grid - 1):
    for j in range(a.grid - 1):
        okruhy.append([E(f"C{i}{j}", f"C{i}{j + 1}"), E(f"C{i}{j + 1}", f"C{i + 1}{j + 1}"),
                       E(f"C{i + 1}{j + 1}", f"C{i + 1}{j}"), E(f"C{i + 1}{j}", f"C{i}{j}")])
stred = f"C{m}{m}"
for c1, b1, b2, b3, c2 in [(f"C{g}{m}", "B0", "B1", "B2", f"C{m}{g}"),
                           (f"C{m}{g}", "B2", "B3", "B4", f"C0{m}"),
                           (f"C0{m}", "B4", "B5", "B6", f"C{m}0"),
                           (f"C{m}0", "B6", "B7", "B0", f"C{g}{m}")]:
    okruhy.append([E(c1, b1), E(b1, b2), E(b2, b3), E(b3, c2), E(c2, stred), E(stred, c1)])

every = round(1 / a.emergency) if a.emergency > 0 else 0
n_amb = 0
with open(rou_f, "w") as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<routes>\n')
    f.write('    <vType id="car" vClass="passenger" accel="2.6" decel="4.5" sigma="0.5" '
            'length="4.5" maxSpeed="36"/>\n')
    f.write('    <vType id="ambulance" vClass="emergency" guiShape="emergency" color="1,0,0" '
            'accel="3.0" decel="7.5" sigma="0.2" length="6.0" maxSpeed="40" speedFactor="1.3"/>\n')
    for k in range(1, a.vehicles + 1):
        okruh = okruhy[(k - 1) % len(okruhy)]
        rot = ((k - 1) // len(okruhy)) % len(okruh)
        trasa = okruh[rot:] + okruh[:rot]
        typ = "car"
        if every and k % every == 0:
            typ = "ambulance"
            n_amb += 1
        f.write(f'    <vehicle id="veh{k}" type="{typ}" depart="0" departLane="best" departSpeed="max">\n')
        f.write(f'        <route edges="{" ".join(trasa)}" repeat="{a.repeat}"/>\n    </vehicle>\n')
    f.write('</routes>\n')

# ====================================================================== 3) RSU
additional = []
if a.rsu > 0:
    nroot = ET.parse(net_f).getroot()
    junc = [j for j in nroot.findall("junction")
            if j.get("type") != "internal" and not j.get("id", "").startswith(":")]


    def XY(j):
        return (float(j.get("x")), float(j.get("y")))


    tls = [XY(j) for j in junc if j.get("type") == "traffic_light"]
    mesto = [XY(j) for j in junc if j.get("id", "").startswith("C") and XY(j) not in tls]
    obchvat = [XY(j) for j in junc if j.get("id", "").startswith("B")]
    # order: signalised intersections first, then the remaining city ones, bypass last
    chosen = []
    for uroven in (tls, mesto, obchvat):
        volne = [p for p in uroven if p not in chosen]
        while volne and len(chosen) < a.rsu:
            if not chosen:
                cx = sum(x for x, _ in volne) / len(volne)
                cy = sum(y for _, y in volne) / len(volne)
                p = min(volne, key=lambda q: (q[0] - cx) ** 2 + (q[1] - cy) ** 2)
            else:
                p = max(volne, key=lambda q: min(math.hypot(q[0] - c[0], q[1] - c[1]) for c in chosen))
            chosen.append(p)
            volne.remove(p)
        if len(chosen) >= a.rsu:
            break
    with open(st_f, "w") as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n<additional>\n')
        for i, (x, y) in enumerate(chosen):
            # the V2X:STATION comment must stay on the same line, the ID must be poi_<number>
            f.write(f'    <!-- V2X:STATION --><poi id="poi_{i}" color="orange" layer="202.00" '
                    f'x="{x:.2f}" y="{y:.2f}"/>\n')
        f.write('</additional>\n')
    additional.append("stations.xml")

add_line = f'\n        <additional-files value="{",".join(additional)}"/>' if additional else ""

with open(cfg_f, "w") as f:
    f.write(f"""<configuration>
    <input>
        <net-file value="{P}map.net.xml"/>
        <route-files value="{P}cars.rou.xml"/>{add_line}
    </input>
    <time>
        <begin value="0"/>
        <end value="{int(a.sim_time) + 600}"/>
        <step-length value="0.1"/>
    </time>
    <processing>
        <time-to-teleport value="120"/>
        <ignore-route-errors value="true"/>
    </processing>
    <report>
        <no-step-log value="true"/>
    </report>
</configuration>
""")

print(f"Scenario: {out}/")
print(f"  map          : city {a.grid}x{a.grid} ({MAXC:.0f}x{MAXC:.0f} m, spacing {a.step:.0f} m), "
      f"bypass radius {R:.0f} m")
print(f"  traffic light: {n_tls} (4-arm intersections only), green {a.zelena:.0f} s, yellow {a.zlta:.0f} s")
print(f"  gates        : {a.brany} (city to bypass connection)")
print(f"  traffic      : {a.vehicles} vehicles ({n_amb} ambulances), {len(okruhy)} loops, all start at t=0")
print(f"  RSU          : {a.rsu}")
if a.step < 100:
    print("  WARNING: spacing below 100 m - the intersections are almost longer than the streets "
          "between them, vehicles will block each other")
print(f"  preview      : sumo-gui -c {cfg_f}")


# ================================================ 4) TRAFFIC FLOW DIAGNOSTICS
def diagnostika(dlzka=600):
    """Runs plain SUMO (without ns-3) and measures how smoothly the traffic flows."""
    fcd = os.path.join(out, "_fcd.xml")
    print(f"\n>>> Traffic diagnostics (SUMO without ns-3, {dlzka} s) ...")
    r = subprocess.run(["sumo", "-c", cfg_f, "--fcd-output", fcd, "--end", str(dlzka)],
                       capture_output=True, text=True)
    tele = len(re.findall(r"teleport", r.stdout + r.stderr, re.I))
    if not os.path.isfile(fcd):
        print("   SUMO failed:", (r.stdout + r.stderr)[-500:])
        return
    rychlosti, stoji, najdlhsie, hrana = {}, {}, {}, {}
    for _, el in ET.iterparse(fcd, events=("end",)):
        if el.tag != "vehicle":
            continue
        i, v = el.get("id"), float(el.get("speed"))
        rychlosti.setdefault(i, []).append(v)
        if v < 0.1:
            stoji[i] = stoji.get(i, 0) + 1
            if stoji[i] > najdlhsie.get(i, 0):
                najdlhsie[i] = stoji[i]
                hrana[i] = el.get("lane")
        else:
            stoji[i] = 0
        el.clear()
    os.remove(fcd)
    if not rychlosti:
        print("   No vehicles?")
        return
    vsetky = [x for v in rychlosti.values() for x in v]
    priem = {k: sum(v) / len(v) for k, v in rychlosti.items()}
    najhorsie = sorted(najdlhsie.items(), key=lambda kv: -kv[1])[:5]
    print(f"   average speed of all vehicles: {sum(vsetky) / len(vsetky):.2f} m/s "
          f"({3.6 * sum(vsetky) / len(vsetky):.0f} km/h)")
    print(f"   teleports (deadlocks SUMO had to resolve): {tele}")
    print("   longest single stop per vehicle:")
    for k, d in najhorsie:
        print(f"     {k:<7} {d * 0.1:5.1f} s on {hrana.get(k, '?')}   (average speed {priem[k]:.2f} m/s)")
    zle = [k for k, d in najdlhsie.items() if d * 0.1 > 60]
    if zle:
        print(f"   WARNING: {len(zle)} vehicles stood longer than 60 s - lower --vehicles, "
              f"shorten --zelena or increase --step")
    else:
        print("   OK: no vehicle stood longer than 60 s, the traffic flows")


if a.diag:
    diagnostika(min(600, int(a.sim_time) + 300))

# ============================================================== 5) COMMANDS
S = out + "/"
ZAKLAD = (f"--sumo-folder={S} --mob-trace={P}cars.rou.xml --sumo-config={cfg_f} "
          f"--sim-time={int(a.sim_time)} --met-sup=true --baseline={a.baseline} "
          f"--sumo-updates={a.sumo_updates} "
          f"--sumo-gui={'true' if a.gui else 'false'}"
          + (f" --RngRun={a.seed}" if a.seed is not None else ""))
TM = f"--phase-time={a.phase_time} --threshold={a.threshold}"
TM_80211P = (f"{TM} --tls-radius={a.tls_radius} --tls-coord={'true' if a.tls_coord else 'false'}"
             f" --coord-lookahead={a.coord_lookahead}")
# SUMO gets its seed from the example on the command line (hardcoded 10 in the authors' examples, so
# a seed in .sumo.cfg is ignored); only v2i-trafficManager-80211p has the --sumo-seed option so far
if a.seed is not None:
    TM_80211P += f" --sumo-seed={a.seed}"   # local intersection control (xgregorm change in the authors' app)
LTE = f"--ns3::LteEnbRrc::SrsPeriodicity={a.srs}"     # otherwise ns-3 aborts with too many UEs
TMP = f"{P}tmp"

# program name, arguments, has a per-vehicle CSV
# --summary=true is added only where the example supports that option
PRIKAZY = {
    "v2i-speed-80211p": ("v2i-areaSpeedAdvisor-80211p",
                         f"{ZAKLAD} --tx-power={a.tx_power} --summary=true", True),
    "v2i-speed-lte": ("v2i-areaSpeedAdvisor-lte", f"{ZAKLAD} {LTE}", True),
    "v2i-traffic-80211p": ("v2i-trafficManager-80211p",
                           f"{ZAKLAD} {TM_80211P} --tx-power={a.tx_power} --summary=true", False),
    "v2i-traffic-lte": ("v2i-trafficManager-LTE", f"{ZAKLAD} {TM} {LTE} --summary=true", False),
    "v2v-80211p": ("v2v-emergencyVehicleAlert-80211p", f"{ZAKLAD} --tx-power={a.tx_power}", False),
}


# ======================================================= 6) RUN AND SUMMARY
def spusti(kluc):
    program, args, ma_csv = PRIKAZY[kluc]
    shutil.rmtree(TMP, ignore_errors=True)
    os.makedirs(TMP, exist_ok=True)
    if ma_csv:
        args += f" --csv-log={TMP}/log"
    args += f" --netstate-dump-file={TMP}/netstate.xml"
    print(f"\n>>> {program}\n")
    t0 = time.time()
    proc = subprocess.Popen(["./ns3", "run", f"{program} {args}"],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    vystup = []
    for riadok in proc.stdout:
        print(riadok, end="")
        vystup.append(riadok)
    proc.wait()
    suhrn(program, "".join(vystup), ma_csv, time.time() - t0, proc.returncode)
    shutil.rmtree(TMP, ignore_errors=True)


def doprava_z_netstate(cesta):
    """Traffic metrics from the SUMO netstate dump of the very same ns-3 run."""
    if not os.path.isfile(cesta):
        return {}, {}
    casy, sucet, vzorky, stoji_teraz, stoji_spolu, max_stoj = [], {}, {}, {}, {}, {}
    for _, el in ET.iterparse(cesta, events=("end",)):
        if el.tag == "timestep":
            if len(casy) < 2:
                casy.append(float(el.get("time")))
            el.clear()
            continue
        if el.tag != "vehicle":
            continue
        i, v = el.get("id"), float(el.get("speed"))
        sucet[i] = sucet.get(i, 0.0) + v
        vzorky[i] = vzorky.get(i, 0) + 1
        if v < 0.1:
            stoji_teraz[i] = stoji_teraz.get(i, 0) + 1
            stoji_spolu[i] = stoji_spolu.get(i, 0) + 1
            if stoji_teraz[i] > max_stoj.get(i, 0):
                max_stoj[i] = stoji_teraz[i]
        else:
            stoji_teraz[i] = 0
        el.clear()
    dt = (casy[1] - casy[0]) if len(casy) > 1 else 0.1
    per_veh = {}
    for i in sucet:
        per_veh[i] = {
            "speed": sucet[i] / vzorky[i],
            "stopped_pct": 100.0 * stoji_spolu.get(i, 0) / vzorky[i],
            "stopped_s": stoji_spolu.get(i, 0) * dt,
            "longest_stop": max_stoj.get(i, 0) * dt,
            "distance": sucet[i] * dt,
        }
    celkom = {}
    if per_veh:
        n = len(per_veh)
        celkom = {
            "speed": sum(d["speed"] for d in per_veh.values()) / n,
            "stopped_pct": sum(d["stopped_pct"] for d in per_veh.values()) / n,
            "stopped_s": sum(d["stopped_s"] for d in per_veh.values()) / n,
            "longest_stop": max(d["longest_stop"] for d in per_veh.values()),
            "longest_stop_avg": sum(d["longest_stop"] for d in per_veh.values()) / n,
            "distance": sum(d["distance"] for d in per_veh.values()) / n,
        }
    return per_veh, celkom


def suhrn(program, vystup, ma_csv, trvanie, rc):
    prr = re.search(r"Average PRR:\s*([\d.eE+-]+)", vystup)
    lat = re.search(r"Average latency \(ms\):\s*([\d.eE+-]+)", vystup)
    doprava, doprava_celkom = doprava_z_netstate(os.path.join(TMP, "netstate.xml"))
    teleporty = len(re.findall(r"teleport", vystup, re.I))

    # "INFO-vehX,KEY:number,KEY:number,..." - every application prints a slightly different key set
    info = {}
    for riadok in vystup.splitlines():
        riadok = riadok.strip()
        if not riadok.startswith("INFO-"):
            continue
        casti = riadok[5:].split(",")
        veh = casti[0].strip()
        udaje = {}
        for c in casti[1:]:
            if ":" in c:
                k, v = c.split(":", 1)
                try:
                    udaje[k.strip().upper()] = int(v.strip())
                except ValueError:
                    pass
        if udaje:
            info[veh] = udaje

    # RSU switches from the temporary per-vehicle CSV (last column = station ID of the sending RSU)
    prepnutia, postupnost, denm_csv = {}, {}, {}
    if ma_csv and os.path.isdir(TMP):
        for f in sorted(os.listdir(TMP)):
            if not f.endswith(".csv") or "-veh" not in f:
                continue
            veh = f[f.index("-veh") + 1:-4]
            rsus, riadkov = [], 0
            for riadok in open(os.path.join(TMP, f)):
                c = riadok.strip().split(",")
                if len(c) < 6 or c[0] == "messageID":
                    continue
                riadkov += 1
                try:
                    sid = int(c[5])
                except ValueError:
                    continue
                meno = f"poi_{sid - 1000000}" if sid >= 1000000 else str(sid)
                if not rsus or rsus[-1] != meno:
                    rsus.append(meno)
            denm_csv[veh] = riadkov
            prepnutia[veh] = max(0, len(rsus) - 1)
            postupnost[veh] = " -> ".join(rsus[:8]) + (" ..." if len(rsus) > 8 else "")

    def cislo(v):
        try:
            return int(v[3:])
        except ValueError:
            return 0

    vozidla = sorted(set(list(info) + list(prepnutia) + list(doprava)), key=cislo)
    print(f"\n================= SUMMARY: {program} =================")
    if rc != 0:
        print(f"  WARNING: the run finished with return code {rc}")
        if "SrsPeriodicity" in vystup:
            print(f"           The LTE cell cannot serve {a.vehicles} UEs with the current SRS "
                  f"periodicity ({a.srs}).")
            print("           Use a higher --srs (40/80/160/320) or fewer --vehicles.")
        if "died with" in vystup or "Segmentation" in vystup:
            print("           The numbers below come only from the part that ran before the crash.")
    cam_tx = cam_rx = denm_rx = cpm_rx = cpm_tx = prepn_spolu = 0
    je_lte = "lte" in program.lower()
    if vozidla:
        print(f"  {'vehicle':<8}{'CAM tx':>8}{'CAM rx':>8}{'DENM rx':>9}{'CPM rx':>8}{'RSU sw.':>9}"
              f"{'v [m/s]':>9}{'stop %':>8}{'stop tot[s]':>12}{'stop max[s]':>12}")
        for v in vozidla:
            u = info.get(v, {})
            c_tx = u.get("CAM-SENT", "-")
            c_rx = u.get("CAM-RECEIVED", "-")
            p_rx = u.get("CPM-RECEIVED", "-")
            d_rx = u.get("DENM-RECEIVED", denm_csv.get(v, "-"))
            d = doprava.get(v)
            if d:
                dop = (f"{d['speed']:>9.2f}{d['stopped_pct']:>8.1f}{d['stopped_s']:>12.1f}"
                       f"{d['longest_stop']:>12.1f}")
            else:
                dop = f"{'-':>9}{'-':>8}{'-':>12}{'-':>12}"
            print(f"  {v:<8}{str(c_tx):>8}{str(c_rx):>8}{str(d_rx):>9}{str(p_rx):>8}"
                  f"{str('-' if je_lte else prepnutia.get(v, '-')):>9}{dop}")

        def sucet(kluc):
            return sum(u.get(kluc, 0) for u in info.values())

        cam_tx = sucet("CAM-SENT")
        cam_rx = sucet("CAM-RECEIVED")
        cpm_rx = sucet("CPM-RECEIVED")
        cpm_tx = sucet("CPM-SENT")
        denm_rx = sucet("DENM-RECEIVED") or sum(denm_csv.values())
        prepn_spolu = sum(prepnutia.values()) if prepnutia else 0
        print("  " + "-" * 86)

    prr_v = float(prr.group(1)) if prr else float("nan")
    lat_v = float(lat.group(1)) if lat else float("nan")
    print("  NETWORK:")
    if je_lte:
        print(f"    PRR / packet loss          : not applicable for LTE (raw value {prr_v:.6f})")
        print("      WHY: LTE delivers a message through the eNodeB and the core network to one")
        print("           specific receiver, it is not broadcast over the air to the neighbours.")
        print(f"           The metric counts how many of the stations within {a.baseline:.0f} m of the")
        print("           sender received it - for unicast that is 0 by construction, no matter how")
        print("           well the network works. Compare latency and delivered DENM/CAM counts instead.")
    else:
        print(f"    PRR                        : {prr_v:.6f}   (baseline {a.baseline:.0f} m)")
        print(f"    packet loss                : {100 * (1 - prr_v):.2f} %")
    if a.sim_time < 60:
        print("      NOTE: PRR uses a 3 s window, with sim-time below 60 s the sample is small.")
    print(f"    average latency            : {lat_v:.3f} ms")
    # an application that does not print a counter must show n/a, not 0 (0 would look like a total loss)
    def hodnota(kluc, sucet_hodnot, aj_csv=False):
        hlasi = any(kluc in u for u in info.values()) or (aj_csv and denm_csv)
        return str(sucet_hodnot) if hlasi else "n/a"
    print(f"    CAM sent / received        : {hodnota('CAM-SENT', cam_tx)} / {hodnota('CAM-RECEIVED', cam_rx)}")
    print(f"    DENM received              : {hodnota('DENM-RECEIVED', denm_rx, True)}")
    print(f"    CPM sent / received        : {hodnota('CPM-SENT', cpm_tx)} / {hodnota('CPM-RECEIVED', cpm_rx)}")
    print("      (n/a = the application does not report this counter; actual delivery is in PRR above)")
    if prepnutia and je_lte:
        print("    RSU switches               : not applicable for LTE (one server, the RSUs are not used)")
    elif prepnutia:
        print(f"    RSU switches               : {prepn_spolu} total, "
              f"{prepn_spolu / len(prepnutia):.2f} per vehicle")
    if doprava_celkom:
        print("  TRAFFIC (from SUMO during the same run, so it already includes the V2X effect):")
        print(f"    average speed              : {doprava_celkom['speed']:.2f} m/s "
              f"({3.6 * doprava_celkom['speed']:.1f} km/h)")
        print(f"    time spent standing        : {doprava_celkom['stopped_pct']:.1f} % of the run")
        print(f"    total standing time        : {doprava_celkom['stopped_s']:.1f} s per vehicle "
              f"(sum of all its stops)")
        print(f"    single longest stop        : {doprava_celkom['longest_stop']:.1f} s (worst vehicle), "
              f"{doprava_celkom['longest_stop_avg']:.1f} s on average")
        print(f"    average distance travelled : {doprava_celkom['distance']:.0f} m per vehicle")
        print(f"    teleports (deadlocks)      : {teleporty}")
    print(f"  Wall clock: {trvanie:.0f} s | vehicles {a.vehicles} | RSU {a.rsu} | "
          f"sim-time {int(a.sim_time)} s | scenario {out}/")
    coord = re.findall(r"XGREGORM-COORD,(\S+?),TX:(\d+),RX:(\d+)", vystup)
    if coord:
        tx = sum(int(c[1]) for c in coord)
        rx = sum(int(c[2]) for c in coord)
        print(f"  wired RSU coordination: {tx} messages sent, {rx} received "
              f"({tx / max(a.sim_time, 1):.1f} msg/s over the whole network)")
    if program == "v2i-trafficManager-80211p":
        rezim = "all traffic lights (authors' original)" if a.tls_radius <= 0 else \
            f"own intersection only (radius {a.tls_radius:.0f} m)" + \
            (" + wired coordination with neighbours" if a.tls_coord else "")
        print(f"  traffic light control per RSU: {rezim}")
    # one line to copy into a spreadsheet when comparing your own solution with the baseline
    print("  CSV: program,vehicles,rsu,sim_time,PRR,loss_%,latency_ms,rsu_switches,speed_ms,stopped_%")
    prr_txt = "n/a" if je_lte else f"{prr_v:.6f}"
    loss_txt = "n/a" if je_lte else f"{100 * (1 - prr_v):.2f}"
    print(f"  CSV: {program},{a.vehicles},{a.rsu},{int(a.sim_time)},{prr_txt},"
          f"{loss_txt},{lat_v:.3f},{'n/a' if je_lte or not prepnutia else prepn_spolu},"
          f"{doprava_celkom.get('speed', float('nan')):.2f},"
          f"{doprava_celkom.get('stopped_pct', float('nan')):.1f}")
    print("=" * (len(program) + 38))


if a.run != "nic":
    for k in (list(PRIKAZY) if a.run == "all" else [a.run]):
        spusti(k)
