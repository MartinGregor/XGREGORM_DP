#!/usr/bin/env python3
# xgregorm: prevzatie publikovaneho scenara SUMO (napr. Bologna Acosta, Bieker a kol. 2015, iTETRIS) do tvaru,
# ktory pozaduje VaN3Twin a moj hybrid. Mapa, typy vozidiel, trasy, autobusy a programy semaforov ostavaju
# presne podla scenara. Meni sa len to, co VaN3Twin vyzaduje alebo co sa inak simulovat neda:
#   1. casove okno: ns-3 vytvori uzol pre kazde vozidlo v trase, cela hodina (8779 vozidiel) sa neda. Scenar sa
#      v SUMO prehra do casu --t0 (ustaleny stav), vozidla, ktore su v tom case v sieti, zacnu v case 0 presne
#      na svojom mieste (pruh, poloha, rychlost, zvysok trasy a zastavok); cakajuce na vlozenie a vozidla
#      s odchodom do --t0 + --window sa posunu o -t0,
#   2. ID vozidiel veh<cislo> (aplikacie autorov robia stol(id.substr(3))),
#   3. geo-referencia siete (CAM/DENM/GeoArea), rovnaka projekcia ako moj generator,
#   4. RSU na krizovatkach so semaforom (stations.xml), budovy pre --channel urban: vsetko mimo vozovky
#      a 4 m chodnika (scenar budovy nema; Bologna je v tejto oblasti suvisle zastavana).
# Pouzitie (z ns-3-dev):
#   python3 xgregorm_import_scenario.py --src /usr/local/share/sumo/tools/sumolib/scenario/scenarios/RealWorld/acosta \
#       --net acosta_buslanes.net.xml --routes acosta.rou.xml --add acosta_busses.add.xml \
#       --other acosta_vtypes.add.xml,acosta_bus_stops.add.xml,acosta_tls.add.xml --t0 1800 --window 90 \
#       --out xgregorm_scenare/acosta
# Potom: python3 xgregorm_gen_scenario.py --import-dir xgregorm_scenare/acosta --name acosta ... --run hybrid
import argparse, math, os, re, shutil, subprocess, sys, tempfile
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.join(os.environ.get("SUMO_HOME", "/usr/local/share/sumo"), "tools"))
import traci  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--src", required=True)
ap.add_argument("--net", required=True)
ap.add_argument("--routes", required=True, help="route files (comma separated) with ordinary vehicles")
ap.add_argument("--add", default="", help="additional files that contain vehicles (e.g. buses with stops)")
ap.add_argument("--other", default="", help="other additional files kept as they are (vTypes, bus stops, TLS programs)")
ap.add_argument("--t0", type=float, default=1800)
ap.add_argument("--window", type=float, default=90, help="keep departures up to t0 + window")
ap.add_argument("--seed", type=int, default=23423, help="SUMO seed for the replay up to t0 (SUMO default)")
ap.add_argument("--out", required=True)
ap.add_argument("--cell", type=float, default=2.0, help="grid for buildings [m]")
a = ap.parse_args()

os.makedirs(a.out, exist_ok=True)
other = [f for f in a.other.split(",") if f]
veh_add = [f for f in a.add.split(",") if f]
routes = [f for f in a.routes.split(",") if f]
for f in [a.net] + other:
    shutil.copy(os.path.join(a.src, f), os.path.join(a.out, f))

# ---------------------------------------------------------------- 1) prehratie do t0 a stav vozidiel
cmd = ["sumo", "-n", os.path.join(a.src, a.net), "-r", ",".join(os.path.join(a.src, f) for f in routes),
       "-a", ",".join(os.path.join(a.src, f) for f in other + veh_add), "--seed", str(a.seed),
       "--no-step-log", "-W", "--begin", "0"]
traci.start(cmd)
traci.simulationStep(a.t0)
stav = []
for v in traci.vehicle.getIDList():
    road, lane = traci.vehicle.getRoadID(v), traci.vehicle.getLaneIndex(v)
    route, idx = list(traci.vehicle.getRoute(v)), traci.vehicle.getRouteIndex(v)
    pos, spd = traci.vehicle.getLanePosition(v), traci.vehicle.getSpeed(v)
    if road.startswith(":"):              # na krizovatke: zacne na zaciatku nasledujuceho useku
        idx += 1
        if idx >= len(route):
            continue
        road, pos, lane = route[idx], 0.0, None
    stops = []
    for s in traci.vehicle.getStops(v):
        if not s.stoppingPlaceID:
            continue
        if s.lane.rsplit("_", 1)[0] == road and s.endPos < pos + 0.05:
            if pos - s.endPos > 1.0:       # zastavka uz za autobusom
                continue
            pos = max(0.0, s.endPos - 0.1)  # autobus prave stoji na zastavke
        stops.append((s.stoppingPlaceID, s.duration))
    stav.append(dict(id=v, type=traci.vehicle.getTypeID(v), edges=route[idx:], pos=pos, lane=lane,
                     speed=spd, stops=stops))
cakajuce = set(traci.simulation.getPendingVehicles())
traci.close()

# ---------------------------------------------------------------- 2) nove trasy
def vozidla(subor):
    for v in ET.parse(os.path.join(a.src, subor)).getroot().iter("vehicle"):
        yield v


nove = []                                      # (cas odchodu, xml)
ids = {}


def nove_id(povodne):
    ids[povodne] = f"veh{len(ids)}"
    return ids[povodne]


def esc(s):
    return s.replace("&", "&amp;").replace('"', "&quot;").replace("<", "&lt;")


# na kazdom useku najprv predne auto: SUMO pri departSpeed="max" da kazdemu najvyssiu bezpecnu rychlost voci
# uz vlozenemu predchodcovi; v opacnom poradi zlyha vlozenie predneho a s nim aj zvysok useku
for s in sorted(stav, key=lambda s: (s["edges"][0], -s["pos"])):
    lane = f'departLane="{s["lane"]}"' if s["lane"] is not None else 'departLane="best"'
    st = "".join(f'\n        <stop busStop="{esc(b)}" duration="{d:g}"/>' for b, d in s["stops"])
    nove.append((0.0, f'    <vehicle id="{nove_id(s["id"])}" type="{esc(s["type"])}" depart="0" {lane} '
                      f'departPos="{s["pos"]:.2f}" departSpeed="max">\n'
                      f'        <route edges="{esc(" ".join(s["edges"]))}"/>{st}\n    </vehicle>'))
v_siti = {s["id"] for s in stav}
for subor in routes + veh_add:
    for v in vozidla(subor):
        vid, dep = v.get("id"), float(v.get("depart"))
        if vid in v_siti:
            continue
        if dep <= a.t0 and vid not in cakajuce:
            continue                           # uz odislo zo siete
        if dep > a.t0 + a.window:
            continue
        v.set("depart", f"{max(0.0, dep - a.t0):.2f}")
        v.set("id", nove_id(vid))
        nove.append((float(v.get("depart")), "    " + ET.tostring(v, encoding="unicode").strip()))
nove.sort(key=lambda x: x[0])                  # stabilne: poradie v case 0 ostane                  # SUMO chce trasy zoradene podla odchodu
with open(os.path.join(a.out, "routes.rou.xml"), "w") as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<routes>\n')
    for _, x in nove:
        f.write(x + "\n")
    f.write("</routes>\n")
with open(os.path.join(a.out, "id_map.txt"), "w") as f:
    for k, v in ids.items():
        f.write(f"{v} {k}\n")

# ---------------------------------------------------------------- 3) geo-referencia (ako generator)
net_f = os.path.join(a.out, a.net)
n = open(net_f).read()
if 'projParameter="!"' in n:
    n = re.sub(r'netOffset="[^"]*"', 'netOffset="-394175.96,-4989734.74"', n, count=1)
    n = re.sub(r'projParameter="!"',
               'projParameter="+proj=utm +zone=32 +ellps=WGS84 +datum=WGS84 +units=m +no_defs"', n, count=1)
    open(net_f, "w").write(n)

# ---------------------------------------------------------------- 4) RSU na krizovatkach so semaforom
root = ET.parse(net_f).getroot()
tl_ids = {t.get("id") for t in root.findall("tlLogic")}
rsu = []
for j in root.findall("junction"):
    if j.get("type") == "traffic_light" or j.get("id") in tl_ids:
        rsu.append((float(j.get("x")), float(j.get("y"))))
with open(os.path.join(a.out, "stations.xml"), "w") as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<additional>\n')
    for i, (x, y) in enumerate(rsu):
        f.write(f'    <!-- V2X:STATION --><poi id="poi_{i}" color="orange" layer="202.00" x="{x:.2f}" y="{y:.2f}"/>\n')
    f.write('</additional>\n')

# ---------------------------------------------------------------- 5) budovy: vsetko mimo vozovky + 4 m chodnika
xmin, ymin, xmax, ymax = map(float, root.find("location").get("convBoundary").split(","))
c = a.cell
nx, ny = int((xmax - xmin) / c) + 1, int((ymax - ymin) / c) + 1
cesta = [bytearray(nx) for _ in range(ny)]
for e in root.findall("edge"):
    for l in e.findall("lane"):
        w = float(l.get("width", 3.2)) / 2 + 4.0
        p = [tuple(map(float, q.split(","))) for q in l.get("shape").split()]
        for (x1, y1), (x2, y2) in zip(p, p[1:]):
            d = math.hypot(x2 - x1, y2 - y1)
            k = max(1, int(d / (c / 2)))
            for t in range(k + 1):
                x, y = x1 + (x2 - x1) * t / k, y1 + (y2 - y1) * t / k
                for iy in range(max(0, int((y - w - ymin) / c)), min(ny, int((y + w - ymin) / c) + 1)):
                    for ix in range(max(0, int((x - w - xmin) / c)), min(nx, int((x + w - xmin) / c) + 1)):
                        cx, cy = xmin + (ix + 0.5) * c, ymin + (iy + 0.5) * c
                        if (cx - x) ** 2 + (cy - y) ** 2 <= w * w:
                            cesta[iy][ix] = 1
for j in root.findall("junction"):          # plocha krizovatiek
    sh = j.get("shape")
    if not sh:
        continue
    p = [tuple(map(float, q.split(","))) for q in sh.split()]
    jx = [q[0] for q in p]; jy = [q[1] for q in p]
    for iy in range(max(0, int((min(jy) - 4 - ymin) / c)), min(ny, int((max(jy) + 4 - ymin) / c) + 1)):
        for ix in range(max(0, int((min(jx) - 4 - xmin) / c)), min(nx, int((max(jx) + 4 - xmin) / c) + 1)):
            cesta[iy][ix] = 1
# pasy po 8 m (ako generator), v pase budova tam, kde je cely stlpec pasu mimo cesty
pas = max(1, int(8 / c))
kvadre = []
for k, iy0 in enumerate(range(0, ny, pas)):
    iy1 = min(ny, iy0 + pas)
    volne = [all(cesta[iy][ix] == 0 for iy in range(iy0, iy1)) for ix in range(nx)]
    ix = 0
    while ix < nx:
        if volne[ix]:
            s = ix
            while ix < nx and volne[ix]:
                ix += 1
            if (ix - s) * c >= 5:
                x0, x1 = xmin + s * c, xmin + ix * c
                vyska = 15 + (len(kvadre) * 7) % 16
                kvadre.append((x0, x1, ymin + iy0 * c, ymin + iy1 * c, vyska))
        else:
            ix += 1
with open(os.path.join(a.out, "buildings.txt"), "w") as f:
    for b in kvadre:
        f.write(" ".join(f"{v:.2f}" for v in b) + "\n")
with open(os.path.join(a.out, "buildings.poly.xml"), "w") as f:
    f.write('<?xml version="1.0" encoding="UTF-8"?>\n<additional>\n')
    for i, (x0, x1, y0, y1, h) in enumerate(kvadre):
        f.write(f'    <poly id="bud{i}" type="building" color="180,170,160" fill="1" layer="-1" '
                f'shape="{x0:.2f},{y0:.2f} {x1:.2f},{y0:.2f} {x1:.2f},{y1:.2f} {x0:.2f},{y1:.2f}"/>\n')
    f.write('</additional>\n')
with open(os.path.join(a.out, "import.txt"), "w") as f:
    f.write(f"net={a.net}\nadditional={','.join(other)}\nt0={a.t0}\nwindow={a.window}\n")
print(f"{a.out}: {len(stav)} vehicles in the network at t0={a.t0:g} s, {len(cakajuce)} waiting for insertion, "
      f"{len(nove)} vehicles in total (departures up to {a.window:g} s), {len(rsu)} RSU, {len(kvadre)} buildings")
