/* xgregorm: hybridny scenar trafficManager - kazde auto (a RSU) moze mat 802.11p, LTE aj NR-V2X a pre kazdu
 * CAM sa rozhodne, ktorou technologiou (alebo ktorymi) ide. Postavene na v2i-trafficManager-80211p.cc,
 * v2i-trafficManager-LTE.cc (EPC, server v sieti) a v2v-emergencyVehicleAlert-nrv2x.cc (sidelink) autorov VaN3Twin.
 *
 * Topologia:
 *   auto --802.11p (broadcast)--------------------------------> RSU (radic semafora svojej krizovatky), susedne auta
 *   auto --NR-V2X sidelink (multicast 225.0.0.0)--------------> RSU, susedne auta
 *   auto --LTE--> eNB --EPC (SGW/PGW)--> server v sieti --kabel (CSMA)--> vsetky RSU
 *
 * Technologie: --use-80211p / --use-lte / --use-nr (vypnuta = nepouziva sa, LTE a NR sa ani neinstaluju;
 *   802.11p zariadenie zostava, lebo GeoNet berie adresu zo zariadenia 0, ale nic nevysiela).
 * Politika: --policy all = kazda CAM na vsetkych zapnutych technologiach; adaptive = viz
 *   trafficManagerClient80211p::xgHybridDecide (priama technologia pre susedov + zaloha pre radic, ked
 *   ho RSU nepotvrdzuje). Jedna technologia v --policy all = cista referencia tej technologie.
 * Vypadok pocas behu: --fail-80211p / --fail-lte / --fail-nr "od:do" [s] (napr. "60:90", "60" = do konca).
 */
#include "ns3/carla-module.h"
#include "ns3/trafficManagerClient80211p-helper.h"
#include "ns3/trafficManagerServer80211p-helper.h"
#include "ns3/trafficManagerClient80211p.h"
#include "ns3/trafficManagerServer80211p.h"
#include "ns3/xgregorm-hybrid-socket.h"
#include "ns3/traci-module.h"
#include "ns3/internet-module.h"
#include "ns3/csma-module.h"
#include "ns3/lte-module.h"
#include "ns3/nr-module.h"
#include "ns3/antenna-module.h"
#include "ns3/buildings-module.h"
#include "ns3/propagation-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/wave-module.h"
#include "ns3/mobility-module.h"
#include "ns3/sumo_xml_parser.h"
#include "ns3/vehicle-visualizer-module.h"
#include "ns3/wifi-module.h"
#include <unistd.h>
#include <deque>
#include <set>

using namespace ns3;

// xgregorm: meranie zataze technologii (len meranie, do siete nic neposiela; definicie v xgregorm-stats.h)
//  802.11p: CBR (ETSI TS 102 687) - pri kazdom vyslani sa pre kazdu stanicu vypocita prijaty vykon tym istym modelom
//           sirenia ako kanal; > -85 dBm = kanal obsadeny po dobu ramca; vlastne vysielanie sa pocita ako obsadene
//  LTE:     vyuzitie PRB uplinku bunky (3GPP TS 36.314) - PRB z pridelenia (UL DCI) kazdeho UE / (PRB bunky x 100 TTI)
//  NR-V2X:  ODHAD SL-CBR z prijatych PSCCH (ns-3 S-RSSI po subkanaloch nevypisuje)
namespace xgload {
std::set<uint32_t> active;                                               // uzly v mape (auta po vstupe, RSU)
std::vector<std::pair<uint32_t, Ptr<MobilityModel>>> wifiNodes;          // vsetky 802.11p stanice v poradi kanala
Ptr<PropagationLossModel> wLoss;                                         // ten isty model ako kanal 802.11p
std::map<uint32_t, std::vector<std::pair<double, double>>> busyW;        // obsadene intervaly [s]
std::map<uint32_t, double> curW, curN;                                   // posledne okno 100 ms
std::map<uint32_t, std::deque<std::pair<double, int>>> nrRx;             // PSCCH: cas, pocet subkanalov
int nrSubch = 0;
double nrSlotsPerSec = 0;
std::map<uint16_t, uint64_t> lteRb;                                      // PRB pridelene v okne, podla bunky
std::map<uint16_t, double> curL;
std::map<uint32_t, Ptr<LteUeNetDevice>> ueOf;
std::vector<uint16_t> cells;
int lteUlRb = 0;
bool useW = false, useL = false, useN = false;
uint64_t wFrames = 0, wBytes = 0;                                        // vyslane ramce 802.11p (MPDU) a ich bajty
// priebezny vypis (kazdych livePeriod s): podiel aut podla hlavnej technologie a priemerna zataz za to obdobie
double livePeriod = 1;
double liveSum[3] = {0, 0, 0}, liveMax[3] = {0, 0, 0};
uint64_t liveN[3] = {0, 0, 0};
int liveTicks = 0;

void
liveAdd (int k, double v)
{
  liveSum[k] += v;
  liveN[k]++;
  liveMax[k] = std::max (liveMax[k], v);
}

void
livePrint ()
{
  // hlavna technologia auta = priama pre susedov (802.11p, inak NR), LTE len bez priamej; zalohu cez LTE/NR pocitame zvlast
  int cnt[4] = {0, 0, 0, 0}, backup = 0, bkNr = 0, bkLte = 0, n = 0, conn[3] = {0, 0, 0};
  for (auto &e : xgstats::pathNow ())
    {
      int p = e.second ();
      int prim = (p & xghyb::WIFI) ? 0 : (p & xghyb::NR) ? 1 : (p & xghyb::LTE) ? 2 : 3;
      cnt[prim]++;
      backup += (prim == 0 && (p & (xghyb::NR | xghyb::LTE))) || (prim == 1 && (p & xghyb::LTE));
      // zaloha = technologia, ktorou ide kopia CAM, hoci hlavna je ina (802.11p je vzdy hlavna, ak je v ceste)
      bkNr += prim == 0 && (p & xghyb::NR);
      bkLte += prim < 2 && (p & xghyb::LTE);
      // pripojenie: 802.11p / NR = pocul RSU za 1 s, LTE = UE v stave RRC CONNECTED (aj pocas odovzdania) a bez vypadku
      auto cf = xgstats::connNow ().find (e.first);
      int cm = cf == xgstats::connNow ().end () ? 0 : cf->second ();
      conn[0] += (cm & xghyb::WIFI) != 0;
      conn[2] += (cm & xghyb::NR) != 0;
      auto ue = ueOf.find (e.first);
      if (ue != ueOf.end () && xghyb::up (xghyb::LTE))
        {
          LteUeRrc::State st = ue->second->GetRrc ()->GetState ();
          conn[1] += st == LteUeRrc::CONNECTED_NORMALLY || st == LteUeRrc::CONNECTED_HANDOVER;
        }
      n++;
    }
  auto pct = [n] (int c) { return n ? 100.0 * c / n : 0.0; };
  auto load = [] (int k) { return liveN[k] ? 100.0 * liveSum[k] / liveN[k] : 0.0; };
  std::cout << "XGREGORM-LIVE,T:" << Simulator::Now ().GetSeconds () << ",VEH:" << n << ",W:" << pct (cnt[0])
            << ",NR:" << pct (cnt[1]) << ",LTE:" << pct (cnt[2]) << ",NONE:" << pct (cnt[3]) << ",BACKUP:" << pct (backup) << ",BK_NR:" << pct (bkNr) << ",BK_LTE:" << pct (bkLte)
            << ",CONN_W:" << pct (conn[0]) << ",CONN_NR:" << pct (conn[2]) << ",CONN_LTE:" << pct (conn[1])
            << ",LOAD_W:" << load (0) << ",LOAD_NR:" << load (2) << ",LOAD_LTE:" << load (1)
            << ",MAX_W:" << 100 * liveMax[0] << ",MAX_NR:" << 100 * liveMax[2] << ",MAX_LTE:" << 100 * liveMax[1] << std::endl;
  for (int k = 0; k < 3; k++)
    liveSum[k] = liveMax[k] = 0, liveN[k] = 0;
}

void
onWifiTx (uint32_t s, Ptr<MobilityModel> mob, Ptr<WifiPhy> phy, WifiConstPsduMap psdus, WifiTxVector txv, double txW)
{
  if (!active.count (s))
    return;
  double now = Simulator::Now ().GetSeconds ();
  double end = now + WifiPhy::CalculateTxDuration (psdus, txv, phy->GetPhyBand ()).GetSeconds ();
  busyW[s].push_back ({now, end});
  wFrames++;
  wBytes += psdus.begin ()->second->GetSize ();
  double txDbm = 10 * std::log10 (txW) + 30;
  // vsetky stanice v poradi kanala (aj neaktivne): model 3GPP losuje stav kanala dvojice pri prvom vypocte,
  // rovnake poradie = rovnake losovanie ako bez merania
  for (auto &w : wifiNodes)
    {
      if (w.first == s)
        continue;
      double rx = wLoss->CalcRxPower (txDbm, mob, w.second);
      if (rx > xgstats::kCbrThrDbm && active.count (w.first))
        busyW[w.first].push_back ({now, end});
    }
}

void
tick ()
{
  double T = Simulator::Now ().GetSeconds () - 0.002; // okno [T - 100 ms, T], ramce uz skoncili
  for (uint32_t n : active)
    {
      if (useW)
        {
          std::vector<std::pair<double, double>> &v = busyW[n];
          std::sort (v.begin (), v.end ());
          double busy = 0, cur = T - 0.1;
          for (auto &iv : v)
            {
              double a = std::max (iv.first, cur), b = std::min (iv.second, T);
              if (b > a)
                {
                  busy += b - a;
                  cur = b;
                }
            }
          v.erase (std::remove_if (v.begin (), v.end (), [T] (const std::pair<double, double> &iv) { return iv.second <= T; }), v.end ());
          curW[n] = std::min (1.0, busy / 0.1);
          xgstats::loadDist ()[0].add (curW[n]);
          liveAdd (0, curW[n]);
        }
      if (useN && nrSubch > 0)
        {
          std::deque<std::pair<double, int>> &d = nrRx[n];
          while (!d.empty () && d.front ().first < T - 0.1)
            d.pop_front ();
          double used = 0;
          for (auto &e : d)
            if (e.first <= T)
              used += e.second;
          curN[n] = std::min (1.0, used / (nrSubch * nrSlotsPerSec * 0.1));
          xgstats::loadDist ()[2].add (curN[n]);
          liveAdd (2, curN[n]);
        }
    }
  if (useL)
    for (uint16_t c : cells)
      {
        curL[c] = std::min (1.0, lteRb[c] / (lteUlRb * 100.0));
        lteRb[c] = 0;
        xgstats::loadDist ()[1].add (curL[c]);
        liveAdd (1, curL[c]);
      }
  if (livePeriod > 0 && ++liveTicks >= (int) std::lround (livePeriod * 10))
    {
      liveTicks = 0;
      livePrint ();
    }
  Simulator::Schedule (MilliSeconds (100), &tick);
}
} // namespace xgload

// xgregorm: dalsia realna prevadzka popri CAM (zataz kanala + meranie jej dorucenia; obsah sa nekoduje v ASN.1,
// posiela sa len sprava spravnej velkosti - ako model prevadzky 3GPP TR 37.885):
//  RSU:  SPATEM 10 Hz, MAPEM 1 Hz (ETSI TS 103 301, C-ITS Day 1) cez 802.11p aj NR-V2X (ktore RSU ma a nemaju vypadok)
//  auta so senzormi (podiel --cpm-share): CPM podla pravidiel generovania ETSI TS 103 324 - kontrola kazdych 100 ms,
//        objekt (ine auto do --cpm-range, v priamej viditelnosti - budovy ho zakryju) sa zaradi pri prvom zisteni,
//        posune o > 4 m alebo po 1 s od posledneho zaradenia; CPM aspon raz za 1 s; kontajner informacii o senzoroch
//        raz za 1 s; ide hlavnou priamou technologiou auta (adaptivne), pri --policy all kazdou priamou v ceste
//  kazda sprava: + BTP-B 4 B + GeoNet SHB 40 B + podpis ETSI TS 103 097 (ak --security; certifikat raz za 1 s)
namespace xgbg {
const uint16_t kEtherType = 0x88B6;   // IEEE Local Experimental Ethertype 2 (beacon RSU ma 0x88B5)
const uint16_t kNrPort = 21;
enum { SPAT = 0, MAP = 1, CPM = 2 };
const char *kName[3] = {"SPATEM", "MAPEM", "CPM"};
struct Station
{
  Ptr<Node> node;
  Ptr<MobilityModel> mob;
  Ptr<Socket> w, nr;
  bool rsu = false, sensor = false;
  uint32_t seq = 0;
  double lastCert = -10, lastCpm = -10, lastSensorInfo = -10;
  std::map<uint32_t, std::pair<Vector, double>> incl;   // objekt -> poloha a cas posledneho zaradenia
};
std::map<uint32_t, Station> st;
bool adaptive = true, urban = false, useW = false, useN = false;
double cpmRange = 150, prrRange = 100;
uint32_t spatBytes = 190, mapBytes = 950, secDigest = 93, secCert = 242;
const uint32_t kHdr = 44;                                  // BTP-B + GeoNet SHB
// meranie po typoch a technologiach (0 = 802.11p, 1 = NR)
uint64_t tx[3][2] = {}, txB[3][2] = {}, exp_[3][2] = {}, dlv[3][2] = {}, objs = 0, cpmN = 0;
struct Pend
{
  double t;
  std::set<uint32_t> want;
};
std::map<std::tuple<uint32_t, uint32_t, int>, Pend> pend;   // (odosielatel, seq, tech) -> kto ma spravu dostat
double lastClean = 0;

bool
los (const Vector &a, const Vector &b)
{
  if (!urban)
    return true;
  for (auto it = BuildingList::Begin (); it != BuildingList::End (); ++it)
    if ((*it)->IsIntersect (a, b))
      return false;
  return true;
}

void
send (Station &s, int type, uint32_t bytes, int techMask)
{
  double now = Simulator::Now ().GetSeconds ();
  bool cert = now - s.lastCert >= 1.0;
  if (cert)
    s.lastCert = now;
  uint32_t size = bytes + kHdr + (cert ? secCert : secDigest);
  uint32_t id = s.node->GetId (), seq = ++s.seq;
  uint8_t buf[12] = {(uint8_t) type};
  std::memcpy (buf + 4, &id, 4);
  std::memcpy (buf + 8, &seq, 4);
  // prijemcovia do prrRange (poloha zo SUMO v okamihu vyslania), auta aj RSU okrem odosielatela
  std::set<uint32_t> want;
  Vector p = s.mob->GetPosition ();
  for (auto &o : st)
    if (o.first != id && xgload::active.count (o.first) && CalculateDistance (p, o.second.mob->GetPosition ()) <= prrRange)
      want.insert (o.first);
  for (int k = 0; k < 2; k++)
    {
      Ptr<Socket> sock = k == 0 ? s.w : s.nr;
      if (!(techMask & (k == 0 ? xghyb::WIFI : xghyb::NR)) || !sock)
        continue;
      buf[1] = k;
      Ptr<Packet> pkt = Create<Packet> (buf, 12);
      pkt->AddPaddingAtEnd (size - 12);
      sock->Send (pkt);
      tx[type][k]++;
      txB[type][k] += size;
      exp_[type][k] += want.size ();
      pend[{id, seq, k}] = {now, want};
    }
  if (now - lastClean > 1.0)   // spravy starsie nez 1 s uz nikto nedostane
    {
      for (auto it = pend.begin (); it != pend.end ();)
        it = now - it->second.t > 1.0 ? pend.erase (it) : std::next (it);
      lastClean = now;
    }
}

void
receive (uint32_t me, Ptr<Socket> sock)
{
  Ptr<Packet> pkt;
  while ((pkt = sock->Recv ()))
    {
      uint8_t buf[12];
      if (pkt->CopyData (buf, 12) < 12)
        continue;
      uint32_t id, seq;
      std::memcpy (&id, buf + 4, 4);
      std::memcpy (&seq, buf + 8, 4);
      auto it = pend.find ({id, seq, (int) buf[1]});
      if (it != pend.end () && it->second.want.erase (me))
        dlv[buf[0]][buf[1]]++;
    }
}

int
rsuMask ()
{
  return (useW && xghyb::up (xghyb::WIFI) ? xghyb::WIFI : 0) | (useN && xghyb::up (xghyb::NR) ? xghyb::NR : 0);
}

void
spat (uint32_t id)
{
  Simulator::Schedule (MilliSeconds (100), &spat, id);
  send (st[id], SPAT, spatBytes, rsuMask ());
}

void
mapem (uint32_t id)
{
  Simulator::Schedule (Seconds (1), &mapem, id);
  send (st[id], MAP, mapBytes, rsuMask ());
}

void
cpm (uint32_t id)
{
  Simulator::Schedule (MilliSeconds (100), &cpm, id);
  Station &s = st[id];
  auto pn = xgstats::pathNow ().find (id);
  if (!xgload::active.count (id) || pn == xgstats::pathNow ().end ())
    return;
  int p = pn->second ();
  int mask = p & (xghyb::WIFI | xghyb::NR);
  if (adaptive && (mask & xghyb::WIFI))
    mask = xghyb::WIFI;   // hlavna priama technologia (zaloha CAM pre radic sa CPM netyka)
  double now = Simulator::Now ().GetSeconds ();
  Vector me = s.mob->GetPosition ();
  uint32_t n = 0;
  for (auto &o : st)
    {
      if (o.first == id || o.second.rsu || !xgload::active.count (o.first))
        continue;
      Vector q = o.second.mob->GetPosition ();
      if (CalculateDistance (me, q) > cpmRange || !los (me, q))
        continue;
      auto in = s.incl.find (o.first);
      if (in == s.incl.end () || CalculateDistance (in->second.first, q) > 4.0 || now - in->second.second >= 1.0)
        {
          s.incl[o.first] = {q, now};
          n++;
        }
    }
  if (n == 0 && now - s.lastCpm < 1.0)
    return;
  n = std::min<uint32_t> (n, 38);   // CPM do ~1400 B (dalej by sa delila na segmenty)
  bool sensorInfo = now - s.lastSensorInfo >= 1.0;
  if (sensorInfo)
    s.lastSensorInfo = now;
  s.lastCpm = now;
  objs += n;
  cpmN++;
  // ITS PDU 8 + management 25 + originating vehicle 15 + perceived object container 2 + 35 B na objekt
  // + informacie o senzoroch (2 senzory) 40 B raz za 1 s
  if (mask)
    send (s, CPM, 50 + 35 * n + (sensorInfo ? 40 : 0), mask);
}

void
install (Ptr<Node> node, bool rsu, bool sensor, bool spatOn, Ptr<UniformRandomVariable> rnd)
{
  Station &s = st[node->GetId ()];
  s.node = node;
  s.mob = node->GetObject<MobilityModel> ();
  s.rsu = rsu;
  s.sensor = sensor;
  uint32_t id = node->GetId ();
  if (useW)
    {
      s.w = Socket::CreateSocket (node, TypeId::LookupByName ("ns3::PacketSocketFactory"));
      PacketSocketAddress local;
      local.SetSingleDevice (node->GetDevice (0)->GetIfIndex ());
      local.SetPhysicalAddress (node->GetDevice (0)->GetAddress ());
      local.SetProtocol (kEtherType);
      s.w->Bind (local);
      PacketSocketAddress remote;
      remote.SetSingleDevice (node->GetDevice (0)->GetIfIndex ());
      remote.SetPhysicalAddress (node->GetDevice (0)->GetBroadcast ());
      remote.SetProtocol (kEtherType);
      s.w->Connect (remote);
      s.w->SetRecvCallback (MakeBoundCallback (&receive, id));
    }
  Ptr<NetDevice> nrDev = useN ? xghyb::findDevice (node, "ns3::NrUeNetDevice") : nullptr;
  if (nrDev)
    {
      s.nr = Socket::CreateSocket (node, UdpSocketFactory::GetTypeId ());
      s.nr->Bind ();
      s.nr->BindToNetDevice (nrDev);
      s.nr->Connect (InetSocketAddress (Ipv4Address ("225.0.0.0"), kNrPort));
      Ptr<Socket> r = Socket::CreateSocket (node, UdpSocketFactory::GetTypeId ());
      r->Bind (InetSocketAddress (Ipv4Address::GetAny (), kNrPort));
      r->BindToNetDevice (nrDev);
      r->SetRecvCallback (MakeBoundCallback (&receive, id));
    }
  if (rsu && spatOn)
    {
      Simulator::Schedule (MilliSeconds (rnd->GetInteger (0, 99)), &spat, id);
      Simulator::Schedule (MilliSeconds (rnd->GetInteger (0, 999)), &mapem, id);
    }
  if (sensor)
    Simulator::Schedule (MilliSeconds (rnd->GetInteger (0, 99)), &cpm, id);
}

void
print ()
{
  for (int t = 0; t < 3; t++)
    for (int k = 0; k < 2; k++)
      if (tx[t][k])
        std::cout << "XGREGORM-BG," << kName[t] << "," << (k ? "NR" : "W") << ",TX:" << tx[t][k] << ",BYTES:" << txB[t][k]
                  << ",EXP:" << exp_[t][k] << ",DLV:" << dlv[t][k] << std::endl;
  std::cout << "XGREGORM-BGCPM,N:" << cpmN << ",OBJ:" << objs << std::endl;
}
} // namespace xgbg

NS_LOG_COMPONENT_DEFINE ("xgregorm-hybrid");

namespace {
// server v sieti: CAM prijate cez LTE (UDP 9) preposle po kabli vsetkym RSU (broadcast 10.10.0.255:9);
// kazda RSU si z nej vezme, len ak je auto na prijazde k jej semaforu (ako v realite geo-filter brokera)
Ptr<Socket> g_relay_out;
uint64_t g_relay_n = 0;
uint64_t g_ho[4] = {0, 0, 0, 0}; // xgregorm: LTE odovzdavanie medzi bunkami: zaciatok, OK, chyba, strata spojenia
const Ipv4Address kBackhaulNet ("10.10.0.0");
const Ipv4Mask kBackhaulMask ("255.255.255.0");

void
XgRelay (Ptr<Socket> s)
{
  Ptr<Packet> p;
  Address from;
  while ((p = s->RecvFrom (from)))
    {
      // vlastny broadcast z kabla (ak by sa vratil) neposielame znova
      if (InetSocketAddress::IsMatchingType (from) &&
          kBackhaulMask.IsMatch (InetSocketAddress::ConvertFrom (from).GetIpv4 (), kBackhaulNet))
        continue;
      g_relay_out->Send (p);
      g_relay_n++;
    }
}
} // namespace

int
main (int argc, char *argv[])
{
  std::vector<float> rate_admitted_values{3,4.5,6,9,12,18,24,27};
  std::string datarate_config;

  std::string sumo_folder = "src/automotive/examples/sumo_files_v2i_TM_map/";
  std::string mob_trace = "cars.rou.xml";
  std::string rsu_file = "stations.xml";
  std::string sumo_config ="src/automotive/examples/sumo_files_v2i_TM_map/map.sumo.cfg";

  bool verbose = true;
  bool realtime = false;
  bool sumo_gui = true;
  double sumo_updates = 0.01;
  bool print_summary = false;
  int txPower=26;
  float datarate=12;
  bool vehicle_vis = false;
  std::string sumo_netstate_file_name;
  bool send_cam = true;
  double m_baseline_prr = 150.0;
  bool m_metric_sup = true;
  double simTime = 200;
  int phase_time = 20;
  double threshold = 70;
  double tls_radius = 50;
  int sumo_seed = 10;
  bool xg_relax = false;
  double xg_relevance = 250;
  uint32_t beacon_ms = 200;
  // hybrid
  bool use_80211p = true, use_lte = true, use_nr = true;
  std::string policy = "adaptive";
  std::string fail_80211p, fail_lte, fail_nr;
  double hyb_range = 250, hyb_qmin = 0.9, hyb_dwell = 1.0;
  double core_delay_ms = 5; // PGW -> server, ako autori v v2i-trafficManager-LTE
  // NR-V2X (hodnoty z v2v-emergencyVehicleAlert-nrv2x autorov)
  double nr_freq = 5.89e9;
  uint16_t nr_bandwidth = 400;
  double nr_tx_power = 23;
  uint16_t nr_numerology = 2;
  uint8_t nr_mcs = 14;
  bool nr_sensing = false; // autori: false (nahodny vyber zdrojov bez sledovania obsadenosti)
  Time nr_bearer_time = Seconds (2.0);

  int numberOfNodes;
  uint32_t nodeCounter = 0;
  int numberOfRSUs;

  CommandLine cmd;
  xmlDocPtr rou_xml_file;

  cmd.AddValue ("realtime", "Use the realtime scheduler or not", realtime);
  cmd.AddValue ("sumo-gui", "Use SUMO gui or not", sumo_gui);
  cmd.AddValue ("sumo-updates", "SUMO granularity", sumo_updates);
  cmd.AddValue ("sumo-folder","Position of sumo config files",sumo_folder);
  cmd.AddValue ("mob-trace", "Name of the mobility trace file", mob_trace);
  cmd.AddValue ("sumo-config", "Location and name of SUMO configuration file", sumo_config);
  cmd.AddValue ("summary", "Print a summary for each vehicle at the end of the simulation", print_summary);
  cmd.AddValue ("vehicle-visualizer", "Activate the web-based vehicle visualizer for ms-van3t", vehicle_vis);
  cmd.AddValue ("send-cam", "Turn on or off the transmission of CAMs",send_cam);
  cmd.AddValue ("netstate-dump-file", "Name of the SUMO netstate-dump file", sumo_netstate_file_name);
  cmd.AddValue ("baseline", "Baseline for PRR calculation (unused, MetricSupervisor is off in hybrid)", m_baseline_prr);
  cmd.AddValue ("met-sup","Ignored: MetricSupervisor is off in the hybrid scenario",m_metric_sup);
  cmd.AddValue ("phase-time", "Duration of traffic lights phases", phase_time);
  cmd.AddValue ("threshold","Threshold multiplier for traffic light phase preemption",threshold);
  cmd.AddValue ("tls-radius","Each RSU controls only traffic lights within this radius [m]",tls_radius);
  cmd.AddValue ("sumo-seed","Random seed for SUMO",sumo_seed);
  cmd.AddValue ("xg-relax","Relevance-based CAM rate: full ETSI rate only near the next traffic light, 1 Hz elsewhere",xg_relax);
  cmd.AddValue ("xg-relevance","Relevance-based CAM rate: distance to the traffic light with full-rate CAMs [m]",xg_relevance);
  cmd.AddValue ("beacon-ms","Adaptive policy: period of the RSU feedback beacon [ms]",beacon_ms);
  cmd.AddValue ("use-80211p","Technology 802.11p available",use_80211p);
  cmd.AddValue ("use-lte","Technology LTE (eNB + core network + server) available",use_lte);
  cmd.AddValue ("use-nr","Technology NR-V2X sidelink available",use_nr);
  cmd.AddValue ("policy","CAM path policy: all (every CAM on all available technologies) or adaptive",policy);
  cmd.AddValue ("fail-80211p","802.11p outage during the run, \"from:to\" [s] (\"60\" = until the end)",fail_80211p);
  cmd.AddValue ("fail-lte","LTE outage during the run, \"from:to\" [s]",fail_lte);
  cmd.AddValue ("fail-nr","NR-V2X outage during the run, \"from:to\" [s]",fail_nr);
  cmd.AddValue ("hyb-range","Adaptive: distance to the next traffic light where its controller needs the vehicle [m]",hyb_range);
  cmd.AddValue ("hyb-qmin","Adaptive: lowest share of CAMs confirmed by the RSU to rely on one direct technology",hyb_qmin);
  cmd.AddValue ("hyb-dwell","Adaptive: the direct link must be good this long before the backup is switched off [s]",hyb_dwell);
  cmd.AddValue ("core-delay","One-way delay PGW -> server [ms]",core_delay_ms);
  cmd.AddValue ("nr-tx-power","NR-V2X tx power [dBm]",nr_tx_power);
  cmd.AddValue ("nr-mcs","NR-V2X sidelink MCS",nr_mcs);
  cmd.AddValue ("nr-sensing","NR-V2X mode 2: sensing-based resource selection (authors: false)",nr_sensing);
  cmd.AddValue ("tx-power", "802.11p transmission power [dBm]", txPower);
  cmd.AddValue ("datarate", "802.11p channel data rate [Mbit/s]", datarate);
  cmd.AddValue ("sim-time", "Total duration of the simulation [s]", simTime);
  bool range_stats = false;
  bool load_stats = true;
  bool color_tech = false;
  cmd.AddValue ("live-period","Print every X s during the run: share of vehicles per main technology and the load of "
                "each technology (needs --load-stats; 0 = off)",xgload::livePeriod);
  cmd.AddValue ("color-tech","Colour vehicles in SUMO by the main technology of their CAMs: blue 802.11p, green NR, "
                "red LTE (no direct technology), grey = none (visualisation only)",color_tech);
  cmd.AddValue ("load-stats","Measure the load of every technology (802.11p CBR, LTE UL PRB usage, NR SL-CBR estimate) "
                "and the performance per load level (measurement only)",load_stats);
  std::string channel_model = "authors";
  uint32_t n_enb = 1;
  double enb_isd = 500;
  double lte_ue_power = 10;
  cmd.AddValue ("lte-ue-power","LTE UE max tx power [dBm] (ns-3 default 10; real UE power class 3 = 23)",lte_ue_power);
  cmd.AddValue ("enb","LTE macro cells: 1 = one cell in the middle of the RSUs (attach at start); k*k > 1 = square grid "
                "with inter-site distance --enb-isd, X2 handover (A3 RSRP), a vehicle attaches when it enters the map",n_enb);
  cmd.AddValue ("enb-isd","Inter-site distance of the LTE cells [m] (3GPP urban macro: 500 m)",enb_isd);
  cmd.AddValue ("channel","Radio channel of the direct technologies: authors = 802.11p LogDistance (authors' 802.11p examples) "
                "and NR 3GPP TR 37.885 V2V highway (authors' NR example); 3gpp = 802.11p on the same 3GPP V2V highway model as NR; "
                "urban = 802.11p and NR on 3GPP TR 37.885 V2V urban with buildings (LOS/NLOS from --buildings-file, NLOSv from "
                "vehicles, shadowing), LTE 3GPP TR 38.901 UMa with buildings, eNB 25 m, RSU 5 m",
                channel_model);
  std::string buildings_file = "";
  cmd.AddValue ("buildings-file","Buildings for --channel urban: one box per line 'xmin xmax ymin ymax height' [m]",buildings_file);
  int security = 0;
  bool spat_on = false;
  double cpm_share = 0;
  cmd.AddValue ("security","Message signature (ETSI TS 103 097): 0 = none (authors), 1 = size model - every vehicle "
                "message gets the signature size measured on the authors' implementation (+93 B digest, +242 B certificate once "
                "per second), 2 = real signing/verification by the authors' GeoNet security (slow, only CAM/beacon)",security);
  cmd.AddValue ("spat","RSU sends SPATEM 10 Hz and MAPEM 1 Hz (ETSI TS 103 301) over its direct technologies",spat_on);
  cmd.AddValue ("mapem-bytes","MAPEM payload [B] (depends on the number of lanes)",xgbg::mapBytes);
  cmd.AddValue ("spatem-bytes","SPATEM payload [B]",xgbg::spatBytes);
  cmd.AddValue ("cpm-share","Share of vehicles with sensors sending CPM (ETSI TS 103 324 generation rules), 0..1",cpm_share);
  cmd.AddValue ("cpm-range","Sensor range of a CPM vehicle [m] (objects also need line of sight with --channel urban)",xgbg::cpmRange);
  cmd.AddValue ("sec-digest","Signature size with a certificate digest [B] (added to SPATEM/MAPEM/CPM with --security)",xgbg::secDigest);
  cmd.AddValue ("sec-cert","Signature size with the full certificate, once per second [B]",xgbg::secCert);
  cmd.AddValue ("range-stats","Measure delivery vs. distance (50 m bins up to 1000 m), car-car and car-RSU",range_stats);
  cmd.Parse (argc, argv);
  xgstats::rangeOn () = range_stats;
  if (security == 1)
    {
      xghyb::secDigest () = xgbg::secDigest;
      xghyb::secCert () = xgbg::secCert;
    }

  uint32_t paths = (use_80211p ? xghyb::WIFI : 0) | (use_lte ? xghyb::LTE : 0) | (use_nr ? xghyb::NR : 0);
  if (paths == 0)
    NS_FATAL_ERROR ("At least one technology must be available (--use-80211p / --use-lte / --use-nr)");
  if (policy != "all" && policy != "adaptive")
    NS_FATAL_ERROR ("--policy must be all or adaptive");
  bool adaptive = policy == "adaptive";
  xghyb::setOutage (xghyb::WIFI, fail_80211p);
  xghyb::setOutage (xghyb::LTE, fail_lte);
  xghyb::setOutage (xghyb::NR, fail_nr);

  if(std::find(rate_admitted_values.begin(), rate_admitted_values.end(), datarate) == rate_admitted_values.end())
    NS_FATAL_ERROR("Fatal error: invalid 802.11p data rate" << datarate << "Mbit/s.");
  datarate_config = datarate == 4.5 ? "OfdmRate4_5MbpsBW10MHz" : "OfdmRate" + std::to_string((int)datarate) + "MbpsBW10MHz";

  if (verbose)
    {
      LogComponentEnable ("CABasicService", LOG_LEVEL_INFO);
      LogComponentEnable ("DENBasicService", LOG_LEVEL_INFO);
      LogComponentEnable ("trafficManagerServer80211p", LOG_LEVEL_INFO);
      LogComponentEnable ("trafficManagerClient80211p", LOG_LEVEL_INFO);
    }

  if(realtime)
      GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));

  // LTE: bunka s viac nez 40 UE potrebuje dlhsiu periodu SRS (inak "too many UEs")
  Config::SetDefault ("ns3::LteEnbRrc::SrsPeriodicity", UintegerValue (320));
  // NR-V2X ako u autorov
  Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue (999999999));

  std::string path = sumo_folder + mob_trace;
  xmlInitParser();
  rou_xml_file = xmlParseFile(path.c_str ());
  if (rou_xml_file == NULL)
    NS_FATAL_ERROR("Error: unable to parse the specified XML file: "<<path);
  numberOfNodes = XML_rou_count_vehicles(rou_xml_file);
  std::string rsu_path = sumo_folder + rsu_file;
  std::ifstream rsu_file_stream (rsu_path.c_str());
  std::vector<std::tuple<std::string, float, float>> rsuData = XML_poli_count_stations(rsu_file_stream);
  numberOfRSUs = rsuData.size();
  xmlFreeDoc(rou_xml_file);
  xmlCleanupParser();
  if(numberOfNodes==-1)
    NS_FATAL_ERROR("Fatal error: cannot gather the number of vehicles from the specified XML file: "<<path);

  ns3::Time simulationTime (ns3::Seconds(simTime));

  /*** 1. Uzly ***/
  NodeContainer obuNodes;
  obuNodes.Create (numberOfNodes);
  NodeContainer rsuNodes;
  rsuNodes.Create (numberOfRSUs);
  NodeContainer serverNodes;
  serverNodes.Create (1);
  Ptr<Node> server = serverNodes.Get (0);

  MobilityHelper mobility;
  mobility.Install (obuNodes);
  mobility.Install (rsuNodes);
  // xgregorm: --channel urban - budovy (kvadre z generatora scenara) pre urcenie LOS/NLOS vo vsetkych 3 technologiach
  bool urban = channel_model == "urban";
  if (urban)
    {
      std::ifstream bf (buildings_file);
      if (!bf)
        NS_FATAL_ERROR ("--channel urban needs --buildings-file");
      double x0, x1, y0, y1, h;
      while (bf >> x0 >> x1 >> y0 >> y1 >> h)
        {
          Ptr<Building> b = CreateObject<Building> ();
          b->SetBoundaries (Box (x0, x1, y0, y1, 0.0, h));
          b->SetBuildingType (Building::Commercial);
          b->SetExtWallsType (Building::ConcreteWithWindows);
        }
      BuildingsHelper::Install (obuNodes);
      BuildingsHelper::Install (rsuNodes);
      std::cout << "XGREGORM-BUILDINGS," << BuildingList::GetNBuildings () << std::endl;
    }

  /*** 2. 802.11p - zariadenie 0 na autach aj RSU (aplikacie a GeoNet), instaluje sa vzdy ***/
  YansWifiPhyHelper wifiPhy;
  wifiPhy.Set ("TxPowerStart", DoubleValue (txPower));
  wifiPhy.Set ("TxPowerEnd", DoubleValue (txPower));
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default ();
  Ptr<YansWifiChannel> channel = wifiChannel.Create ();
  // xgregorm: --channel 3gpp - 802.11p na rovnakom modeli sirenia ako NR-V2X (3GPP TR 37.885 V2V highway: LOS/NLOSv
  // podla vzdialenosti, bez tienenia, stav kanala pre dvojicu sa neaktualizuje - presne ako NR u autorov), aby rozdiel
  // dosahu urcovala technologia (vykon, citlivost, modulacia), nie rozny model
  if (channel_model == "3gpp")
    {
      Ptr<ThreeGppV2vHighwayChannelConditionModel> ccm = CreateObject<ThreeGppV2vHighwayChannelConditionModel> ();
      ccm->SetAttribute ("UpdatePeriod", TimeValue (MilliSeconds (0)));
      ccm->AssignStreams (2000);
      Ptr<ThreeGppV2vHighwayPropagationLossModel> loss = CreateObject<ThreeGppV2vHighwayPropagationLossModel> ();
      loss->SetAttribute ("Frequency", DoubleValue (5.9e9));
      loss->SetAttribute ("ShadowingEnabled", BooleanValue (false));
      loss->SetChannelConditionModel (ccm);
      loss->AssignStreams (2010);
      channel = CreateObject<YansWifiChannel> ();
      channel->SetPropagationDelayModel (CreateObject<ConstantSpeedPropagationDelayModel> ());
      channel->SetPropagationLossModel (loss);
      xgload::wLoss = loss;
    }
  // xgregorm: --channel urban - 3GPP TR 37.885 V2V urban: budova medzi stanicami = NLOS, inak LOS / NLOSv (auta
  // v ceste, pravdepodobnost podla vzdialenosti); stav sa pre dvojicu prepocita kazdych 100 ms (auta sa hybu),
  // tienenie zapnute (LOS 3 dB, NLOSv 4 dB, NLOS 4 dB); rovnaky model ako NR
  else if (urban)
    {
      Ptr<ThreeGppV2vUrbanChannelConditionModel> ccm = CreateObject<ThreeGppV2vUrbanChannelConditionModel> ();
      ccm->SetAttribute ("UpdatePeriod", TimeValue (MilliSeconds (100)));
      ccm->AssignStreams (2000);
      Ptr<ThreeGppV2vUrbanPropagationLossModel> loss = CreateObject<ThreeGppV2vUrbanPropagationLossModel> ();
      loss->SetAttribute ("Frequency", DoubleValue (5.9e9));
      loss->SetAttribute ("ShadowingEnabled", BooleanValue (true));
      loss->SetChannelConditionModel (ccm);
      loss->AssignStreams (2010);
      channel = CreateObject<YansWifiChannel> ();
      channel->SetPropagationDelayModel (CreateObject<ConstantSpeedPropagationDelayModel> ());
      channel->SetPropagationLossModel (loss);
      xgload::wLoss = loss;
    }
  else if (channel_model != "authors")
    NS_FATAL_ERROR ("--channel must be authors, 3gpp or urban");
  wifiPhy.SetChannel (channel);
  wifiPhy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11);
  NqosWaveMacHelper wifi80211pMac = NqosWaveMacHelper::Default ();
  Wifi80211pHelper wifi80211p = Wifi80211pHelper::Default ();
  wifi80211p.SetRemoteStationManager ("ns3::ConstantRateWifiManager", "DataMode", StringValue (datarate_config), "ControlMode", StringValue (datarate_config));
  wifi80211p.Install (wifiPhy, wifi80211pMac, obuNodes);
  wifi80211p.Install (wifiPhy, wifi80211pMac, rsuNodes);
  // xgregorm: meranie CBR 802.11p - kanal autorov = YansWifiChannelHelper::Default (LogDistance s predvolenymi
  // parametrami, deterministicky), pre meranie rovnaky model; pri 3gpp ten isty objekt ako kanal
  if (!xgload::wLoss)
    xgload::wLoss = CreateObject<LogDistancePropagationLossModel> ();
  xgload::useW = use_80211p && load_stats;
  for (NodeContainer *nc : {&obuNodes, &rsuNodes})
    for (uint32_t n = 0; n < nc->GetN (); ++n)
      {
        Ptr<Node> node = nc->Get (n);
        Ptr<MobilityModel> mob = node->GetObject<MobilityModel> ();
        xgload::wifiNodes.push_back ({node->GetId (), mob});
        Ptr<WifiPhy> phy = DynamicCast<WifiNetDevice> (node->GetDevice (0))->GetPhy ();
        if (xgload::useW)
          phy->TraceConnectWithoutContext ("PhyTxPsduBegin", MakeBoundCallback (&xgload::onWifiTx, node->GetId (), mob, phy));
      }
  for (uint32_t n = 0; n < rsuNodes.GetN (); ++n)
    xgload::active.insert (rsuNodes.Get (n)->GetId ());

  /*** 3. IP (InternetStackHelper pridava aj PacketSocketFactory pre 802.11p) ***/
  InternetStackHelper internet;
  internet.Install (obuNodes);
  internet.Install (rsuNodes);
  internet.Install (serverNodes);
  Ipv4StaticRoutingHelper ipv4RoutingHelper;

  /*** 4. LTE + EPC: auta = UE, jedna makro bunka v strede mesta, server za PGW ***/
  Ipv4Address serverAddr ("1.0.0.2");
  // helpery musia zit az do konca simulacie (planuju udalosti s ukazovatelom na seba)
  Ptr<LteHelper> lteHelper;
  Ptr<PointToPointEpcHelper> epcHelper;
  Ptr<NrPointToPointEpcHelper> nrEpcHelper;
  Ptr<NrHelper> nrHelper;
  Ptr<NrSlHelper> nrSlHelper;
  NetDeviceContainer lteUeDevs;
  if (use_lte)
    {
      Config::SetDefault ("ns3::LteUePhy::TxPower", DoubleValue (lte_ue_power));
      lteHelper = CreateObject<LteHelper> ();
      epcHelper = CreateObject<PointToPointEpcHelper> ();
      lteHelper->SetEpcHelper (epcHelper);
      Ptr<Node> pgw = epcHelper->GetPgwNode ();

      PointToPointHelper p2ph;
      p2ph.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("10Gb/s")));
      p2ph.SetDeviceAttribute ("Mtu", UintegerValue (1500));
      p2ph.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (core_delay_ms)));
      NetDeviceContainer coreDevs = p2ph.Install (pgw, server);
      Ipv4AddressHelper coreIp;
      coreIp.SetBase ("1.0.0.0", "255.0.0.0");
      Ipv4InterfaceContainer coreIfaces = coreIp.Assign (coreDevs);
      serverAddr = coreIfaces.GetAddress (1);
      Ptr<Ipv4StaticRouting> serverRouting = ipv4RoutingHelper.GetStaticRouting (server->GetObject<Ipv4> ());
      serverRouting->AddNetworkRouteTo (Ipv4Address ("7.0.0.0"), Ipv4Mask ("255.0.0.0"),
                                        server->GetObject<Ipv4> ()->GetInterfaceForDevice (coreDevs.Get (1)));

      // eNB v strede RSU (mesto), vyska stoziaru 30 m
      double cx = 0, cy = 0;
      for (auto &r : rsuData)
        {
          cx += std::get<1> (r) / rsuData.size ();
          cy += std::get<2> (r) / rsuData.size ();
        }
      // viac buniek (--enb k*k): stvorcova siet stoziarov okolo stredu, sused po enb_isd, odovzdavanie cez X2
      uint32_t k = (uint32_t) std::lround (std::sqrt ((double) n_enb));
      if (k < 1 || k * k != n_enb)
        NS_FATAL_ERROR ("--enb must be a square number (1, 4, 9, ...)");
      if (n_enb > 1)
        {
          lteHelper->SetHandoverAlgorithmType ("ns3::A3RsrpHandoverAlgorithm");
          lteHelper->SetHandoverAlgorithmAttribute ("Hysteresis", DoubleValue (3.0));
          lteHelper->SetHandoverAlgorithmAttribute ("TimeToTrigger", TimeValue (MilliSeconds (256)));
        }
      NodeContainer enbNodes;
      enbNodes.Create (n_enb);
      MobilityHelper enbMobility;
      Ptr<ListPositionAllocator> enbPos = CreateObject<ListPositionAllocator> ();
      for (uint32_t a = 0; a < k; a++)
        for (uint32_t b = 0; b < k; b++)
          enbPos->Add (Vector (cx + (a - (k - 1) / 2.0) * enb_isd, cy + (b - (k - 1) / 2.0) * enb_isd, urban ? 25 : 30));
      enbMobility.SetPositionAllocator (enbPos);
      enbMobility.Install (enbNodes);
      // xgregorm: --channel urban - LTE 3GPP TR 38.901 UMa (eNB 25 m), LOS/NLOS podla budov, tienenie zapnute
      // (predvolene ns-3 LTE = Friis, bez budov)
      if (urban)
        {
          BuildingsHelper::Install (enbNodes);
          lteHelper->SetAttribute ("PathlossModel", StringValue ("ns3::ThreeGppUmaPropagationLossModel"));
          lteHelper->SetPathlossModelAttribute ("ShadowingEnabled", BooleanValue (true));
          lteHelper->SetPathlossModelAttribute ("ChannelConditionModel",
                                                PointerValue (CreateObject<BuildingsChannelConditionModel> ()));
        }

      NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice (enbNodes);
      NetDeviceContainer ueDevs = lteHelper->InstallUeDevice (obuNodes);
      lteUeDevs = ueDevs;
      // xgregorm: meranie vyuzitia PRB uplinku po bunkach
      xgload::useL = load_stats;
      for (uint32_t e = 0; e < enbDevs.GetN (); ++e)
        {
          Ptr<LteEnbNetDevice> enb = DynamicCast<LteEnbNetDevice> (enbDevs.Get (e));
          xgload::cells.push_back (enb->GetCellId ());
          xgload::lteUlRb = enb->GetUlBandwidth ();
        }
      for (uint32_t u = 0; u < ueDevs.GetN (); ++u)
        {
          Ptr<LteUeNetDevice> ue = DynamicCast<LteUeNetDevice> (ueDevs.Get (u));
          xgload::ueOf[obuNodes.Get (u)->GetId ()] = ue;
          if (xgload::useL)
            ue->GetPhy ()->TraceConnectWithoutContext (
              "ReportUlPhyResourceBlocks", MakeBoundCallback (+[] (Ptr<LteUeNetDevice> d, uint16_t, const std::vector<int> &rbs) {
                xgload::lteRb[d->GetRrc ()->GetCellId ()] += rbs.size ();
              }, ue));
        }
      epcHelper->AssignUeIpv4Address (ueDevs);
      for (uint32_t u = 0; u < obuNodes.GetN (); ++u)
        {
          // LTE je jedina cesta do siete (802.11p ide cez PacketSocket/GeoNet, NR len multicast 225.0.0.0)
          Ptr<Ipv4> ipv4 = obuNodes.Get (u)->GetObject<Ipv4> ();
          ipv4RoutingHelper.GetStaticRouting (ipv4)->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (),
                                                                       ipv4->GetInterfaceForDevice (ueDevs.Get (u)));
        }
      if (n_enb == 1)
        lteHelper->Attach (ueDevs, enbDevs.Get (0));
      else
        {
          lteHelper->AddX2Interface (enbNodes); // UE sa pripoja az pri vstupe do mapy (vyber bunky podla polohy)
          Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/LteUeRrc/HandoverStart",
                                         MakeCallback (+[] (uint64_t, uint16_t, uint16_t, uint16_t) { g_ho[0]++; }));
          Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndOk",
                                         MakeCallback (+[] (uint64_t, uint16_t, uint16_t) { g_ho[1]++; }));
          Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndError",
                                         MakeCallback (+[] (uint64_t, uint16_t, uint16_t) { g_ho[2]++; }));
          Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/LteUeRrc/RadioLinkFailure",
                                         MakeCallback (+[] (uint64_t, uint16_t, uint16_t) { g_ho[3]++; }));
        }
    }

  /*** 5. NR-V2X sidelink (mode 2, bez gNB): auta aj RSU; nastavenie ako v2v-emergencyVehicleAlert-nrv2x ***/
  if (use_nr)
    {
      NodeContainer slNodes;
      slNodes.Add (obuNodes);
      slNodes.Add (rsuNodes);

      // NR sidelink potrebuje vlastny NrPointToPointEpcHelper (LTE UE neprijme) a oba EPC helpery maju
      // natvrdo rovnake interne adresy (PGW 7.0.0.1, S5/S11 ...). Su to dve oddelene siete (NR jadro sa
      // pre sidelink nepouziva), preto vypneme len globalnu kontrolu kolizii adries.
      if (use_lte)
        Ipv4AddressGenerator::TestMode ();
      nrEpcHelper = CreateObject<NrPointToPointEpcHelper> ();
      nrHelper = CreateObject<NrHelper> ();
      nrHelper->SetEpcHelper (nrEpcHelper);

      CcBwpCreator ccBwpCreator;
      // xgregorm: --channel urban - V2V urban s budovami (ako 802.11p), stav LOS/NLOS kazdych 100 ms, tienenie;
      // maticu kanala (fading) ns-3 pri zmene LOS/NLOS prepocita samo
      CcBwpCreator::SimpleOperationBandConf bandConfSl (nr_freq, nr_bandwidth, 1,
                                                        urban ? BandwidthPartInfo::V2V_Urban : BandwidthPartInfo::V2V_Highway);
      OperationBandInfo bandSl = ccBwpCreator.CreateOperationBandContiguousCc (bandConfSl);
      Config::SetDefault ("ns3::ThreeGppChannelModel::UpdatePeriod", TimeValue (MilliSeconds (0)));
      nrHelper->SetChannelConditionModelAttribute ("UpdatePeriod", TimeValue (MilliSeconds (urban ? 100 : 0)));
      nrHelper->SetPathlossAttribute ("ShadowingEnabled", BooleanValue (urban));
      nrHelper->InitializeOperationBand (&bandSl);
      BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps ({bandSl});

      nrHelper->SetUeAntennaAttribute ("NumRows", UintegerValue (1));
      nrHelper->SetUeAntennaAttribute ("NumColumns", UintegerValue (2));
      nrHelper->SetUeAntennaAttribute ("AntennaElement", PointerValue (CreateObject<IsotropicAntennaModel> ()));
      nrHelper->SetUePhyAttribute ("TxPower", DoubleValue (nr_tx_power));
      nrHelper->SetUeMacAttribute ("EnableSensing", BooleanValue (nr_sensing));
      nrHelper->SetUeMacAttribute ("T1", UintegerValue (2));
      nrHelper->SetUeMacAttribute ("T2", UintegerValue (81));
      nrHelper->SetUeMacAttribute ("ActivePoolId", UintegerValue (0));
      nrHelper->SetUeMacAttribute ("ReservationPeriod", TimeValue (MilliSeconds (20)));
      nrHelper->SetUeMacAttribute ("NumSidelinkProcess", UintegerValue (4));
      nrHelper->SetUeMacAttribute ("EnableBlindReTx", BooleanValue (true));
      nrHelper->SetUeMacAttribute ("SlThresPsschRsrp", IntegerValue (-128));
      uint8_t bwpId = 0;
      nrHelper->SetBwpManagerTypeId (TypeId::LookupByName ("ns3::NrSlBwpManagerUe"));
      nrHelper->SetUeBwpManagerAlgorithmAttribute ("GBR_MC_PUSH_TO_TALK", UintegerValue (bwpId));
      std::set<uint8_t> bwpIds = {bwpId};

      NetDeviceContainer slDevs = nrHelper->InstallUeDevice (slNodes, allBwps);
      for (auto it = slDevs.Begin (); it != slDevs.End (); ++it)
        DynamicCast<NrUeNetDevice> (*it)->UpdateConfig ();

      nrSlHelper = CreateObject<NrSlHelper> ();
      nrSlHelper->SetEpcHelper (nrEpcHelper);
      nrSlHelper->SetSlErrorModel ("ns3::NrLteMiErrorModel");
      nrSlHelper->SetUeSlAmcAttribute ("AmcModel", EnumValue (NrAmc::ErrorModel));
      nrSlHelper->SetNrSlSchedulerTypeId (NrSlUeMacSchedulerSimple::GetTypeId ());
      nrSlHelper->SetUeSlSchedulerAttribute ("FixNrSlMcs", BooleanValue (true));
      nrSlHelper->SetUeSlSchedulerAttribute ("InitialNrSlMcs", UintegerValue (nr_mcs));
      nrSlHelper->PrepareUeForSidelink (slDevs, bwpIds);

      Ptr<NrSlCommPreconfigResourcePoolFactory> ptrFactory = Create<NrSlCommPreconfigResourcePoolFactory> ();
      std::vector<std::bitset<1>> slBitMap (10, std::bitset<1> (1));
      ptrFactory->SetSlTimeResources (slBitMap);
      ptrFactory->SetSlSensingWindow (100);
      ptrFactory->SetSlSelectionWindow (5);
      ptrFactory->SetSlFreqResourcePscch (10);
      ptrFactory->SetSlSubchannelSize (10);
      ptrFactory->SetSlMaxNumPerReserve (3);
      LteRrcSap::SlResourcePoolConfigNr poolConfig;
      poolConfig.haveSlResourcePoolConfigNr = true;
      LteRrcSap::SlResourcePoolIdNr poolId;
      poolId.id = 0;
      poolConfig.slResourcePoolId = poolId;
      poolConfig.slResourcePool = ptrFactory->CreatePool ();
      LteRrcSap::SlBwpPoolConfigCommonNr bwpPoolConfig;
      bwpPoolConfig.slTxPoolSelectedNormal[poolId.id] = poolConfig;
      LteRrcSap::Bwp bwp;
      bwp.numerology = nr_numerology;
      bwp.symbolsPerSlots = 14;
      bwp.rbPerRbg = 1;
      bwp.bandwidth = nr_bandwidth;
      LteRrcSap::SlBwpGeneric bwpGeneric;
      bwpGeneric.bwp = bwp;
      bwpGeneric.slLengthSymbols = LteRrcSap::GetSlLengthSymbolsEnum (14);
      bwpGeneric.slStartSymbol = LteRrcSap::GetSlStartSymbolEnum (0);
      LteRrcSap::SlBwpConfigCommonNr bwpConfig;
      bwpConfig.haveSlBwpGeneric = true;
      bwpConfig.slBwpGeneric = bwpGeneric;
      bwpConfig.haveSlBwpPoolConfigCommonNr = true;
      bwpConfig.slBwpPoolConfigCommonNr = bwpPoolConfig;
      LteRrcSap::SlFreqConfigCommonNr freqConfig;
      for (uint8_t id : bwpIds)
        freqConfig.slBwpList[id] = bwpConfig;
      LteRrcSap::TddUlDlConfigCommon tdd;
      tdd.tddPattern = "UL|UL|UL|UL|UL|UL|UL|UL|UL|UL|";
      LteRrcSap::SlPreconfigGeneralNr general;
      general.slTddConfig = tdd;
      LteRrcSap::SlUeSelectedConfig ueSelected;
      ueSelected.slProbResourceKeep = 0.0;
      LteRrcSap::SlPsschTxParameters psschParams;
      psschParams.slMaxTxTransNumPssch = 5;
      LteRrcSap::SlPsschTxConfigList psschList;
      psschList.slPsschTxParameters[0] = psschParams;
      ueSelected.slPsschTxConfigList = psschList;
      LteRrcSap::SidelinkPreconfigNr preconfig;
      preconfig.slPreconfigGeneral = general;
      preconfig.slUeSelectedPreConfig = ueSelected;
      preconfig.slPreconfigFreqInfoList[0] = freqConfig;
      nrSlHelper->InstallNrSlPreConfiguration (slDevs, preconfig);
      // xgregorm: ODHAD obsadenosti NR kanala (SL-CBR) - kazdy prijaty PSCCH = obsadene subkanaly v jednom slote;
      // subkanaly poolu = RB sirky pasma / velkost subkanala (SetSlSubchannelSize (10) vyssie)
      xgload::useN = load_stats;
      xgstats::nrFrom () = nr_bearer_time.GetSeconds ();
      xgload::nrSlotsPerSec = 1000.0 * (1 << nr_numerology);
      xgload::nrSubch = DynamicCast<NrUeNetDevice> (slDevs.Get (0))->GetPhy (0)->GetRbNum () / 10;
      for (uint32_t n = 0; n < slDevs.GetN () && xgload::useN; ++n)
        DynamicCast<NrUeNetDevice> (slDevs.Get (n))->GetPhy (0)->GetSpectrumPhy ()->TraceConnectWithoutContext (
            "RxPscchTraceUe", MakeBoundCallback (+[] (uint32_t nd, const SlRxCtrlPacketTraceParams p) {
              xgload::nrRx[nd].push_back ({Simulator::Now ().GetSeconds (), (int) p.m_lengthSubChannel});
            }, slDevs.Get (n)->GetNode ()->GetId ()));

      int64_t stream = 1000;
      stream += nrHelper->AssignStreams (slDevs, stream);
      stream += nrSlHelper->AssignStreams (slDevs, stream);

      // IP na NR rozhrani vlastna podsiet (LTE EPC uz pouziva 7.0.0.0/8); multicast 225.0.0.0 cez NR
      Ipv4AddressHelper nrIp;
      nrIp.SetBase ("8.0.0.0", "255.0.0.0");
      nrIp.Assign (slDevs);
      for (uint32_t n = 0; n < slNodes.GetN (); ++n)
        {
          Ptr<Ipv4> ipv4 = slNodes.Get (n)->GetObject<Ipv4> ();
          ipv4RoutingHelper.GetStaticRouting (ipv4)->AddNetworkRouteTo (Ipv4Address ("225.0.0.0"), Ipv4Mask ("255.0.0.0"),
                                                                         ipv4->GetInterfaceForDevice (slDevs.Get (n)));
        }
      Ipv4Address group ("225.0.0.0");
      uint32_t dstL2Id = 255;
      nrSlHelper->ActivateNrSlBearer (nr_bearer_time, slDevs,
                                      Create<LteSlTft> (LteSlTft::Direction::TRANSMIT, LteSlTft::CommType::GroupCast, group, dstL2Id));
      nrSlHelper->ActivateNrSlBearer (nr_bearer_time, slDevs,
                                      Create<LteSlTft> (LteSlTft::Direction::RECEIVE, LteSlTft::CommType::GroupCast, group, dstL2Id));
    }

  /*** 6. Kabel: server + vsetky RSU na jednom segmente (mestska siet riadenia dopravy) ***/
  NodeContainer backhaulNodes;
  backhaulNodes.Add (server);
  backhaulNodes.Add (rsuNodes);
  CsmaHelper csma;
  csma.SetChannelAttribute ("DataRate", StringValue ("100Mbps"));
  csma.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (1)));
  NetDeviceContainer backhaulDevs = csma.Install (backhaulNodes);
  Ipv4AddressHelper backhaulIp;
  backhaulIp.SetBase ("10.10.0.0", "255.255.255.0");
  backhaulIp.Assign (backhaulDevs);

  // server: prijem CAM z LTE a preposlanie RSU
  Ptr<Socket> relayIn = Socket::CreateSocket (server, UdpSocketFactory::GetTypeId ());
  relayIn->Bind (InetSocketAddress (Ipv4Address::GetAny (), 9));
  relayIn->SetRecvCallback (MakeCallback (&XgRelay));
  g_relay_out = Socket::CreateSocket (server, UdpSocketFactory::GetTypeId ());
  g_relay_out->Bind ();
  g_relay_out->SetAllowBroadcast (true);
  g_relay_out->Connect (InetSocketAddress (Ipv4Address ("10.10.0.255"), 9));

  /*** 7. SUMO ***/
  Ptr<TraciClient> sumoClient = CreateObject<TraciClient> ();
  sumoClient->SetAttribute ("SumoConfigPath", StringValue (sumo_config));
  sumoClient->SetAttribute ("SumoBinaryPath", StringValue (""));
  sumoClient->SetAttribute ("SynchInterval", TimeValue (Seconds (sumo_updates)));
  sumoClient->SetAttribute ("StartTime", TimeValue (Seconds (0.0)));
  sumoClient->SetAttribute ("SumoGUI", (BooleanValue) sumo_gui);
  sumoClient->SetAttribute ("SumoPort", UintegerValue (3400));
  sumoClient->SetAttribute ("PenetrationRate", DoubleValue (1.0));
  sumoClient->SetAttribute ("SumoLogFile", BooleanValue (false));
  sumoClient->SetAttribute ("SumoStepLog", BooleanValue (false));
  sumoClient->SetAttribute ("SumoSeed", IntegerValue (sumo_seed));
  std::string sumo_additional_options = "--verbose true";
  if(sumo_netstate_file_name!="")
    sumo_additional_options += " --netstate-dump " + sumo_netstate_file_name;
  sumo_additional_options += " --collision.action warn --collision.check-junctions --error-log=sumo-errors-or-collisions.xml";
  sumoClient->SetAttribute ("SumoWaitForSocket", TimeValue (Seconds (1.0)));
  sumoClient->SetAttribute ("SumoAdditionalCmdOptions", StringValue (sumo_additional_options));

  vehicleVisualizer vehicleVisObj;
  Ptr<vehicleVisualizer> vehicleVis = &vehicleVisObj;
  if (vehicle_vis)
  {
      vehicleVis->startServer();
      vehicleVis->connectToServer ();
      sumoClient->SetAttribute ("VehicleVisualizer", PointerValue (vehicleVis));
  }

  // MetricSupervisor meria len GeoNet: CAM poslanu inou technologiou nez 802.11p povazuje za nepocuty
  // broadcast a CAM poslanu viacerymi cestami za viac prijmov toho isteho paketu (PRR > 1 -> NS_FATAL_ERROR).
  // V hybride sa preto nepouziva; meria sa XGREGORM-V2V / -AW / -HYB (xgregorm-stats.h).
  Ptr<MetricSupervisor> metSup = NULL;

  /*** 8. RSU = radic semafora svojej krizovatky ***/
  trafficManagerServer80211pHelper serverHelper;
  serverHelper.SetAttribute ("Client", (PointerValue) sumoClient);
  serverHelper.SetAttribute ("RealTime", BooleanValue(realtime));
  serverHelper.SetAttribute ("PhaseTime", UintegerValue(phase_time));
  serverHelper.SetAttribute ("Threshold", DoubleValue(threshold));
  serverHelper.SetAttribute ("TlsRadius", DoubleValue(tls_radius));
  serverHelper.SetAttribute ("MetricSupervisor", PointerValue (metSup));
  serverHelper.SetAttribute ("SendCAM", BooleanValue (use_80211p)); // CAM samotnej RSU idu len cez 802.11p
  serverHelper.SetAttribute ("XgBeaconMs", UintegerValue (beacon_ms));
  // beacon so spatnou vazbou len pre adaptivnu politiku (inak by len zatazoval kanal)
  serverHelper.SetAttribute ("XgHybrid", UintegerValue (adaptive ? 2 : 1));
  serverHelper.SetAttribute ("XgNr", BooleanValue (use_nr));
  serverHelper.SetAttribute ("XgSecurity", BooleanValue (security == 2));
  int i = 0;
  for (auto rsu : rsuData)
    {
      Ptr<Node> rsuNode = rsuNodes.Get (i++);
      // xgregorm: --channel urban - antena RSU 5 m (3GPP TR 37.885), inak ako u autorov 0 m
      sumoClient->AddStation (std::get<0> (rsu), std::get<1> (rsu), std::get<2> (rsu), urban ? 5.0 : 0.0, rsuNode);
      ApplicationContainer app = serverHelper.Install (rsuNode);
      app.Start (Seconds (0.0));
      app.Stop (simulationTime - Seconds (0.1));
    }

  /*** 9. Auta ***/
  trafficManagerClient80211pHelper clientHelper;
  clientHelper.SetAttribute ("ServerAddr", Ipv4AddressValue (serverAddr));
  clientHelper.SetAttribute ("Client", (PointerValue) sumoClient);
  clientHelper.SetAttribute ("PrintSummary", BooleanValue(print_summary));
  clientHelper.SetAttribute ("RealTime", BooleanValue(realtime));
  clientHelper.SetAttribute ("SendCAM", BooleanValue (send_cam));
  clientHelper.SetAttribute ("MetricSupervisor", PointerValue (metSup));
  clientHelper.SetAttribute ("XgRelax", BooleanValue (xg_relax));
  clientHelper.SetAttribute ("XgRelevance", DoubleValue (xg_relevance));
  clientHelper.SetAttribute ("XgHybrid", UintegerValue (adaptive ? 2 : 1));
  clientHelper.SetAttribute ("XgPaths", UintegerValue (paths));
  clientHelper.SetAttribute ("XgHybRange", DoubleValue (hyb_range));
  clientHelper.SetAttribute ("XgHybQmin", DoubleValue (hyb_qmin));
  clientHelper.SetAttribute ("XgHybDwell", DoubleValue (hyb_dwell));
  clientHelper.SetAttribute ("XgColor", BooleanValue (color_tech));
  clientHelper.SetAttribute ("XgSecurity", BooleanValue (security == 2));

  STARTUP_FCN setupNewNode = [&] (std::string vehicleID,TraciClient::StationTypeTraCI_t stationType) -> Ptr<Node>
    {
      if (nodeCounter >= obuNodes.GetN())
        NS_FATAL_ERROR("Node Pool empty!: " << nodeCounter << " nodes created.");
      Ptr<Node> includedNode = obuNodes.Get(nodeCounter);
      // xgregorm: viac LTE buniek - UE sa pripoji, az ked ma auto polohu (vyber bunky v idle mode)
      if (use_lte && n_enb > 1)
        {
          Ptr<NetDevice> ue = lteUeDevs.Get (nodeCounter);
          Simulator::Schedule (MilliSeconds (10), [lteHelper, ue] () { lteHelper->Attach (ue); });
        }
      ++nodeCounter;
      xgload::active.insert (includedNode->GetId ());
      ApplicationContainer ClientApp = clientHelper.Install (includedNode);
      ClientApp.Start (Seconds (0.0));
      ClientApp.Stop (simulationTime - Simulator::Now () - Seconds (0.1));
      return includedNode;
    };

  SHUTDOWN_FCN shutdownNode = [] (Ptr<Node> exNode,std::string vehicleID)
    {
      xgload::active.erase (exNode->GetId ());
      Ptr<trafficManagerClient80211p> app = exNode->GetApplication(0)->GetObject<trafficManagerClient80211p>();
      if(app)
        app->StopApplicationNow ();
      Ptr<ConstantPositionMobilityModel> mob = exNode->GetObject<ConstantPositionMobilityModel>();
      mob->SetPosition(Vector(-1000.0+(rand()%25),320.0+(rand()%25),250.0));
    };

  sumoClient->SumoSetup (setupNewNode, shutdownNode);

  // xgregorm: dalsia prevadzka (SPATEM/MAPEM z RSU, CPM z aut so senzormi), len ak je zapnuta
  if (spat_on || cpm_share > 0)
    {
      xgbg::adaptive = adaptive;
      xgbg::urban = urban;
      xgbg::useW = use_80211p;
      xgbg::useN = use_nr;
      if (!security)
        xgbg::secDigest = xgbg::secCert = 0;
      Ptr<UniformRandomVariable> rnd = CreateObject<UniformRandomVariable> ();
      rnd->SetStream (3000);
      for (uint32_t n = 0; n < rsuNodes.GetN (); ++n)
        xgbg::install (rsuNodes.Get (n), true, false, spat_on, rnd);
      for (uint32_t n = 0; n < obuNodes.GetN (); ++n)
        xgbg::install (obuNodes.Get (n), false, rnd->GetValue () < cpm_share, false, rnd);
    }

  // xgregorm: zataz technologii kazdych 100 ms; vykonnost podla zataze odosielatela v case vyslania CAM
  if (xgload::useW)
    xgstats::loadFn ()[0] = [] (uint32_t n) { return xgload::curW[n]; };
  if (xgload::useL)
    xgstats::loadFn ()[1] = [] (uint32_t n) {
      auto ue = xgload::ueOf.find (n);
      return ue == xgload::ueOf.end () ? 0.0 : xgload::curL[ue->second->GetRrc ()->GetCellId ()];
    };
  if (xgload::useN)
    xgstats::loadFn ()[2] = [] (uint32_t n) { return xgload::curN[n]; };
  if (load_stats)
    Simulator::Schedule (MilliSeconds (102), &xgload::tick);

  Simulator::Stop (simulationTime);
  Simulator::Run ();
  xgstats::finishAllTx ();
  Simulator::Destroy ();

  std::cout << "XGREGORM-RELAY,CAM:" << g_relay_n << std::endl;
  if (spat_on || cpm_share > 0)
    xgbg::print ();
  std::cout << "XGREGORM-WIFIFRAMES,N:" << xgload::wFrames << ",BYTES:" << xgload::wBytes << std::endl;
  std::cout << "XGREGORM-NRDROP,N:" << NrNetDevice::m_xgUnknownIpDrops << std::endl;
  std::cout << "XGREGORM-LTEHO,START:" << g_ho[0] << ",OK:" << g_ho[1] << ",ERR:" << g_ho[2] << ",RLF:" << g_ho[3] << std::endl;
  // casova os po 1 s: T<sekunda>:pocet CAM pre masku 0..7 (oddelene /)
  std::cout << "XGREGORM-HYBT";
  for (auto &e : xghyb::timeline ())
    {
      std::cout << ",T" << e.first << ":";
      for (int m = 0; m < 8; m++)
        std::cout << (m ? "/" : "") << e.second[m];
    }
  std::cout << std::endl;
  // casova os dorucenia po 1 s (xgstats::timeline): T<s>:ul_exp/ul_rx/v2v_exp/v2v_rx/vzorky/nevie/vek_ms/chyba>10m
  std::cout << "XGREGORM-LOSST";
  for (auto &e : xgstats::timeline ())
    {
      std::cout << ",T" << e.first << ":";
      for (int k = 0; k < 8; k++)
        std::cout << (k ? "/" : "") << e.second[k];
    }
  std::cout << std::endl;
  // oneskorenie auto -> auto: W = 802.11p, NR = NR-V2X (prva kopia kazdej CAM)
  std::cout << "XGREGORM-V2VLAT,W_SUM:" << xgstats::v2vLatSum ()[0] << ",W_N:" << xgstats::v2vLatN ()[0]
            << ",NR_SUM:" << xgstats::v2vLatSum ()[2] << ",NR_N:" << xgstats::v2vLatN ()[2] << std::endl;
  // zataz: rozdelenie (vzorka = stanica/bunka za 100 ms) a vykonnost podla kosa zataze odosielatela
  const char *techName[3] = {"W", "LTE", "NR"};
  for (int k = 0; k < 3; k++)
    {
      if (!xgstats::loadFn ()[k])
        continue;
      const xgstats::LoadHist &h = xgstats::loadDist ()[k];
      std::cout << "XGREGORM-LOAD,TECH:" << techName[k] << ",N:" << h.n << ",MEAN:" << (h.n ? h.sum / h.n : 0)
                << ",P50:" << h.pct (0.5) << ",P95:" << h.pct (0.95) << ",P99:" << h.pct (0.99) << ",MAX:" << h.max
                << ",GE30:" << h.atLeast (0.3) << ",GE60:" << h.atLeast (0.6);
      if (k == 1)
        std::cout << ",CELLS:" << xgload::cells.size () << ",ULRB:" << xgload::lteUlRb;
      if (k == 2)
        std::cout << ",SUBCH:" << xgload::nrSubch << ",SLOTS_S:" << xgload::nrSlotsPerSec << ",ESTIMATE:1";
      std::cout << std::endl;
      for (int b = 0; b < xgstats::kLoadBins; b++)
        {
          const xgstats::Perf &p = xgstats::perf ()[k][b];
          if (!p.cam)
            continue;
          std::cout << "XGREGORM-PERF,TECH:" << techName[k] << ",BIN:" << b << ",CAM:" << p.cam << ",EXP:" << p.exp
                    << ",RX:" << p.rx << ",LAT_N:" << p.lat.n << ",LAT_MEAN_MS:" << (p.lat.n ? 1000 * p.lat.sum / p.lat.n : 0)
                    << ",LAT_P50_MS:" << 1000 * p.lat.pct (0.5) << ",LAT_P95_MS:" << 1000 * p.lat.pct (0.95)
                    << ",LAT_P99_MS:" << 1000 * p.lat.pct (0.99) << ",LAT_MAX_MS:" << 1000 * p.lat.max << std::endl;
        }
    }
  {
    const xgstats::Hist &h = xgstats::pir ();
    std::cout << "XGREGORM-PIR,N:" << h.n << ",MEAN_MS:" << (h.n ? 1000 * h.sum / h.n : 0) << ",P50_MS:" << 1000 * h.pct (0.5)
              << ",P95_MS:" << 1000 * h.pct (0.95) << ",P99_MS:" << 1000 * h.pct (0.99) << ",MAX_MS:" << 1000 * h.max << std::endl;
  }
  // dorucenie podla vzdialenosti (po 50 m): B<index>:v2v_exp/v2v_rx/ul_exp/ul_rx
  if (range_stats)
    {
      std::cout << "XGREGORM-RANGE";
      for (int b = 0; b < xgstats::kBins; b++)
        std::cout << ",B" << b << ":" << xgstats::rangeBins ()[0][b] << "/" << xgstats::rangeBins ()[1][b] << "/"
                  << xgstats::rangeBins ()[2][b] << "/" << xgstats::rangeBins ()[3][b];
      std::cout << std::endl;
    }
  return 0;
}
