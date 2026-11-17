#!/usr/bin/env python3
"""xgregorm: prevod scenara xgregorm_gen_scenario.py (ns-3 / VaN3Twin) na beh OMNeT++ / Veins.

Vytvori priecinok behu so subormi SUMO (rovnaka siet, auta aj RSU), launchd.xml pre veins_launchd (rovnaky seed
SUMO ako ns-3) a omnetpp.ini (include omnetpp_base.ini + RSU v suradniciach OMNeT, cas, warmup, vykon, velkost CAM).

  python3 export_scenario.py --scenario ../src/automotive/examples/xgregorm_VAL --out run_VAL \
          --sim-time 60 --warmup 20 --seed 10 --tx-power 30 --rx-min -88 --cam-bytes 160
"""
import argparse, os, re, shutil

ap = argparse.ArgumentParser()
ap.add_argument("--scenario", required=True, help="priecinok scenara z xgregorm_gen_scenario.py")
ap.add_argument("--out", required=True)
ap.add_argument("--sim-time", type=float, default=60)
ap.add_argument("--warmup", type=float, default=0)
ap.add_argument("--seed", type=int, default=10, help="seed SUMO (ns-3: --seed, bez neho 10)")
ap.add_argument("--tx-power", type=float, default=30, help="vysielaci vykon [dBm] (ns-3 --tx-power)")
ap.add_argument("--rx-min", type=float, default=-88, help="citlivost [dBm] (ns-3 --wifi-rx-min)")
ap.add_argument("--cam-bytes", type=int, default=160, help="ramec CAM bez podpisu [B] (z ns-3 WIFIFRAMES)")
ap.add_argument("--margin", type=float, default=25, help="okraj Veins (TraCIScenarioManager.margin)")
ap.add_argument("--channel", default="authors", choices=["authors", "urban"],
                help="authors = LogDistance (config.xml); urban = 3GPP TR 37.885 V2V Urban s budovami (XgV2vUrban)")
a = ap.parse_args()

sc = os.path.abspath(a.scenario)
out = os.path.abspath(a.out)
os.makedirs(out, exist_ok=True)
cfg = [f for f in os.listdir(sc) if f.endswith(".sumo.cfg")][0]
txt = open(os.path.join(sc, cfg)).read()
files = re.findall(r'<(?:net-file|route-files|additional-files) value="([^"]+)"', txt)
names = []
for v in files:
    for f in v.split(","):
        f = f.strip()
        if f:
            shutil.copy(os.path.join(sc, f), out)
            names.append(f)
shutil.copy(os.path.join(sc, cfg), out)

# siet: hranice ako TraCI getNetBoundary (convBoundary) -> suradnice OMNeT (Veins TraCICoordinateTransformation)
net = [f for f in names if f.endswith(".net.xml")][0]
m = re.search(r'convBoundary="([^"]+)"', open(os.path.join(out, net)).read())
x0, y0, x1, y1 = map(float, m.group(1).split(","))
rsu = re.findall(r'<poi id="poi_(\d+)"[^>]*x="([-0-9.]+)" y="([-0-9.]+)"', open(os.path.join(sc, "stations.xml")).read())
rsu.sort(key=lambda r: int(r[0]))

with open(os.path.join(out, "launchd.xml"), "w") as f:
    f.write('<?xml version="1.0"?>\n<launch>\n')
    for n in names:
        f.write(f'    <copy file="{n}" />\n')
    f.write(f'    <copy file="{cfg}" type="config" />\n    <seed value="{a.seed}" />\n    <basedir path="{out}/" />\n</launch>\n')

# kanal: authors = spolocny config.xml; urban = budovy prepocitane do suradnic OMNeT++ + channel.xml
chan = "../config.xml"
if a.channel == "urban":
    bf = os.path.join(sc, "xgregorm_buildings.txt")
    with open(bf) as fi, open(os.path.join(out, "buildings_omnet.txt"), "w") as fo:
        for line in fi:
            bx0, bx1, by0, by1 = map(float, line.split()[:4])
            fo.write(f"{bx0 - x0 + a.margin:.2f} {bx1 - x0 + a.margin:.2f} {y1 - by1 + a.margin:.2f} {y1 - by0 + a.margin:.2f}\n")
    with open(os.path.join(out, "channel.xml"), "w") as f:
        f.write(f"""<?xml version="1.0" encoding="UTF-8"?>
<!-- xgregorm: 3GPP TR 37.885 V2V Urban ako channel urban v ns-3 (XgV2vUrban) -->
<root>
    <AnalogueModels>
        <AnalogueModel type="XgV2vUrban">
            <parameter name="buildingsFile" type="string" value="{os.path.join(out, 'buildings_omnet.txt')}"/>
            <parameter name="frequencyGHz" type="double" value="5.9"/>
            <parameter name="fading" type="bool" value="true"/>
        </AnalogueModel>
    </AnalogueModels>
    <Decider type="Decider80211p">
        <parameter name="centerFrequency" type="double" value="5.890e9"/>
    </Decider>
</root>
""")
    chan = "channel.xml"
mw = 10 ** (a.tx_power / 10)
with open(os.path.join(out, "omnetpp.ini"), "w") as f:
    f.write(f"""# xgregorm: vygenerovane z {a.scenario} (export_scenario.py)
include ../omnetpp_base.ini

[General]
sim-time-limit = {a.sim_time}s
*.playgroundSizeX = {x1 - x0 + 2 * a.margin + 100:.0f}m
*.playgroundSizeY = {y1 - y0 + 2 * a.margin + 100:.0f}m
*.playgroundSizeZ = 50m
*.manager.margin = {a.margin}
*.manager.launchConfig = xmldoc("launchd.xml")
*.**.nic.mac1609_4.txPower = {mw:.3f}mW
*.**.nic.phy80211p.minPowerLevel = {a.rx_min}dBm
*.**.appl.camBytes = {a.cam_bytes}
*.**.appl.warmup = {a.warmup}s
*.numRsu = {len(rsu)}
*.**.nic.phy80211p.decider = xmldoc("{chan}")
*.**.nic.phy80211p.analogueModels = xmldoc("{chan}")
""")
    for i, (pid, x, y) in enumerate(rsu):
        # Veins: x_omnet = x - x0 + margin, y_omnet = (y1 - y) + margin
        f.write(f"*.rsu[{i}].mobility.x = {float(x) - x0 + a.margin:.2f}\n*.rsu[{i}].mobility.y = {y1 - float(y) + a.margin:.2f}\n"
                f"*.rsu[{i}].mobility.z = 5\n")
print(f"{out}: {len(names)} SUMO files, {len(rsu)} RSU, seed {a.seed}")
