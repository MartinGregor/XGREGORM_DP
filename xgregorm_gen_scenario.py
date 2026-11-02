#!/usr/bin/env python3
"""
xgregorm_gen_scenario.py - generates a VaN3Twin scenario and optionally runs the simulation
with a final summary. The VaN3Twin source code is NOT modified, the original applications are used.

Maps (--map):
default   3x3 city intersections (50 km/h, 1 lane, 200 m apart -> 400x400 m town centre)
          + circular bypass (90 km/h, 2 lanes, 250 m from the city edge, radius 450 m)
          connected to the middle of each side of the city by 70 km/h roads.
          - traffic lights are given ONLY to 4-arm intersections (the authors' trafficManager
            indexes exactly 4 approaches and reads out of range on a 3-arm intersection),
          - the bypass gates use "right_before_left", otherwise vehicles coming from the city
            wait up to 40 s to merge into the dense bypass traffic.
city-center  9 signalised 4-arm intersections, 3 lanes per direction (right lane right + straight, middle
          straight, left lane left only, permissive left turns, 4-phase programs for trafficManager), irregular
          blocks 171-286 m (not a chessboard), outer streets continue 200 m out to a one-way ring road; a vehicle
          leaving the centre drives one ring segment to the neighbouring entry and comes back (600 vehicles:
          rush hour, 0 teleports, 10-23 vehicles queued on the busy approaches on average, 30-40 at peaks).
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
ap.add_argument("--map", default="default", choices=["default", "city-center"],
                help="default = 3x3 town (5 traffic lights, 1 lane) + bypass; city-center = 9 traffic lights, "
                     "3 lanes per direction, irregular blocks 170-285 m, ring road around")
ap.add_argument("--name", default=None,
                help="scenario name -> src/automotive/examples/xgregorm_<name>/ (default: mesto / city_center)")
# map
ap.add_argument("--grid", type=int, default=3, help="number of intersections per row")
ap.add_argument("--step", type=float, default=200,
                help="distance between intersections [m]; 150-250 m is a typical urban block between "
                     "signalised junctions; with the default 802.11p channel (measured: 100 %% delivery up to ~130 m, "
                     "nothing beyond ~145 m at 30 dBm) the coverage of neighbouring RSUs does not overlap")
ap.add_argument("--odstup", type=float, default=None,
                help="distance of the bypass / ring road from the city edge [m] (default 250 / city-center 200)")
ap.add_argument("--v-mesto", type=float, default=13.89, help="city speed [m/s] (13.89 = 50 km/h)")
ap.add_argument("--v-obchvat", type=float, default=None,
                help="bypass / ring road speed [m/s] (default 25 = 90 km/h / city-center 13.89 = 50 km/h)")
ap.add_argument("--v-privod", type=float, default=19.44,
                help="speed on the roads joining the city to the bypass [m/s] (19.44 = 70 km/h)")
ap.add_argument("--lanes-mesto", type=int, default=None, help="city lanes per direction (default 1 / city-center 3)")
ap.add_argument("--lanes-obchvat", type=int, default=None,
                help="bypass / ring road lanes per direction (default 2 / city-center 3)")
ap.add_argument("--zelena", type=float, default=30, help="green time of the fixed traffic light program [s]")
ap.add_argument("--zlta", type=float, default=3, help="yellow time [s]")
ap.add_argument("--kruh", type=int, default=10,
                help="number of geometry points per 45 deg bypass segment; higher = smoother circle")
ap.add_argument("--brany", default=None,
                choices=["right_before_left", "priority", "allway_stop", "zipper"],
                help="junction type where the city connects to the bypass / ring road "
                     "(default right_before_left / city-center priority)")
# traffic
ap.add_argument("--vehicles", type=int, default=None,
                help="number of vehicles, all created at t=0 (default 10 / city-center 600)")
ap.add_argument("--emergency", type=float, default=0.1, help="share of ambulances 0..1 (0.2 = every 5th)")
ap.add_argument("--repeat", type=int, default=100, help="how many times a vehicle repeats its loop")
ap.add_argument("--rsu", type=int, default=None, help="number of RSUs (default 5 / city-center 9)")
# simulation
ap.add_argument("--sim-time", type=float, default=120, help="simulation duration [s]")
ap.add_argument("--baseline", type=float, default=100, help="reference distance for the PRR computation [m]")
ap.add_argument("--tx-power", type=float, default=None,
                help="transmission power [dBm] (802.11p only; default 30, --channel urban 23 = same as NR-V2X)")
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
# my CAM algorithm (trafficManager 802.11p and hybrid); 0 = authors' ETSI CAM generation
ap.add_argument("--xg-relax", type=int, default=0, choices=[0, 1],
                help="relevance-based CAM rate: full ETSI rate only near the next traffic light, 1 Hz elsewhere")
ap.add_argument("--xg-relevance", type=float, default=250,
                help="relevance-based CAM rate: full-rate distance to the next traffic light [m]")
ap.add_argument("--beacon-ms", type=int, default=200, help="hybrid adaptive: period of the RSU feedback beacon [ms]")
# hybrid 802.11p + LTE + NR-V2X (program "hybrid" = xgregorm-hybrid)
ap.add_argument("--tech", default="80211p,lte,nr",
                help="hybrid: available technologies, comma separated subset of 80211p,lte,nr")
ap.add_argument("--policy", default="adaptive", choices=["adaptive", "all"],
                help="hybrid: adaptive = direct technology + backup only when needed; all = every CAM on all technologies")
ap.add_argument("--fail-80211p", default="", help="hybrid: 802.11p outage during the run, 'from:to' [s] ('60' = until the end)")
ap.add_argument("--fail-lte", default="", help="hybrid: LTE outage during the run, 'from:to' [s]")
ap.add_argument("--fail-nr", default="", help="hybrid: NR-V2X outage during the run, 'from:to' [s]")
ap.add_argument("--hyb-range", type=float, default=250,
                help="hybrid adaptive: distance to the next light where its controller needs the vehicle [m]")
ap.add_argument("--hyb-qmin", type=float, default=0.9,
                help="hybrid adaptive: lowest share of CAMs confirmed by the RSU to rely on one direct technology")
ap.add_argument("--hyb-dwell", type=float, default=1.0,
                help="hybrid adaptive: the direct link must be good this long before the backup is switched off [s]")
ap.add_argument("--load-rat", type=int, default=0, choices=[0, 1],
                help="hybrid adaptive: choose the main direct technology by load - 802.11p, at CBR >= --cbr-high part of "
                     "the vehicles move to NR-V2X (back below --cbr-low), LTE backup only below --lte-high PRB usage")
ap.add_argument("--cbr-high", type=float, default=0.3, help="load rule: congested CBR (ETSI DCC 0.3)")
ap.add_argument("--cbr-low", type=float, default=0.2, help="load rule: CBR of 802.11p to return from NR-V2X")
ap.add_argument("--lte-high", type=float, default=0.6, help="load rule: LTE UL PRB usage above which LTE is no backup")
ap.add_argument("--qos-rat", type=int, default=0, choices=[0, 1],
                help="hybrid adaptive: choose the main direct technology by the measured reliability (neighbours' CAMs "
                     "within 100 m delivered within --lat-budget, per technology); takes precedence over --load-rat")
ap.add_argument("--qos-margin", type=float, default=0.05, help="quality rule: required reliability advantage")
ap.add_argument("--lat-budget", type=float, default=0.1, help="quality rule: CAM latency budget [s]")
ap.add_argument("--pred-rat", type=int, default=0, choices=[0, 1],
                help="hybrid adaptive: quality rule with prediction - reliability and load over the next --pred-horizon s "
                     "from a shared quality map (measurements of all vehicles) along the vehicle's route (implies --qos-rat)")
ap.add_argument("--pred-horizon", type=float, default=3, help="prediction horizon [s]")
ap.add_argument("--coex", type=int, default=0, choices=[0, 1],
                help="hybrid: mutual interference of 802.11p and NR-V2X in the 5.9 GHz band (802.11p on --coex-wifi-ch, "
                     "NR 40 MHz at 5890 MHz = co-channel); 802.11p defers to NR energy above -65 dBm (CCA-ED)")
ap.add_argument("--coex-aclr", type=float, default=30, help="coexistence: adjacent channel leakage ratio [dB]")
ap.add_argument("--coex-wifi-ch", type=int, default=180, help="coexistence: 802.11p channel (180 = 5900 MHz)")
ap.add_argument("--nr-freq", type=float, default=5.89e9, help="hybrid: NR-V2X sidelink centre frequency [Hz]")
ap.add_argument("--nr-bw", type=float, default=0, help="hybrid: NR-V2X sidelink bandwidth [MHz] (0 = 40, authors)")
ap.add_argument("--equip", default="",
                help="hybrid: equipment of the vehicles - shares of W (802.11p only), N (NR-V2X only), WN (both), NONE "
                     "(no V2X), e.g. W:0.4,N:0.2,WN:0.2,NONE:0.2; every vehicle has LTE; empty = all WN")
ap.add_argument("--phone-share", type=float, default=0,
                help="hybrid: share of vehicles with passengers' phones / infotainment on the LTE uplink (bursty ON/OFF); "
                     "CAMs over LTE then use a dedicated V2X bearer")
ap.add_argument("--phone-kbps", type=float, default=100, help="hybrid: mean uplink rate of the phones in one vehicle [kbit/s]")
ap.add_argument("--lte-bw", type=int, default=0, choices=[0, 6, 15, 25, 50, 75, 100],
                help="hybrid: LTE cell bandwidth [PRB] (25 = 5 MHz ns-3 default, 50 = 10 MHz, 100 = 20 MHz; 0 = default)")
ap.add_argument("--dcc", type=int, default=0, choices=[0, 1],
                help="hybrid: 802.11p reactive DCC (ETSI TS 102 687) - gatekeeper after every frame + T_GenCam_DCC / T_GenCpm")
ap.add_argument("--nr-cr", type=int, default=0, choices=[0, 1],
                help="hybrid: NR-V2X congestion control - CR limit by SL-CBR (ETSI TS 103 574)")
ap.add_argument("--lte-bg-ue", type=int, default=0,
                help="hybrid: other (non-V2X) LTE users in the cell - static UEs, each a group of phones with bursty uplink")
ap.add_argument("--lte-bg-kbps", type=float, default=500, help="hybrid: mean uplink rate of one background LTE UE [kbit/s]")
ap.add_argument("--fading", type=int, default=1, choices=[0, 1],
                help="hybrid, --channel urban: Nakagami-m fast fading on 802.11p (NR has 3GPP fading of its own)")
ap.add_argument("--nr-noise-figure", type=float, default=-1,
                help="hybrid: NR-V2X UE noise figure [dB]; -1 = 9 with --channel urban (3GPP TR 37.885), otherwise 5 (authors)")
ap.add_argument("--lte-iot", type=float, default=-1,
                help="hybrid: LTE uplink interference from neighbouring cells [dB] added to the eNB noise figure; "
                     "-1 = 3 with --channel urban, otherwise 0")
ap.add_argument("--warmup", type=float, default=0,
                help="hybrid: statistics (network and traffic) start at this time [s]; all vehicles depart standing "
                     "at t = 0, the first ~30 s on city-center are the start-up (e.g. --warmup 30 --sim-time 60)")
ap.add_argument("--channel", default="authors", choices=["authors", "3gpp", "urban"],
                help="hybrid: radio channel of 802.11p and NR-V2X; authors = 802.11p LogDistance (authors' 802.11p "
                     "examples) and NR 3GPP V2V highway (authors' NR example), 3gpp = both on the same 3GPP TR 37.885 "
                     "V2V highway model, urban = buildings in the city blocks + 3GPP TR 37.885 V2V urban for 802.11p and "
                     "NR, 3GPP TR 38.901 UMa for LTE (sets --tx-power and --lte-ue-power to 23 dBm unless given)")
ap.add_argument("--security", type=int, default=0, choices=[0, 1, 2],
                help="hybrid: message signature ETSI TS 103 097; 1 = size model (+93 B digest / +242 B certificate once "
                     "per second, measured on the authors' implementation), 2 = real signing by the authors' GeoNet (slow)")
ap.add_argument("--spat", type=int, default=0, choices=[0, 1],
                help="hybrid: RSUs send SPATEM 10 Hz + MAPEM 1 Hz (ETSI TS 103 301) over their direct technologies")
ap.add_argument("--cpm-share", type=float, default=0,
                help="hybrid: share of vehicles with sensors sending CPM (ETSI TS 103 324 generation rules), 0..1")
ap.add_argument("--cpm-range", type=float, default=150, help="hybrid: sensor range of a CPM vehicle [m]")
ap.add_argument("--enb", type=int, default=1,
                help="hybrid: LTE macro cells; 1 = one cell in the middle, k*k = square grid with X2 handover "
                     "(one cell serves at most ~320 vehicles)")
ap.add_argument("--enb-isd", type=float, default=500, help="hybrid: LTE inter-site distance [m] (3GPP urban macro 500 m)")
ap.add_argument("--lte-ue-power", type=float, default=None,
                help="hybrid: LTE UE max tx power [dBm] (default 10 = ns-3; --channel urban 23 = real UE power class 3)")
ap.add_argument("--colorful", action="store_true",
                help="hybrid: colour vehicles in SUMO by the main technology of their CAMs: blue 802.11p, green NR-V2X, "
                     "red LTE (only when no direct technology), grey none; a backup copy on another technology is not shown; use with --gui")
ap.add_argument("--live", type=float, default=1,
                help="hybrid: every X s during the run print the share of vehicles per main technology and the load of "
                     "each technology (0 = off)")
ap.add_argument("--load-stats", type=int, default=1, choices=[0, 1],
                help="hybrid: measure the load of every technology (802.11p CBR per ETSI TS 102 687, LTE uplink PRB "
                     "usage per 3GPP TS 36.314, NR SL-CBR = ESTIMATE from received PSCCH) and the performance per load level")
ap.add_argument("--range-stats", type=int, default=0, choices=[0, 1],
                help="hybrid: measure delivery vs. distance (50 m bins up to 1000 m), car->car and car->RSU")
ap.add_argument("--nr-sensing", type=int, default=0, choices=[0, 1],
                help="hybrid: NR-V2X mode 2 sensing-based resource selection (authors: 0)")
ap.add_argument("--core-delay", type=float, default=5, help="hybrid: one-way delay PGW -> server [ms]")
ap.add_argument("--seed", type=int, default=None,
                help="random seed: ns-3 --RngRun for all programs, SUMO seed only for v2i-traffic-80211p "
                     "(--sumo-seed); not set = defaults (SUMO seed 10), identical to the reference results")
ap.add_argument("--run", default="nic",
                choices=["nic", "v2i-speed-80211p", "v2i-speed-lte", "v2i-traffic-80211p", "v2i-traffic-lte",
                         "v2v-80211p", "hybrid", "all"],
                help="which simulation to run after generating the scenario")
ap.add_argument("--gui", action="store_true", help="enable the SUMO GUI")
ap.add_argument("--diag", action="store_true",
                help="before ns-3, run plain SUMO and report how smoothly the traffic flows")
a = ap.parse_args()
# xgregorm: predvolene hodnoty podla mapy (default = povodna mapa xgregorm_mesto, bez zmeny)
for k, d, c in (("name", "mesto", "city_center"), ("odstup", 250, 200), ("v_obchvat", 25.0, 13.89),
                ("lanes_mesto", 1, 3), ("lanes_obchvat", 2, 3), ("brany", "right_before_left", "priority"), ("vehicles", 10, 600), ("rsu", 5, 9)):
    if getattr(a, k) is None:
        setattr(a, k, d if a.map == "default" else c)
# xgregorm: --channel urban = realisticke vykony (802.11p ako NR 23 dBm, LTE UE trieda 3 23 dBm), inak ako doteraz
if a.tx_power is None:
    a.tx_power = 23 if a.channel == "urban" else 30
if a.lte_ue_power is None:
    a.lte_ue_power = 23 if a.channel == "urban" else 10
if a.map == "city-center" and a.grid != 3:
    ap.error("--grid applies only to --map default (city-center has a fixed 3x3 layout)")
TECH = [t.strip().lower() for t in a.tech.split(",") if t.strip()]
if not TECH or any(t not in ("80211p", "lte", "nr") for t in TECH):
    ap.error("--tech must be a comma separated subset of 80211p,lte,nr")

if not os.path.isfile("./ns3"):
    sys.exit("Error: run this from the ns-3-dev folder (next to the ./ns3 file)")

out = os.path.join("src/automotive/examples", f"{P}{a.name}")
os.makedirs(out, exist_ok=True)
nod_f, edg_f = os.path.join(out, f"{P}map.nod.xml"), os.path.join(out, f"{P}map.edg.xml")
net_f, rou_f = os.path.join(out, f"{P}map.net.xml"), os.path.join(out, f"{P}cars.rou.xml")
cfg_f = os.path.join(out, f"{P}map.sumo.cfg")
st_f = os.path.join(out, "stations.xml")  # fixed name, VaN3Twin has it hardcoded

# =============================================================== 1) ROAD NETWORK
nodes, edges = [], []


def edge(x, y, lanes, speed, prio, shape_ab="", shape_ba=""):
    edges.append((x, y, lanes, speed, prio, shape_ab))
    edges.append((y, x, lanes, speed, prio, shape_ba))


# xgregorm: --map city-center = centrum mesta, 9 krizovatiek so semaforom, 3 pruhy v kazdom smere.
# Nie sachovnica: bloky 170-285 m, ulice nie su presne rovnobezne (posunute krizovatky), ako v starsej
# zastavbe. Kazda krizovatka ma 4 ramena (trafficManager zvlada len 4): krajne ulice pokracuju o --odstup
# von a napajaju sa na okruh (zvysok mesta), po ktorom sa auta vracaju - pocet aut v sieti ostava stale.
CC_XY = {(0, 0): (0, 0), (1, 0): (270, 15), (2, 0): (520, -10),
         (0, 1): (-20, 175), (1, 1): (255, 185), (2, 1): (540, 160),
         (0, 2): (10, 390), (1, 2): (285, 370), (2, 2): (530, 400)}


def city_center():
    global MID, n_tls, cc_brany, cc_okruhy, cc_spoje
    cc_spoje = []
    cx = sum(x for x, _ in CC_XY.values()) / 9
    cy = sum(y for _, y in CC_XY.values()) / 9
    MID = (cx, cy)
    for (i, j), (x, y) in CC_XY.items():
        nodes.append((f"C{i}{j}", x, y, "traffic_light"))
    n_tls = 9
    for i in range(3):
        for j in range(3):
            if i + 1 < 3: edge(f"C{i}{j}", f"C{i + 1}{j}", a.lanes_mesto, a.v_mesto, 3)
            if j + 1 < 3: edge(f"C{i}{j}", f"C{i}{j + 1}", a.lanes_mesto, a.v_mesto, 3)
    # vyjazdy z centra: krajna ulica pokracuje rovno von (smer od vnutorneho suseda)
    konce = []
    for (i, j) in CC_XY:
        for di, dj in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            if 0 <= i + di < 3 and 0 <= j + dj < 3:
                continue
            x, y = CC_XY[(i, j)]
            sx, sy = CC_XY[(i - di, j - dj)]
            d = math.hypot(x - sx, y - sy)
            ex, ey = x + (x - sx) / d * a.odstup, y + (y - sy) / d * a.odstup
            konce.append((math.atan2(ey - cy, ex - cx), f"C{i}{j}", round(ex, 1), round(ey, 1)))
    konce.sort()
    cc_brany = []
    for k, (_, c, x, y) in enumerate(konce):
        nodes.append((f"B{k}", x, y, a.brany))
        edge(c, f"B{k}", a.lanes_mesto, a.v_mesto, 3)
        cc_brany.append(c)
    nb = len(konce)
    # okruh je jednosmerny v smere hodinovych ruciciek: z vyjazdu sa nan odboci aj z neho zide vzdy doprava,
    # prudy sa na napojeniach nekrizuju (obojsmerny okruh s prednostou sprava sa pri 400+ autach zablokoval)
    for k in range(nb):
        edges.append((f"B{(k + 1) % nb}", f"B{k}", a.lanes_obchvat, a.v_obchvat, 4, ""))

    # okruhy: z kazdeho vjazdu do kazdeho ineho vyjazdu najkratsou cestou centrom (rovno, vlavo aj vpravo)
    def dlzka(p, q):
        return math.dist(CC_XY[(int(p[1]), int(p[2]))], CC_XY[(int(q[1]), int(q[2]))])

    def najkratsia(p, q):
        dist, prev, todo = {p: 0}, {}, {p}
        while todo:
            u = min(todo, key=dist.get)
            todo.remove(u)
            if u == q:
                break
            i, j = int(u[1]), int(u[2])
            for di, dj in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                if 0 <= i + di < 3 and 0 <= j + dj < 3:
                    v = f"C{i + di}{j + dj}"
                    if dist[u] + dlzka(u, v) < dist.get(v, 1e18):
                        dist[v], prev[v] = dist[u] + dlzka(u, v), u
                        todo.add(v)
        cesta = [q]
        while cesta[-1] != p:
            cesta.append(prev[cesta[-1]])
        cesta.reverse()
        return [E(x, y) for x, y in zip(cesta, cesta[1:])]

    # pruhy na krizovatkach: pravy = vpravo + rovno, stredne = rovno, lavy = len vlavo (bez neho lavo odbocujuci
    # cakajuci na medzeru v 3 protismernych pruhoch blokoval aj auta rovno -> od 700 aut zablokovanie siete)
    poz = {n: (x, y) for n, x, y, _ in nodes}
    for c in (f"C{i}{j}" for i, j in CC_XY):
        for u, c1, nl, *_ in edges:
            if c1 != c:
                continue
            for c2, w, nl2, *_ in edges:
                if c2 != c or w == u:
                    continue
                (ux, uy), (cx_, cy_), (wx, wy) = poz[u], poz[c], poz[w]
                uhol = math.degrees(math.atan2((cx_ - ux) * (wy - cy_) - (cy_ - uy) * (wx - cx_),
                                               (cx_ - ux) * (wx - cx_) + (cy_ - uy) * (wy - cy_)))
                if uhol > 30:       # vlavo
                    pary = [(nl - 1, nl2 - 1)] if nl > 1 else [(0, 0)]
                elif uhol < -30:    # vpravo
                    pary = [(0, 0)]
                else:               # rovno
                    pary = [(k, k) for k in range(max(1, nl - 1))]
                for fl, tl in pary:
                    cc_spoje.append((E(u, c), E(c, w), fl, tl))

    def cez(e, x):
        return ([E(f"B{e}", cc_brany[e])] + najkratsia(cc_brany[e], cc_brany[x]) + [E(cc_brany[x], f"B{x}")]
                + [E(f"B{x}", f"B{(x - 1) % nb}")])

    # okruh: vzdy len jeden usek k susednemu vjazdu (x -> x-1), nikto po nom nejde rovno -> zaradenie z vyjazdu
    # nema s kym kolidovat (pri jazde po okruhu dalej sa vyjazdy pri 400+ autach zablokovali, vedlajsia cesta
    # nedostala medzeru); okruh: vjazd e -> vyjazd x -> vjazd x-1 -> vyjazd e+1 -> vjazd e
    cc_okruhy = []
    for e in range(nb):
        for x in range(nb):
            if e == x or (x - 1) % nb == (e + 1) % nb:
                continue
            cc_okruhy.append(cez(e, x) + cez((x - 1) % nb, (e + 1) % nb))

if a.map == "default":
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
else:
    def E(x, y): return f"{x}_{y}"
    city_center()

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

con_f = os.path.join(out, f"{P}map.con.xml")
if a.map == "city-center":
    with open(con_f, "w") as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n<connections>\n')
        for fr, to, fl, tl in cc_spoje:
            f.write(f'    <connection from="{fr}" to="{to}" fromLane="{fl}" toLane="{tl}"/>\n')
        f.write('</connections>\n')
r = subprocess.run(["netconvert", "-n", nod_f, "-e", edg_f, "-o", net_f]
                   # city-center: bez chranenych faz pre lavy pruh (trafficManager cakava 4 fazy: 0 zelena S-J, 1 zlta,
                   # 2 zelena V-Z, 3 zlta), lave odbocenie s prednostou protismeru ako v beznom meste
                   + (["-x", con_f, "--tls.left-green.time", "0"] if a.map == "city-center" else []) + [
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

# xgregorm: trafficManager (802.11p aj LTE) ma natvrdo fazu 0 = zelena pre 1. a 3. privod v poradi riadenych
# pruhov (u autorov "sever-juh"), 2 = zelena pre 2. a 4. netconvert vsak da fazu 0 hlavnej ceste, takze na
# niektorych krizovatkach bola opacne: radic potom daval zelenu smeru podla zataze druheho smeru a pri
# zahlteni ju hned ukoncil -> rady sa nikdy nevyprazdnili (city-center 280 aut: po 120 s stoji 77 % aut).
# Fazy takej krizovatky posunieme o 2 (pevny program SUMO ostava rovnaky, len zacne druhym smerom).
_root = ET.parse(net_f).getroot()
_otocit = []
for _t in _root.findall("tlLogic"):
    _fazy = _t.findall("phase")
    if len(_fazy) != 4:
        continue
    _spoje = sorted((c for c in _root.findall("connection") if c.get("tl") == _t.get("id")),
                    key=lambda c: int(c.get("linkIndex")))
    _prvy = [int(c.get("linkIndex")) for c in _spoje if c.get("from") == _spoje[0].get("from")]
    if not any(_fazy[0].get("state")[i] in "Gg" for i in _prvy):
        _otocit.append(_t.get("id"))
if _otocit:
    _n = open(net_f).read()
    for _id in _otocit:
        def _rotuj(mm):
            riadky = re.findall(r"[ \t]*<phase [^>]*/>\n", mm.group(2))
            return mm.group(1) + "".join(riadky[2:] + riadky[:2]) + mm.group(3)
        _n = re.sub(rf'(<tlLogic id="{_id}"[^>]*>\n)((?:[ \t]*<phase [^>]*/>\n){{4}})([ \t]*</tlLogic>)', _rotuj, _n, count=1)
    open(net_f, "w").write(_n)


# ======================================================= 2) LOOPS AND VEHICLES
def E(x, y): return f"{x}_{y}"


if a.map == "default":
    okruhy = [[E(f"B{k}", f"B{(k + 1) % 8}") for k in range(8)],
              [E(f"B{(k + 1) % 8}", f"B{k}") for k in range(7, -1, -1)]]
    # block loops run clockwise = right turns only
    for i in range(a.grid - 1):
        for j in range(a.grid - 1):
            okruhy.append([E(f"C{i}{j}", f"C{i}{j + 1}"), E(f"C{i}{j + 1}", f"C{i + 1}{j + 1}"),
                           E(f"C{i + 1}{j + 1}", f"C{i + 1}{j}"), E(f"C{i + 1}{j}", f"C{i}{j}")])
    stred = f"C{m}{m}"


    def rovno(p, q):
        """edges straight through the city from junction p to q (same row or column); with --grid 3 just one edge"""
        (i1, j1), (i2, j2) = (int(p[1]), int(p[2])), (int(q[1]), int(q[2]))
        di, dj = (i2 > i1) - (i2 < i1), (j2 > j1) - (j2 < j1)
        body = [(i1 + k * di, j1 + k * dj) for k in range(max(abs(i2 - i1), abs(j2 - j1)) + 1)]
        return [E(f"C{a}{b}", f"C{c}{d}") for (a, b), (c, d) in zip(body, body[1:])]
    for c1, b1, b2, b3, c2 in [(f"C{g}{m}", "B0", "B1", "B2", f"C{m}{g}"),
                               (f"C{m}{g}", "B2", "B3", "B4", f"C0{m}"),
                               (f"C0{m}", "B4", "B5", "B6", f"C{m}0"),
                               (f"C{m}0", "B6", "B7", "B0", f"C{g}{m}")]:
        okruhy.append([E(c1, b1), E(b1, b2), E(b2, b3), E(b3, c2)] + rovno(c2, stred) + rovno(stred, c1))
else:
    okruhy = cc_okruhy

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
        # city-center: na okruh sa prvym krokom nezmesti 1000 aut na zaciatky hran -> nahodne volne miesto na hrane
        dep = ('departLane="best" departSpeed="max"' if a.map == "default"
               else 'departLane="free" departPos="random_free" departSpeed="0"')
        f.write(f'    <vehicle id="veh{k}" type="{typ}" depart="0" {dep}>\n')
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

# xgregorm: --channel urban - budovy v blokoch medzi ulicami (LOS/NLOS v ns-3, v SUMO len polygony na pohlad).
# Blok = mnohouholnik z krizovatiek okolo neho, zmenseny o polovicu sirky cesty + chodnik; ns-3 pozna len kvadre
# rovnobezne s osami, blok sa preto vyplni vodorovnymi pasmi po 8 m (na sikmych uliciach odchylka <= 4 m, do vozovky
# nezasahuje - chodnik ma 4 m).
bud_f = os.path.join(out, f"{P}buildings.txt")
n_bud = 0
if a.channel == "urban":
    poz = {n: (x, y) for n, x, y, _ in nodes}
    if a.map == "default":   # len bloky vnutri mesta, medzi mestom a obchvatom volny priestor (predmestie)
        bloky = [[f"C{i}{j}", f"C{i + 1}{j}", f"C{i + 1}{j + 1}", f"C{i}{j + 1}"]
                 for i in range(a.grid - 1) for j in range(a.grid - 1)]
    else:                    # 4 bloky v centre + useky medzi susednymi vyjazdmi a okruhom
        bloky = [[f"C{i}{j}", f"C{i + 1}{j}", f"C{i + 1}{j + 1}", f"C{i}{j + 1}"] for i in range(2) for j in range(2)]
        nb = len(cc_brany)
        for k in range(nb):
            ca, cb = cc_brany[k], cc_brany[(k + 1) % nb]
            bloky.append([ca, f"B{k}", f"B{(k + 1) % nb}"] + ([cb] if cb != ca else []))
    ulica = a.lanes_mesto * 3.2 + 4   # od osi cesty: pruhy jedneho smeru (3.2 m) + chodnik 4 m

    def zmensi(poly, d):
        """konvexny mnohouholnik posunuty o d dovnutra (priesecniky posunutych stran)"""
        if sum(x1 * y2 - x2 * y1 for (x1, y1), (x2, y2) in zip(poly, poly[1:] + poly[:1])) < 0:
            poly = poly[::-1]
        ciary = []
        for (x1, y1), (x2, y2) in zip(poly, poly[1:] + poly[:1]):
            l = math.hypot(x2 - x1, y2 - y1)
            nx, ny = -(y2 - y1) / l * d, (x2 - x1) / l * d
            ciary.append(((x1 + nx, y1 + ny), (x2 - x1, y2 - y1)))
        body = []
        for (p, u), (q, v) in zip(ciary[-1:] + ciary[:-1], ciary):
            det = u[0] * v[1] - u[1] * v[0]
            t = ((q[0] - p[0]) * v[1] - (q[1] - p[1]) * v[0]) / det
            body.append((p[0] + t * u[0], p[1] + t * u[1]))
        return body

    def interval(poly, y):
        xs = []
        for (x1, y1), (x2, y2) in zip(poly, poly[1:] + poly[:1]):
            if min(y1, y2) <= y <= max(y1, y2):
                xs += [x1, x2] if y1 == y2 else [x1 + (y - y1) * (x2 - x1) / (y2 - y1)]
        return (min(xs), max(xs)) if xs else None

    kvadre = []
    for k, blok in enumerate(bloky):
        poly = zmensi([poz[n] for n in blok], ulica)
        vyska = 15 + (k * 7) % 16       # 15-30 m (5-10 poschodi), deterministicky
        y, ymax = min(p[1] for p in poly), max(p[1] for p in poly)
        while ymax - y > 3:
            y2 = min(y + 8, ymax)
            iv = interval(poly, (y + y2) / 2)   # sirka v strede pasu: odchylka od okraja bloku <= 4 m (= chodnik)
            lo, hi = iv if iv else (0, 0)
            if hi - lo > 5:
                kvadre.append((lo, hi, y, y2, vyska))
            y = y2
    with open(bud_f, "w") as f:
        for b in kvadre:
            f.write(" ".join(f"{v:.2f}" for v in b) + "\n")
    with open(os.path.join(out, f"{P}buildings.poly.xml"), "w") as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n<additional>\n')
        for i, (x0, x1, y0, y1, h) in enumerate(kvadre):
            f.write(f'    <poly id="bud{i}" type="building" color="180,170,160" fill="1" layer="-1" '
                    f'shape="{x0:.2f},{y0:.2f} {x1:.2f},{y0:.2f} {x1:.2f},{y1:.2f} {x0:.2f},{y1:.2f}"/>\n')
        f.write('</additional>\n')
    additional.append(f"{P}buildings.poly.xml")
    n_bud = len(kvadre)

add_line =f'\n        <additional-files value="{",".join(additional)}"/>' if additional else ""

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
if a.map == "default":
    print(f"  map          : city {a.grid}x{a.grid} ({MAXC:.0f}x{MAXC:.0f} m, spacing {a.step:.0f} m), "
          f"bypass radius {R:.0f} m")
else:
    bloky = [math.dist(CC_XY[p], CC_XY[q]) for p in CC_XY for q in CC_XY
             if abs(p[0] - q[0]) + abs(p[1] - q[1]) == 1 and p < q]
    print(f"  map          : city center, 9 intersections, blocks {min(bloky):.0f}-{max(bloky):.0f} m, "
          f"{a.lanes_mesto} lanes per direction, ring road {a.odstup:.0f} m out ({a.lanes_obchvat} lanes, "
          f"{3.6 * a.v_obchvat:.0f} km/h)")
print(f"  traffic light: {n_tls} (4-arm intersections only), green {a.zelena:.0f} s, yellow {a.zlta:.0f} s")
print(f"  gates        : {a.brany} (city to bypass connection)")
print(f"  traffic      : {a.vehicles} vehicles ({n_amb} ambulances), {len(okruhy)} loops, all start at t=0")
print(f"  RSU          : {a.rsu}")
if n_bud:
    print(f"  buildings    : {n_bud} boxes in the city blocks (--channel urban)")
if a.map == "default" and a.step < 100:
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
    if r.returncode != 0:
        print("   SUMO failed:", (r.stdout + r.stderr)[-500:])
        return
    if not os.path.isfile(fcd):
        print("   SUMO failed:", (r.stdout + r.stderr)[-500:])
        return
    rychlosti, stoji, najdlhsie, hrana = {}, {}, {}, {}
    # xgregorm: rady pred semaformi = stojace auta na hranach, ktore vchadzaju do krizovatky so semaforom
    nroot = ET.parse(net_f).getroot()
    tls_j = {j.get("id") for j in nroot.findall("junction") if j.get("type") == "traffic_light"}
    prijazd = {e.get("id"): e.get("to") for e in nroot.findall("edge") if e.get("to") in tls_j}
    rad, rad_t, kroky, t_akt = {}, {}, 0, None
    def uzavri_krok():
        for e, n in rad_t.items():
            rad.setdefault(e, []).append(n)
    for _, el in ET.iterparse(fcd, events=("end",)):
        if el.tag == "timestep":
            uzavri_krok()
            rad_t = {e: 0 for e in prijazd}
            kroky += 1
            el.clear()
            continue
        if el.tag != "vehicle":
            continue
        i, v = el.get("id"), float(el.get("speed"))
        e = el.get("lane", "").rsplit("_", 1)[0]
        if v < 0.1 and e in rad_t:
            rad_t[e] += 1
        rychlosti.setdefault(i, []).append(v)
        if v < 0.1:
            stoji[i] = stoji.get(i, 0) + 1
            if stoji[i] > najdlhsie.get(i, 0):
                najdlhsie[i] = stoji[i]
                hrana[i] = el.get("lane")
        else:
            stoji[i] = 0
        el.clear()
    uzavri_krok()
    os.remove(fcd)
    if not rychlosti:
        print("   No vehicles?")
        return
    vsetky = [x for v in rychlosti.values() for x in v]
    if rad:
        # po 100 s (rozbeh), na prijazd: priemer a 95. percentil poctu stojacich aut, na krizovatku spolu
        zac = min(1000, len(next(iter(rad.values()))) // 2)
        per_e = {e: sorted(v[zac:]) for e, v in rad.items()}
        p95 = lambda v: v[int(0.95 * (len(v) - 1))] if v else 0
        print(f"   vehicles in the network: {len(rychlosti)} of {a.vehicles} inserted")
        print(f"   queues at traffic lights (standing vehicles per approach, all lanes, after {zac * 0.1:.0f} s):")
        for j in sorted(tls_j):
            ee = [e for e in prijazd if prijazd[e] == j]
            spolu = [sum(rad[e][t] for e in ee) for t in range(zac, len(rad[ee[0]]))]
            print(f"     {j}: per approach mean " + " / ".join(f"{sum(per_e[e]) / max(1, len(per_e[e])):4.1f}" for e in ee)
                  + "  p95 " + " / ".join(f"{p95(per_e[e]):2d}" for e in ee)
                  + f"  | whole junction mean {sum(spolu) / max(1, len(spolu)):5.1f}, max {max(spolu, default=0)}")
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
             f" --coord-lookahead={a.coord_lookahead}"
             f" --xg-relax={'true' if a.xg_relax else 'false'} --xg-relevance={a.xg_relevance}")
# SUMO gets its seed from the example on the command line (hardcoded 10 in the authors' examples, so
# a seed in .sumo.cfg is ignored); only v2i-trafficManager-80211p has the --sumo-seed option so far
if a.seed is not None:
    TM_80211P += f" --sumo-seed={a.seed}"   # local intersection control (xgregorm change in the authors' app)
LTE = f"--ns3::LteEnbRrc::SrsPeriodicity={a.srs}"     # otherwise ns-3 aborts with too many UEs
# my hybrid scenario (802.11p + LTE + NR-V2X, RSU controllers + server in the network); SRS 320 is set inside
HYB = (f"{TM} --tls-radius={a.tls_radius}"
       f" --xg-relax={'true' if a.xg_relax else 'false'} --xg-relevance={a.xg_relevance}"
       f" --beacon-ms={a.beacon_ms} --nr-sensing={'true' if a.nr_sensing else 'false'}"
       f" --range-stats={'true' if a.range_stats else 'false'} --channel={a.channel}"
       + (f" --buildings-file={bud_f}" if a.channel == "urban" else "")
       + f" --load-stats={'true' if a.load_stats else 'false'}"
       f" --color-tech={'true' if a.colorful else 'false'} --live-period={a.live}"
       f" --enb={a.enb} --enb-isd={a.enb_isd} --lte-ue-power={a.lte_ue_power}"
       + "".join(f" --use-{t}={'true' if t in TECH else 'false'}" for t in ("80211p", "lte", "nr"))
       + f" --policy={a.policy}"
       + "".join(f" --fail-{t}={v}" for t, v in (("80211p", a.fail_80211p), ("lte", a.fail_lte), ("nr", a.fail_nr)) if v)
       + f" --hyb-range={a.hyb_range} --hyb-qmin={a.hyb_qmin} --hyb-dwell={a.hyb_dwell}"
       f" --load-rat={'true' if a.load_rat else 'false'} --cbr-high={a.cbr_high} --cbr-low={a.cbr_low} --lte-high={a.lte_high}"
       f" --qos-rat={'true' if a.qos_rat else 'false'} --qos-margin={a.qos_margin} --lat-budget={a.lat_budget}"
       f" --lte-bg-ue={a.lte_bg_ue} --lte-bg-kbps={a.lte_bg_kbps}"
       + (f" --pred-rat=true --pred-horizon={a.pred_horizon}" if a.pred_rat else "")
       + (f" --coex=true --coex-aclr={a.coex_aclr} --coex-wifi-ch={a.coex_wifi_ch}" if a.coex else "")
       + (f" --nr-freq={a.nr_freq}" if a.nr_freq != 5.89e9 else "") + (f" --nr-bw={a.nr_bw}" if a.nr_bw else "")
       + (f" --equip={a.equip}" if a.equip else "")
       + (f" --phone-share={a.phone_share} --phone-kbps={a.phone_kbps}" if a.phone_share else "")
       + (f" --lte-bw={a.lte_bw}" if a.lte_bw else "") + (" --dcc=true" if a.dcc else "") + (" --nr-cr=true" if a.nr_cr else "") +
       f" --fading={'true' if a.fading else 'false'} --nr-noise-figure={a.nr_noise_figure} --lte-iot={a.lte_iot}"
       f" --warmup={a.warmup}"
       f" --core-delay={a.core_delay}" + (f" --sumo-seed={a.seed}" if a.seed is not None else "")
       # xgregorm: dalsia prevadzka; MAPEM podla poctu pruhov krizovatky (4 ramena x pruhy dnu a von:
       # 45 B na prijazdovy pruh s prepojeniami, 32 B na vyjazdovy, 30 B hlavicka), podpis podla merania (Dennik)
       + f" --security={a.security} --spat={'true' if a.spat else 'false'}"
       f" --mapem-bytes={30 + 4 * a.lanes_mesto * (45 + 32)} --cpm-share={a.cpm_share} --cpm-range={a.cpm_range}")
# per scenario name, so that runs with different --name can go in parallel
TMP = f"{P}tmp" if a.name == "mesto" else f"{P}tmp_{a.name}"

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
    "hybrid": ("xgregorm-hybrid", f"{ZAKLAD} {HYB} --tx-power={a.tx_power} --summary=true", False),
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
    hlavicka = False
    for riadok in proc.stdout:
        vystup.append(riadok)
        if riadok.startswith("XGREGORM-LIVE,"):
            # hybrid: priebezne - podiel aut podla hlavnej technologie a zataz kazdej technologie za poslednu periodu
            d = dict((k, float(v)) for k, v in re.findall(r"([A-Z_]+):([\d.eE+-]+)", riadok))
            if not hlavicka:
                print("  LIVE  time | veh | CONNECTED to infrastructure:   | main technology (sum 100 %):     | as backup:      |"
                      " load mean (max): 802.11p CBR    NR SL-CBR (est.)  LTE UL PRB")
                print("             |     | 802.11p    NR      LTE         | 802.11p   NR     LTE    none    |  NR     LTE     |")
                hlavicka = True
            print(f"  LIVE {d['T']:5.0f} s | {d['VEH']:3.0f} | {d['CONN_W']:5.1f} % {d['CONN_NR']:5.1f} % {d['CONN_LTE']:5.1f} %"
                  f"       | {d['W']:5.1f} % {d['NR']:5.1f} % {d['LTE']:5.1f} % {d['NONE']:5.1f} % |"
                  f" {d['BK_NR']:5.1f} % {d['BK_LTE']:5.1f} % |"
                  f"     {d['LOAD_W']:5.2f} % ({d['MAX_W']:5.2f})  {d['LOAD_NR']:5.2f} % ({d['MAX_NR']:5.2f})"
                  f"  {d['LOAD_LTE']:5.2f} % ({d['MAX_LTE']:5.2f})", flush=True)
            continue
        print(riadok, end="")
    proc.wait()
    suhrn(program, "".join(vystup), ma_csv, time.time() - t0, proc.returncode)
    shutil.rmtree(TMP, ignore_errors=True)


def doprava_z_netstate(cesta):
    """Traffic metrics from the SUMO netstate dump of the very same ns-3 run."""
    if not os.path.isfile(cesta):
        return {}, {}
    casy, sucet, vzorky, stoji_teraz, stoji_spolu, max_stoj = [], {}, {}, {}, {}, {}
    # xgregorm: --warmup (len hybrid) - doprava sa pocita az od tohto casu; vozidla prichadzaju pred timestep
    od = a.warmup if a.run == "hybrid" else 0
    for _, el in ET.iterparse(cesta, events=("start", "end")):
        if el.tag == "timestep":
            if _ == "start":
                t_teraz = float(el.get("time"))
                continue
            if len(casy) < 2:
                casy.append(t_teraz)
            el.clear()
            continue
        if el.tag != "vehicle" or _ != "end" or t_teraz < od:
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
    # my measurements (xgregorm-stats.h): what the RSUs know about the vehicles on their approaches
    aw = [dict((k, float(v)) for k, v in re.findall(r"([A-Z_0-9]+):([\d.eE+-]+)", r))
          for r in re.findall(r"XGREGORM-AW,(.*)", vystup)]
    xv = [dict((k, int(v)) for k, v in re.findall(r"([A-Z_]+):(\d+)", r))
          for r in re.findall(r"XGREGORM-VEH,(.*)", vystup)]
    if aw:
        S_ = lambda k: sum(r.get(k, 0) for r in aw)
        sm = max(S_("SAMPLES"), 1)
        known = max(sm - S_("UNKNOWN"), 1)
        ul = S_("UL_RX") / max(S_("UL_EXP"), 1)
        ulg = S_("ULG_RX") / max(S_("ULG_EXP"), 1)
        ul_all = (S_("UL_RX") + S_("ULG_RX")) / max(S_("UL_EXP") + S_("ULG_EXP"), 1)
        beac, bbytes = S_("BEACON_TX"), S_("BEACON_BYTES")
        cam_veh_s = cam_tx / max(len(info), 1) / max(a.sim_time, 1)
        v2v = re.search(r"XGREGORM-V2V,EXP:(\d+),RX:(\d+)", vystup)
        # 0 expected receivers (e.g. LTE only: no CAM goes to the neighbours at all) = not applicable, not 0
        v2v_prr = int(v2v.group(2)) / int(v2v.group(1)) if v2v and int(v2v.group(1)) > 0 else float("nan")
        prrv = re.search(r"XGREGORM-PRR,VEH:([\d.eE+-]+)", vystup)
        print("  RSU AWARENESS (vehicles on an approach of the RSU's light, sampled every 100 ms):")
        print(f"    samples                    : {sm:.0f}, RSU never heard the vehicle in {100 * S_('UNKNOWN') / sm:.2f} %")
        print(f"    RSU has a wrong edge       : {100 * S_('EDGE_WRONG') / sm:.2f} % of samples")
        print(f"    position error             : mean {S_('ERR_SUM') / known:.2f} m, > 5 m in "
              f"{100 * S_('GT5') / known:.2f} %, > 10 m in {100 * S_('GT10') / known:.2f} %")
        print(f"    true PRR car -> car (<100 m, SUMO positions): {v2v_prr:.5f}"
              f"{' (' + v2v.group(2) + '/' + v2v.group(1) + ')' if v2v else ''}")
        print("      (MetricSupervisor takes the sender position from GeoNet, refreshed only every 1 s,")
        print("       so its PRR is biased and the bias depends on the CAM rate - compare this one instead)")
        print(f"    uplink CAM -> RSU (<100 m) : {ul_all:.4f} (3 s after a green start {format(ulg, '.4f') if S_('ULG_EXP') else 'n/a'}, "
              f"otherwise {ul:.4f})")
        print(f"    CAM per vehicle per second : {cam_veh_s:.2f}")
        print(f"    RSU beacons                : {beac:.0f} ({beac / max(a.sim_time, 1):.1f}/s, "
              f"avg {bbytes / max(beac, 1):.0f} B)")
        if xv:
            T = lambda k: sum(r.get(k, 0) for r in xv)
            print(f"    relevance-based CAM rate   : {T('RELAXED')} CAMs suppressed outside the area, "
                  f"{T('FORCED')} sent at once on entering/leaving it")
        gaps = [dict((k, float(v)) for k, v in re.findall(r"([A-Z_0-9]+):([\d.eE+-]+)", r))
                for r in re.findall(r"XGREGORM-GAP,(.*)", vystup)]
        gaps = [g for g in gaps if g.get("MIN", -1) >= 0]
        if gaps:
            print(f"    ETSI EN 302 637-2 CAM gap  : min {min(g['MIN'] for g in gaps):.0f} ms, max "
                  f"{max(g['MAX'] for g in gaps):.0f} ms (allowed 100-1000), violations "
                  f"< 100 ms: {sum(g['LT100'] for g in gaps):.0f}, > 1000 ms: {sum(g['GT1000'] for g in gaps):.0f}")
        hyb = [dict((k, int(v)) for k, v in re.findall(r"([A-Z_0-9]+):(\d+)", r))
               for r in re.findall(r"XGREGORM-HYB,(.*)", vystup)]
        if hyb:
            H = lambda k: sum(r.get(k, 0) for r in hyb)
            # P<mask>: CAMs by the combination of technologies they really left on (1 802.11p, 2 LTE, 4 NR)
            ht = max(sum(H(f"P{m}") for m in range(8)), 1)
            on = lambda bit: sum(H(f"P{m}") for m in range(8) if m & bit)
            combo = lambda m: "+".join(n for b, n in ((1, "802.11p"), (2, "LTE"), (4, "NR")) if m & b) or "none (outage / LTE not needed)"
            relay = re.search(r"XGREGORM-RELAY,CAM:(\d+)", vystup)
            nrdrop = re.search(r"XGREGORM-NRDROP,N:(\d+)", vystup)
            lat = [1000 * S_(f"LAT_{k}_SUM") / max(S_(f"LAT_{k}_N"), 1) for k in ("W", "L", "N")]
            vl = dict((k, float(v)) for k, v in re.findall(r"([A-Z_]+):([\d.eE+-]+)",
                      (re.search(r"XGREGORM-V2VLAT,(.*)", vystup) or re.search("()", "")).group(1)))
            smf = max(S_("SAMPLES_F"), 1)
            knf = max(smf - S_("UNKNOWN_F"), 1)
            print(f"  HYBRID (technologies {'+'.join(TECH)}, policy {a.policy}"
                  + "".join(f", {t} outage {v} s" for t, v in (("802.11p", a.fail_80211p), ("LTE", a.fail_lte),
                                                              ("NR", a.fail_nr)) if v) + "):")
            print("    CAMs by path               : " + ", ".join(
                f"{combo(m)} {100 * H(f'P{m}') / ht:.1f} %" for m in range(8) if H(f"P{m}")))
            print(f"    CAMs per technology        : 802.11p {on(1)}, LTE {on(2)} ({on(2) / max(a.sim_time, 1):.1f}/s "
                  f"cellular uplink), NR {on(4)}; path switches {H('SWITCH')}, probes of 802.11p {H('PROBE')}, "
                  f"moves of the main technology due to load {H('LOADSW')}, due to quality {H('QOSSW')}, "
                  f"by prediction {H('PREDSW')}")
            qe = [list(map(int, m)) for m in re.findall(r"QW:(\d+)/(\d+)/(\d+),QN:(\d+)/(\d+)/(\d+)", vystup)]
            if qe:
                # odhad vozidiel (z poradovych cisel) vs. skutocnost (PRR kopie po technologiach z polohy v SUMO)
                q = [sum(r[i] for r in qe) for i in range(6)]
                print(f"    quality estimate (vehicles): 802.11p received {q[1] / max(q[0], 1):.3f}, on time "
                      f"{q[2] / max(q[0], 1):.3f} ({q[0]} expected) | NR received {q[4] / max(q[3], 1):.3f}, "
                      f"on time {q[5] / max(q[3], 1):.3f} ({q[3]} expected) - compare with the PRR per technology below")
            print(f"    relayed by the server      : {relay.group(1) if relay else 'n/a'} CAMs to the RSUs over the backhaul")
            ltebg = re.search(r"XGREGORM-LTEBG,UE:(\d+),PKT:(\d+),BYTES:(\d+)", vystup)
            if ltebg:
                print(f"    other LTE users            : {ltebg.group(1)} UEs, {int(ltebg.group(3)) * 8 / 1e3 / max(a.sim_time, 1):.0f} kbit/s uplink in total")
            if nrdrop:
                print(f"    NR packets without a valid IP header dropped (5G-LENA, originally aborted the run): {nrdrop.group(1)}")
            print(f"    latency vehicle -> RSU     : 802.11p {lat[0]:.2f} ms ({S_('LAT_W_N'):.0f}), LTE + core + backhaul "
                  f"{lat[1]:.2f} ms ({S_('LAT_L_N'):.0f}, every RSU gets each relayed CAM), NR {lat[2]:.2f} ms ({S_('LAT_N_N'):.0f})")
            print(f"    latency vehicle -> vehicle : 802.11p {1000 * vl.get('W_SUM', 0) / max(vl.get('W_N', 0), 1):.2f} ms "
                  f"({vl.get('W_N', 0):.0f}), NR {1000 * vl.get('NR_SUM', 0) / max(vl.get('NR_N', 0), 1):.2f} ms "
                  f"({vl.get('NR_N', 0):.0f}) - first copy of each CAM")
            print(f"    RSU info age (<100 m / whole approach): {S_('AGE_SUM') / known:.3f} s / {S_('AGE_SUM_F') / knf:.3f} s")
            print(f"    whole approach             : unknown {100 * S_('UNKNOWN_F') / smf:.2f} %, error mean "
                  f"{S_('ERR_SUM_F') / knf:.2f} m, > 10 m in {100 * S_('GT10_F') / knf:.2f} %")
            print(f"    phantom vehicles           : {100 * S_('PHANTOM') / max(S_('BEL_ON'), 1):.2f} % of the vehicles a "
                  "controller counts on its approach are no longer there (last CAM > 2 s old)")
            print(f"    CAMs received by the RSUs  : 802.11p {S_('RX_W'):.0f}, LTE {S_('RX_L'):.0f}, NR {S_('RX_N'):.0f}; "
                  f"stale copies dropped {S_('STALE'):.0f}; beacons NR {S_('BEACON_NR_TX'):.0f}")
            print("      (MetricSupervisor is off in the hybrid scenario - it cannot count LTE/NR or duplicates)")
            tl = re.search(r"XGREGORM-HYBT,(.*)", vystup)
            lt = re.search(r"XGREGORM-LOSST,(.*)", vystup)
            if tl and lt:
                # timeline in 5 s windows: share of CAMs per technology + what was delivered
                paths, loss = {}, {}
                for sec, cnt in re.findall(r"T(\d+):([\d/]+)", tl.group(1)):
                    paths[int(sec)] = [int(x) for x in cnt.split("/")]
                for sec, cnt in re.findall(r"T(\d+):([\d/]+)", lt.group(1)):
                    loss[int(sec)] = [int(x) for x in cnt.split("/")]
                pct = lambda x, n: f"{100 * x / n:5.1f}" if n else "   - "
                print("    timeline (5 s): % of CAMs on 802.11p / LTE / NR / none | delivered: uplink to RSU <100 m, "
                      "car->car <100 m | RSU on the approach: does not know the car %, info age s")
                for k in range(0, max(list(paths) + list(loss) + [0]) // 5 + 1):
                    w = [sum(paths.get(t, [0] * 8)[m] for t in range(5 * k, 5 * k + 5)) for m in range(8)]
                    l = [sum(loss.get(t, [0] * 8)[m] for t in range(5 * k, 5 * k + 5)) for m in range(8)]
                    tot = sum(w)
                    pc = [pct(sum(w[m] for m in range(8) if m & b), tot) for b in (1, 2, 4)]
                    known_k = l[4] - l[5]
                    age = f"{l[6] / 1000 / known_k:5.2f}" if known_k else "   - "
                    print(f"      {5 * k:3d}-{5 * k + 5:3d} s: {pc[0]} /{pc[1]} /{pc[2]} /{pct(w[0], tot)} | "
                          f"{pct(l[1], l[0])} {pct(l[3], l[2])} | {pct(l[5], l[4])} {age}   ({tot} CAMs)")
                # single seconds with a loss burst (the 5 s average could hide it)
                bad = [t for t, l in sorted(loss.items())
                       if (l[0] >= 5 and l[1] < 0.9 * l[0]) or (l[2] >= 10 and l[3] < 0.9 * l[2])
                       or (l[4] - l[5] and l[6] / 1000 / (l[4] - l[5]) > 1.5)]
                print(f"    seconds with uplink or car->car < 90 % or RSU info older than 1.5 s: "
                      f"{', '.join(map(str, bad)) if bad else 'none'}")
            loads = {m.group(1): dict((k, float(v)) for k, v in re.findall(r"([A-Z_0-9]+):([\d.eE+-]+|nan)", m.group(2)))
                     for m in re.finditer(r"XGREGORM-LOAD,TECH:(\w+),(.*)", vystup)}
            if loads:
                nazov = {"W": "802.11p", "LTE": "LTE", "NR": "NR-V2X"}
                print("    LOAD per technology (one sample = one station (802.11p, NR) or one cell (LTE) per 100 ms):")
                print("      802.11p = CBR, ETSI TS 102 687 (share of time rx power > -85 dBm, own tx included) - MEASURED")
                print("      LTE     = uplink PRB usage of the cell, 3GPP TS 36.314 (granted PRB / available PRB) - MEASURED")
                print("      NR-V2X  = SL-CBR, 3GPP TS 38.215 - ESTIMATE from decoded PSCCH (lower bound, ns-3 has no S-RSSI)")
                print("      tech     samples   mean    p50    p95    p99    max   >=0.3 %  >=0.6 %")
                for t, d in loads.items():
                    n = max(d.get("N", 0), 1)
                    print(f"      {nazov[t]:<8} {d.get('N', 0):8.0f} {d.get('MEAN', 0):6.3f} {d.get('P50', 0):6.2f} "
                          f"{d.get('P95', 0):6.2f} {d.get('P99', 0):6.2f} {d.get('MAX', 0):6.3f} "
                          f"{100 * d.get('GE30', 0) / n:8.2f} {100 * d.get('GE60', 0) / n:8.2f}"
                          + (f"   ({d['CELLS']:.0f} cell(s), {d['ULRB']:.0f} UL PRB)" if t == "LTE" else "")
                          + (f"   ({d['SUBCH']:.0f} subchannels x {d['SLOTS_S']:.0f} slots/s)" if t == "NR" else ""))
                kose = ["<0.1", "0.1-0.2", "0.2-0.3", "0.3-0.6", ">=0.6"]
                print("    PERFORMANCE per load of the sender at tx time (802.11p / NR: car->car <100 m, every technology")
                print("    counted on its own; LTE: car->RSU, delivered = at least one RSU got it; latency per copy):")
                print("      tech     load      CAMs   expected  delivered   PRR      lat mean   p50     p95     p99     max [ms]")
                for m in re.finditer(r"XGREGORM-PERF,TECH:(\w+),(.*)", vystup):
                    d = dict((k, float(v)) for k, v in re.findall(r"([A-Z_0-9]+):([\d.eE+-]+|nan)", m.group(2)))
                    prr = d["RX"] / d["EXP"] if d["EXP"] else float("nan")
                    print(f"      {nazov[m.group(1)]:<8} {kose[int(d['BIN'])]:<8} {d['CAM']:7.0f} {d['EXP']:9.0f} {d['RX']:9.0f}   "
                          f"{prr:.4f}  {d['LAT_MEAN_MS']:8.2f} {d['LAT_P50_MS']:7.2f} {d['LAT_P95_MS']:7.2f} "
                          f"{d['LAT_P99_MS']:7.2f} {d['LAT_MAX_MS']:7.2f}")
                pr = re.search(r"XGREGORM-PIR,(.*)", vystup)
                if pr:
                    d = dict((k, float(v)) for k, v in re.findall(r"([A-Z_0-9]+):([\d.eE+-]+|nan)", pr.group(1)))
                    print(f"    PIR car->car <100 m (time between two received CAMs of the same sender, 3GPP TR 37.885): "
                          f"{d['N']:.0f} gaps, mean {d['MEAN_MS']:.0f} ms, p50 {d['P50_MS']:.0f}, p95 {d['P95_MS']:.0f}, "
                          f"p99 {d['P99_MS']:.0f}, max {d['MAX_MS']:.0f} ms")
            sw = re.search(r"XGREGORM-SWLOSS,(.*)", vystup)
            if sw:
                # xgregorm: car->car <100 m by the direct technologies of the CAM, who got it (first copy of each)
                c = {k: [int(x) for x in v.split("/")] for k, v in re.findall(r"(C\d|ALT\d):([\d/]+)", sw.group(1))}
                print("    car->car <100 m by the direct path of the CAM (each CAM x neighbour): got it via 802.11p only / "
                      "NR only / both / nobody")
                for k, nazov in (("C0", "802.11p only"), ("C1", "NR only"), ("C2", "802.11p + NR")):
                    v = c.get(k, [0, 0, 0, 0])
                    n = sum(v)
                    if n:
                        print(f"      sent {nazov:<13}: {n:8d}  {100 * v[1] / n:5.1f} / {100 * v[2] / n:5.1f} / "
                              f"{100 * v[3] / n:5.1f} / {100 * v[0] / n:5.1f} %   PRR {1 - v[0] / n:.4f}")
                v = c.get("C2", [0, 0, 0, 0])
                if sum(v):
                    print(f"      complementarity (CAMs sent on both): NR alone would lose {100 * v[1] / sum(v):.1f} %, "
                          f"802.11p alone would lose {100 * v[2] / sum(v):.1f} % of the receptions")
                for k, j, nazov, druha in (("C0", "ALT0", "802.11p only", "NR"), ("C1", "ALT1", "NR only", "802.11p")):
                    n, alt = c.get(k, [0])[0], c.get(j, [0, 0])
                    if n:
                        print(f"      lost when sent {nazov}: {n}, of which the neighbour got this sender over {druha} "
                              f"within 1 s / 3 s before: {100 * alt[0] / n:.1f} / {100 * alt[1] / n:.1f} %")
            cx = re.search(r"XGREGORM-COEX,TO_NR:(.*)", vystup)
            cf = re.search(r"XGREGORM-COEX,W_MHZ:([\d.]+)/([\d.]+),NR_MHZ:([\d.]+)/([\d.]+),OVERLAP_MHZ:([\d.]+),ACLR_DB:([\d.]+)", vystup)
            if cx:
                d = dict((k, float(v)) for k, v in re.findall(r"([A-Z_]+):([\d.eE+-]+)", "TO_NR:" + cx.group(1)))
                print(f"    coexistence 5.9 GHz        : 802.11p {cf.group(1)} MHz / {cf.group(2)} MHz, NR {cf.group(3)} MHz / "
                      f"{cf.group(4)} MHz, overlap {cf.group(5)} MHz, ACLR {cf.group(6)} dB (whole run)" if cf else
                      "    coexistence 5.9 GHz        :")
                print(f"      802.11p -> NR receivers  : {d['TO_NR']:.0f} signals (mean {d['TO_NR_DBM']:.1f} dBm), counted in "
                      f"the NR SL-CBR estimate {d['CBR_NR']:.0f}")
                print(f"      NR -> 802.11p receivers  : {d['TO_W']:.0f} signals (mean {d['TO_W_DBM']:.1f} dBm), CCA busy "
                      f"(802.11p defers) {d['CCA']:.0f}, counted in CBR {d['CBR_W']:.0f}")
            eqm = re.search(r"XGREGORM-EQUIP,W:(\d+),N:(\d+),WN:(\d+),NONE:(\d+)", vystup)
            if eqm:
                print(f"    equipment (vehicle pool)   : 802.11p only {eqm.group(1)}, NR only {eqm.group(2)}, both {eqm.group(3)}, "
                      f"no V2X {eqm.group(4)} (all with LTE)")
            ph = re.search(r"XGREGORM-PHONE,VEH:(\d+),PKT:(\d+),BYTES:(\d+)", vystup)
            if ph:
                print(f"    phones in vehicles         : {ph.group(1)} vehicles, {int(ph.group(3)) * 8 / 1e3 / max(a.sim_time, 1):.0f} "
                      f"kbit/s LTE uplink in total (whole run)")
            dc = re.search(r"XGREGORM-DCC,NOW:(.*)", vystup)
            if dc:
                d = dict((k, v) for k, v in re.findall(r"([A-Z_]+):([\d/.eE+-]+)", "NOW:" + dc.group(1)))
                f_ = lambda k: float(d.get(k, 0))
                st_ = [int(x) for x in d["STATE"].split("/")]
                tot = max(sum(st_), 1)
                n11 = f_("NOW") + f_("QUEUED") + f_("REPLACED")
                if n11:
                    print(f"    DCC 802.11p (whole run)    : messages sent at once {100 * f_('NOW') / n11:.1f} %, queued "
                          f"{100 * f_('QUEUED') / n11:.1f} % (mean wait {1000 * f_('QDELAY_S') / max(f_('FROMQ'), 1):.0f} ms), "
                          f"replaced by a newer one {100 * f_('REPLACED') / n11:.1f} %, expired {f_('EXPIRED'):.0f}; "
                          f"CAMs not generated (T_GenCam_DCC) {f_('CAMSKIP'):.0f}, CPMs {f_('CPMSKIP'):.0f}")
                    print("      state at a message    : " + ", ".join(
                        f"{n} {100 * v / tot:.1f} %" for n, v in zip(("relaxed", "active1", "active2", "active3", "restrictive"), st_)))
                nr = f_("NR_SENT") + f_("NR_DROP")
                if nr:
                    print(f"    NR CR limit (whole run)    : copies dropped {100 * f_('NR_DROP') / nr:.2f} % of {nr:.0f}, mean CR "
                          f"{f_('CR_SUM') / nr:.4f}")
            pd = re.search(r"XGREGORM-PRED,(.*)", vystup)
            if pd:
                d = dict((k, v) for k, v in re.findall(r"([A-Z0-9]+):([\d/.eE+-]+)", pd.group(1)))
                cov = [int(x) for x in d["COVER"].split("/")]
                print(f"    prediction                 : map cells {d['CELLS']}, predictions with map data "
                      f"{100 * cov[0] / max(sum(cov), 1):.1f} % ({sum(cov)}); reliability over the next {a.pred_horizon:g} s, "
                      f"mean |error| (bias) prediction vs. 'stays as now':")
                for k, nazov in ((0, "802.11p"), (1, "NR")):
                    n = int(d[f"N{k}"])
                    e = [float(x) for x in d[f"E{k}"].split("/")]
                    b = [float(x) for x in d[f"B{k}"].split("/")]
                    if n:
                        print(f"      {nazov:<8}: {e[0] / n:.4f} ({b[0] / n:+.4f}) vs. {e[1] / n:.4f} ({b[1] / n:+.4f})   ({n} predictions)")
            rg = re.search(r"XGREGORM-RANGE,(.*)", vystup)
            if rg:
                # delivery vs. distance: share of CAMs received (first copy) by vehicles / RSUs at that distance
                pct = lambda x, n: f"{100 * x / n:5.1f}" if n else "   - "
                print("    delivery vs. distance      : distance m | car->car % (expected) | car->RSU % (expected)")
                for b, cnt in re.findall(r"B(\d+):([\d/]+)", rg.group(1)):
                    e1, r1, e2, r2 = [int(x) for x in cnt.split("/")]
                    if e1 or e2:
                        print(f"      {50 * int(b):4d}-{50 * int(b) + 50:4d} m | {pct(r1, e1)} ({e1:7d}) | {pct(r2, e2)} ({e2:6d})")
            print("  XGCSV-HYB: tech,policy,seed,vehicles,cam_wifi,cam_lte,cam_nr,lte_cam_s,switches,lat_wifi_ms,"
                  "lat_lte_ms,lat_nr_ms,age_near_s,age_far_s,unknown_far_%,err_far_m,gt10_far_%,v2v_prr,"
                  "stop_max_s,stopped_%,speed_kmh")
            print(f"  XGCSV-HYB: {'+'.join(TECH)},{a.policy},{a.seed},{a.vehicles},{on(1)},{on(2)},{on(4)},"
                  f"{on(2) / max(a.sim_time, 1):.2f},{H('SWITCH')},{lat[0]:.3f},{lat[1]:.3f},{lat[2]:.3f},"
                  f"{S_('AGE_SUM') / known:.4f},{S_('AGE_SUM_F') / knf:.4f},"
                  f"{100 * S_('UNKNOWN_F') / smf:.3f},{S_('ERR_SUM_F') / knf:.3f},{100 * S_('GT10_F') / knf:.3f},"
                  f"{v2v_prr:.5f},{doprava_celkom.get('longest_stop', float('nan')):.1f},"
                  f"{doprava_celkom.get('stopped_pct', float('nan')):.1f},"
                  f"{3.6 * doprava_celkom.get('speed', float('nan')):.1f}")
        alg = "relax" if a.xg_relax else "none"
        print("  XGCSV: alg,seed,vehicles,PRR,latency_ms,cam_tx,cam_veh_s,beacons,beacon_B,frames_s,"
              "edge_wrong_%,err_mean_m,err_gt5_%,ul,ul_green,unknown_%,stop_max_s,stopped_%,speed_kmh,v2v_prr")
        print(f"  XGCSV: {alg},{a.seed},{a.vehicles},{prr_v:.6f},{lat_v:.4f},{cam_tx},{cam_veh_s:.3f},"
              f"{beac:.0f},{bbytes / max(beac, 1):.0f},{(cam_tx + beac) / max(a.sim_time, 1):.2f},"
              f"{100 * S_('EDGE_WRONG') / sm:.3f},{S_('ERR_SUM') / known:.3f},{100 * S_('GT5') / known:.3f},"
              f"{ul_all:.5f},{ulg:.5f},{100 * S_('UNKNOWN') / sm:.3f},"
              f"{doprava_celkom.get('longest_stop', float('nan')):.1f},"
              f"{doprava_celkom.get('stopped_pct', float('nan')):.1f},"
              f"{3.6 * doprava_celkom.get('speed', float('nan')):.1f},{v2v_prr:.5f}")
    if program in ("v2i-trafficManager-80211p", "xgregorm-hybrid"):
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
    # "all" = the authors' 5 simulations; my hybrid scenario only on request
    for k in ([k for k in PRIKAZY if k != "hybrid"] if a.run == "all" else [a.run]):
        spusti(k)
