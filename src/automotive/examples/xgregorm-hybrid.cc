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
#include "ns3/waypoint-mobility-model.h" // xgregorm
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
#include "ns3/three-gpp-channel-model.h" // xgregorm: g_xgCacheTtl (--nr-cache-ttl)
#include "ns3/antenna-module.h"
#include "ns3/buildings-module.h"
#include "ns3/propagation-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/wave-module.h"
#include "ns3/mobility-module.h"
#include "ns3/sumo_xml_parser.h"
#include "ns3/vehicle-visualizer-module.h"
#include "ns3/wifi-module.h"
#include "ns3/interference-helper.h"
#include "ns3/wifi-phy-state-helper.h"
#include <unistd.h>
#include <deque>
#include <set>
#include <unordered_map>

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
std::map<uint32_t, double> prevW, prevN;                                 // predposledne (DCC: priemer dvoch okien)
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
          prevW[n] = curW[n];
          curW[n] = std::min (1.0, busy / 0.1);
          if (xgstats::measuring ())
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
          prevN[n] = curN[n];
          curN[n] = std::min (1.0, used / (nrSubch * nrSlotsPerSec * 0.1));
          if (xgstats::measuring ())
            xgstats::loadDist ()[2].add (curN[n]);
          liveAdd (2, curN[n]);
        }
    }
  if (useL)
    for (uint16_t c : cells)
      {
        curL[c] = std::min (1.0, lteRb[c] / (lteUlRb * 100.0));
        lteRb[c] = 0;
        if (xgstats::measuring ())
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

// xgregorm: vzajomne rusenie 802.11p a NR-V2X v pasme 5.9 GHz (--coex 1). V ns-3 su to dva oddelene kanaly (Yans
// a spektralny kanal NR), vysielanie jednej technologie druhu nevidi (autori to riesia upravenymi subormi jadra
// ns-3, TxTracker - tu bez zasahu do jadra, cez trace zdroje). Pri kazdom vysielani sa prijimacom druhej technologie
// prida cudzi signal:
//  - vykon v prekryti pasiem (802.11p rovnomerne v 10 MHz, NR podla PSD po RB z vysielania) + unik do susedneho
//    pasma (do sirky jedneho kanala) potlaceny o ACLR (--coex-aclr, 3GPP TS 38.101-1 UE: 30 dB);
//  - utlm samostatnym modelom sirenia rovnakeho typu ako kanal 802.11p (budovy, LOS/NLOS, tienenie; bez rychlych
//    unikov), pre dvojicu prepocitany najviac raz za 100 ms (ako stav kanala);
//  - 802.11p: cudzi signal v InterferenceHelper (zhorsi SINR prebiehajuceho prijmu) a CCA podla energie - nad prahom
//    ED (ETSI EN 302 571: -65 dBm) je kanal obsadeny a 802.11p odklada vysielanie (CSMA/CA); pocita sa aj do CBR
//    (> -85 dBm, ETSI TS 102 687);
//  - NR: cudzi signal do interferencie sidelinku (SINR PSCCH/PSSCH); NR (mode 2 bez sensingu) 802.11p neodklada;
//    do odhadu SL-CBR sa zapocitaju prekryte subkanaly, ak je vykon v pasme NR nad -94 dBm.
namespace xgcoex {
bool on = false;
double fW = 5.9e9, bwW = 10e6, fN = 5.89e9, bwN = 40e6, aclr = 1000, ccaW = 0;
Ptr<PropagationLossModel> loss;
struct Sta
{
  uint32_t id;
  Ptr<MobilityModel> mob;
  Ptr<YansWifiPhy> w;
  Ptr<NrSpectrumPhy> n;
};
std::vector<Sta> sta;
std::map<uint32_t, size_t> idx;
std::unordered_map<uint64_t, std::pair<double, double>> gainC; // dvojica -> (cas vypoctu, zisk [dB])
uint64_t toNr = 0, toW = 0, ccaHits = 0, cbrW = 0, cbrN = 0;
double sumNrDbm = 0, sumWDbm = 0;

double
ovl (double a0, double a1, double b0, double b1)
{
  return std::max (0.0, std::min (a1, b1) - std::max (a0, b0));
}

double
gainDb (size_t a, size_t b)
{
  uint64_t key = ((uint64_t) std::min (sta[a].id, sta[b].id) << 32) | std::max (sta[a].id, sta[b].id);
  double now = Simulator::Now ().GetSeconds ();
  auto it = gainC.find (key);
  if (it == gainC.end () || now - it->second.first >= 0.1)
    {
      double g = loss->CalcRxPower (0, sta[a].mob, sta[b].mob);
      it = gainC.insert_or_assign (key, std::make_pair (now, g)).first;
    }
  return it->second.second;
}

void
onWifiTx (size_t s, Ptr<WifiPhy> phy, WifiConstPsduMap psdus, WifiTxVector txv, double txW)
{
  if (!xgload::active.count (sta[s].id))
    return;
  Time d = WifiPhy::CalculateTxDuration (psdus, txv, phy->GetPhyBand ());
  double now = Simulator::Now ().GetSeconds ();
  double w0 = fW - bwW / 2, w1 = fW + bwW / 2, psd = txW / bwW;
  for (size_t j = 0; j < sta.size (); j++)
    {
      if (j == s || !sta[j].n || !xgload::active.count (sta[j].id))
        continue;
      double g = std::pow (10, gainDb (s, j) / 10);
      if (10 * std::log10 (txW * g) + 30 < -110)
        continue; // pod sumom NR
      Ptr<SpectrumValue> v = Create<SpectrumValue> (sta[j].n->GetRxSpectrumModel ());
      double tot = 0;
      int i = 0;
      std::set<int> sub;
      auto b = v->ConstBandsBegin ();
      for (auto it = v->ValuesBegin (); it != v->ValuesEnd (); ++it, ++b, ++i)
        {
          double bw = b->fh - b->fl;
          double in = ovl (b->fl, b->fh, w0, w1);
          double leak = ovl (b->fl, b->fh, w0 - bwW, w0) + ovl (b->fl, b->fh, w1, w1 + bwW);
          *it = psd * g * (in + leak / aclr) / bw;
          tot += *it * bw;
          if (in > 0)
            sub.insert (i / 10); // subkanal = 10 RB (SetSlSubchannelSize)
        }
      if (tot <= 0)
        continue;
      double dbm = 10 * std::log10 (tot) + 30;
      if (dbm < -110)
        continue;
      sta[j].n->GetSlInterferencePointer ()->AddSignal (v, d);
      toNr++;
      sumNrDbm += dbm;
      if (xgload::useN && dbm > -94 && !sub.empty ())
        {
          int slots = std::max (1, (int) std::ceil (d.GetSeconds () * xgload::nrSlotsPerSec));
          xgload::nrRx[sta[j].id].push_back ({now, (int) sub.size () * slots});
          cbrN++;
        }
    }
}

void
onNrTx (Ptr<SpectrumSignalParameters> p)
{
  if (!p->txPhy || !p->txPhy->GetDevice ())
    return;
  auto f = idx.find (p->txPhy->GetDevice ()->GetNode ()->GetId ());
  if (f == idx.end () || !xgload::active.count (f->first))
    return;
  size_t s = f->second;
  double w0 = fW - bwW / 2, w1 = fW + bwW / 2, in = 0, leak = 0;
  auto b = p->psd->ConstBandsBegin ();
  for (auto v = p->psd->ConstValuesBegin (); v != p->psd->ConstValuesEnd (); ++v, ++b)
    if (*v > 0)
      {
        in += *v * ovl (b->fl, b->fh, w0, w1);
        leak += *v * (ovl (b->fl, b->fh, w0 - bwW, w0) + ovl (b->fl, b->fh, w1, w1 + bwW));
      }
  double P = in + leak / aclr;
  if (P <= 0)
    return;
  double now = Simulator::Now ().GetSeconds ();
  for (size_t j = 0; j < sta.size (); j++)
    {
      if (j == s || !sta[j].w || !xgload::active.count (sta[j].id))
        continue;
      double rx = P * std::pow (10, gainDb (s, j) / 10);
      double dbm = 10 * std::log10 (rx) + 30;
      if (dbm < -110)
        continue; // pod sumom 802.11p (10 MHz: -97 dBm)
      RxPowerWattPerChannelBand m;
      m.insert ({std::make_pair (0, 0), rx});
      Ptr<InterferenceHelper> ih = sta[j].w->GetInterferenceHelper ();
      ih->AddForeignSignal (p->duration, m);
      Time busy = ih->GetEnergyDuration (ccaW, std::make_pair (0, 0));
      if (!busy.IsZero ())
        {
          sta[j].w->GetState ()->SwitchMaybeToCcaBusy (busy);
          ccaHits++;
        }
      toW++;
      sumWDbm += dbm;
      if (xgload::useW && dbm > xgstats::kCbrThrDbm)
        {
          xgload::busyW[sta[j].id].push_back ({now, now + p->duration.GetSeconds ()});
          cbrW++;
        }
    }
}
} // namespace xgcoex

// xgregorm: dalsia realna prevadzka popri CAM (zataz kanala + meranie jej dorucenia; obsah sa nekoduje v ASN.1,
// posiela sa len sprava spravnej velkosti - ako model prevadzky 3GPP TR 37.885):
//  RSU:  SPATEM 10 Hz, MAPEM 1 Hz (ETSI TS 103 301, C-ITS Day 1) cez 802.11p aj NR-V2X (ktore RSU ma a nemaju vypadok)
//  auta so senzormi (podiel --cpm-share): CPM podla pravidiel generovania ETSI TS 103 324 - kontrola kazdych 100 ms,
//        objekt (ine auto do --cpm-range, v priamej viditelnosti - budovy ho zakryju) sa zaradi pri prvom zisteni,
//        posune o > 4 m alebo po 1 s od posledneho zaradenia; CPM aspon raz za 1 s; kontajner informacii o senzoroch
//        raz za 1 s; ide hlavnou priamou technologiou auta (adaptivne), pri --policy all kazdou priamou v ceste
//  kazda sprava: + BTP-B 4 B + GeoNet SHB 40 B + podpis ETSI TS 103 097 (ak --security; certifikat raz za 1 s)
namespace xgbg {
int cpmRat = 0; // xgregorm: napad 6

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

uint8_t curObjs = 0;

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
  buf[2] = type == CPM ? curObjs : 0; // pocet objektov v CPM (obsah spravy, velkost paketu sa nemeni)
  std::memcpy (buf + 4, &id, 4);
  std::memcpy (buf + 8, &seq, 4);
  // prijemcovia do prrRange (poloha zo SUMO v okamihu vyslania), auta aj RSU okrem odosielatela, ktore tu
  // technologiu maju (--equip: auto bez nej spravu nedostane, ani ju nema dostat)
  std::set<uint32_t> want[2];
  Vector p = s.mob->GetPosition ();
  for (auto &o : st)
    if (o.first != id && xgload::active.count (o.first) && CalculateDistance (p, o.second.mob->GetPosition ()) <= prrRange)
      for (int k = 0; k < 2; k++)
        if (k == 0 ? (bool) o.second.w : (bool) o.second.nr)
          want[k].insert (o.first);
  static const int kKind[3] = {xgdcc::SPAT, xgdcc::MAP, xgdcc::CPM};
  for (int k = 0; k < 2; k++)
    {
      Ptr<Socket> sock = k == 0 ? s.w : s.nr;
      if (!(techMask & (k == 0 ? xghyb::WIFI : xghyb::NR)) || !sock)
        continue;
      buf[1] = k;
      Ptr<Packet> pkt = Create<Packet> (buf, 12);
      pkt->AddPaddingAtEnd (size - 12);
      // xgregorm: --dcc / --nr-cr - 802.11p cez branu DCC, NR s limitom CR
      if (k == 0)
        xgdcc::wSend (sock, pkt, kKind[type]);
      else
        xgdcc::nrSend (sock, pkt);
      if (!xgstats::measuring ())
        continue; // --warmup: sprava sa posle, ale nepocita
      tx[type][k]++;
      txB[type][k] += size;
      exp_[type][k] += want[k].size ();
      pend[{id, seq, k}] = {now, want[k]};
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
      // xgregorm: --zones - lokalne zhorsenie aj pre SPATEM/MAPEM/CPM
      if (!xgstats::zones ().empty ())
        {
          Vector zp = st[me].mob->GetPosition ();
          if (xgstats::zoneDrop ((int) buf[1], zp.x, zp.y))
            continue;
        }
      if (xgstats::bgRxOn ())
        xgstats::bgRxAdd (me, buf[0], buf[1], id, buf[2]);
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
  // xgregorm: napad 6 - CPM pevnou technologiou; napad 1 - CPM by cakala na brane DCC dlhsie nez rozpocet -> NR
  if (cpmRat && ((cpmRat & xghyb::WIFI) ? s.w : s.nr))
    mask = cpmRat;
  if (xgdcc::offload () > 0 && mask == xghyb::WIFI && s.nr && xgdcc::gateWait (id) > xgdcc::offload ())
    {
      mask = xghyb::NR;
      xgdcc::offloadN () += xgstats::measuring ();
    }
  double now = Simulator::Now ().GetSeconds ();
  // xgregorm: --dcc - T_GenCpm (ETSI TS 103 324) cez 802.11p nie kratsi nez Toff stavu DCC
  if ((mask & xghyb::WIFI) && xgdcc::wOn () && now - s.lastCpm < xgdcc::camGap (id) - 1e-6)
    {
      xgdcc::cnt ()[8]++;
      return;
    }
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
  if (xgstats::measuring ())
    {
      objs += n;
      cpmN++;
    }
  // ITS PDU 8 + management 25 + originating vehicle 15 + perceived object container 2 + 35 B na objekt
  // + informacie o senzoroch (2 senzory) 40 B raz za 1 s
  curObjs = (uint8_t) n;
  if (mask)
    send (s, CPM, 50 + 35 * n + (sensorInfo ? 40 : 0), mask);
}

void
install (Ptr<Node> node, bool rsu, bool sensor, bool spatOn, Ptr<UniformRandomVariable> rnd, int equip = xghyb::WIFI | xghyb::NR)
{
  Station &s = st[node->GetId ()];
  s.node = node;
  s.mob = node->GetObject<MobilityModel> ();
  s.rsu = rsu;
  s.sensor = sensor;
  uint32_t id = node->GetId ();
  if (useW && (equip & xghyb::WIFI))
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
  Ptr<NetDevice> nrDev = useN && (equip & xghyb::NR) ? xghyb::findDevice (node, "ns3::NrUeNetDevice") : nullptr;
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

// xgregorm: ina prevadzka v bunke LTE (smartfony, kamery... - bunka nie je len pre V2X). Kazde UE pozadia
// zastupuje skupinu pouzivatelov: uplink v davkach ON/OFF (exponencialne, priemer 0.5 s / 0.5 s), pocas ON
// 2x priemerna rychlost, pakety 1000 B na server za PGW. Meria sa len to, kolko PRB zaberie (vytazenie bunky).
namespace xglbg {
uint32_t pkt = 1000;
double kbps = 500;
uint64_t txN = 0, phoneN = 0;
Ptr<ExponentialRandomVariable> onT, offT;

// rate [kbit/s] priemerna rychlost; node = auto s telefonmi (--phone-share; vysiela, len kym je auto v mape),
// UINT32_MAX = staticke UE pozadia
void
burst (Ptr<Socket> s, double until, double rate, uint32_t node)
{
  if (node != UINT32_MAX && !xgload::active.count (node))
    return;
  if (Simulator::Now ().GetSeconds () >= until)
    {
      Simulator::Schedule (Seconds (offT->GetValue ()), +[] (Ptr<Socket> s, double rate, uint32_t node) {
        burst (s, Simulator::Now ().GetSeconds () + onT->GetValue (), rate, node);
      }, s, rate, node);
      return;
    }
  s->Send (Create<Packet> (pkt));
  (node == UINT32_MAX ? txN : phoneN)++;
  Simulator::Schedule (Seconds (pkt * 8.0 / (2 * rate * 1000)), &burst, s, until, rate, node);
}
} // namespace xglbg

// xgregorm: vybava aut (--equip): kazde auto ma LTE (telematika, eCall - v EU povinne), priame technologie podla
// trhu: len 802.11p (ITS-G5, napr. VW), len NR-V2X (C-V2X), obe, alebo ziadnu V2X. Rozdelenie podla poradia uzla
// v poole (vlastny prud nahodnych cisel). Bez 802.11p ma auto radio 802.11p v spanku (nic neprijima ani nevysiela),
// bez NR nema NR zariadenie; auto bez V2X nema klienta (CAM neposiela), CPM ostatnych ho vsak zachyti.
namespace xgeq {
std::vector<int> mask;   // index uzla v poole -> xghyb::WIFI | xghyb::NR
bool on = false;
} // namespace xgeq
// xgregorm: telefony v autach (--phone-share): socket na server za PGW podla indexu uzla (nullptr = bez telefonov)
namespace xgphone {
std::vector<Ptr<Socket>> sock;
std::set<uint32_t> started;
Ptr<UniformRandomVariable> start;
double kbps = 100;
uint32_t n = 0;
} // namespace xgphone

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
  int wifi_bcast_rate = 0; // xgregorm: 0 = predvolene ns-3 (najnizsia rychlost 3 Mbit/s)
  std::string zones_s;     // xgregorm: lokalne zhorsenie "rsu:r:t0:t1:W|N:p+..."
  double wifi_rx_min = 0;  // xgregorm: prah detekcie preambuly 802.11p [dBm]; 0 = predvolene ns-3 (-82)
  bool clean_policy = false;   // xgregorm: jedna priama technologia na CAM, bez kopii a beaconu
  uint32_t init_main = 0;
  double probe_hz = 0, cell_dist = 150;
  std::string feat_log, pred_model;
  double dcc_offload_ms = 0, dup_near = 0, geo_rat = 0;
  bool oracle = false, bandit = false, bandit_probe = false;
  std::string pred_gbm;
  bool nb_equip = false, rsu_route = false;
  std::string rsu_equip;   // xgregorm: napad 3: vybava RSU po rade, opakuje sa, napr. "W,N,WN"
  std::string cpm_rat;     // xgregorm: napad 6: CPM vzdy touto technologiou (W, N)
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
  bool load_rat = false;                                     // xgregorm: vyber technologie podla zataze
  uint32_t lte_bg_ue = 0;                                    // xgregorm: ina prevadzka v bunke LTE
  double cbr_high = 0.3, cbr_low = 0.2, lte_high = 0.6, load_period = 1.0;
  bool qos_rat = false;                                      // xgregorm: vyber technologie podla namerannej kvality
  double qos_margin = 0.05, lat_budget = 0.1;
  double nr_max_loss = 0; // xgregorm: --nr-max-loss
  bool nr_fading = true;  // xgregorm: --nr-fading
  bool pred_rat = false;                                     // xgregorm: prediktivny vyber (mapa kvality + trasa)
  double pred_horizon = 3;
  double coex_aclr = 30;                                     // xgregorm: vzajomne rusenie 802.11p a NR
  uint16_t coex_wifi_ch = 180;
  double nr_bw_mhz = 0;
  std::string equip;                                         // xgregorm: vybava aut, napr. "W:0.4,N:0.2,WN:0.2,NONE:0.2"
  double phone_share = 0, phone_kbps = 100;                  // xgregorm: telefony v autach (LTE uplink)
  uint16_t lte_bw = 0;                                       // xgregorm: sirka pasma bunky LTE [PRB]
  bool dcc = false, nr_cr = false;                           // xgregorm: riadenie zahltenia
  double core_delay_ms = 5; // PGW -> server, ako autori v v2i-trafficManager-LTE
  // NR-V2X (hodnoty z v2v-emergencyVehicleAlert-nrv2x autorov)
  double nr_freq = 5.89e9;
  uint16_t nr_bandwidth = 400;
  double nr_tx_power = 23;
  uint16_t nr_numerology = 2;
  uint8_t nr_mcs = 14;
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
  cmd.AddValue ("load-rat","Adaptive: choose the main direct technology by load (802.11p -> NR-V2X under congestion, "
                "LTE backup only when the cell is not overloaded); needs --load-stats",load_rat);
  cmd.AddValue ("cbr-high","Load rule: CBR from which a direct channel is congested (ETSI DCC 0.3)",cbr_high);
  cmd.AddValue ("cbr-low","Load rule: CBR of 802.11p below which vehicles return from NR-V2X",cbr_low);
  cmd.AddValue ("lte-high","Load rule: LTE cell UL PRB usage above which LTE is not used as a backup",lte_high);
  cmd.AddValue ("load-period","Load rule: re-evaluation period of the main technology [s]",load_period);
  cmd.AddValue ("qos-rat","Quality rule: main direct technology by the measured reliability of the neighbours' CAMs "
                "(delivered within --lat-budget, per technology); needs --load-stats",qos_rat);
  cmd.AddValue ("qos-margin","Quality rule: required reliability advantage of the other technology",qos_margin);
  cmd.AddValue ("lat-budget","Quality rule: CAM latency budget [s] (later = lost)",lat_budget);
  cmd.AddValue ("nr-fading","NR sidelink 3GPP fast fading (false = path loss and shadowing only, large scenarios)",nr_fading);
  cmd.AddValue ("nr-max-loss","Large scenarios: NR sidelink receivers with path loss above X dB do not get the signal "
                "at all (0 = off, as before)",nr_max_loss);
  cmd.AddValue ("nr-cache-ttl","Large scenarios: forget 3GPP channel realizations of pairs unused for X s (0 = off)",ns3::g_xgCacheTtl);
  cmd.AddValue ("clock-err","Quality rule: clock error of each station, std. dev. [s] (0 = exact GNSS time)",xgstats::clockErr ());
  cmd.AddValue ("pred-rat","Quality rule with prediction: reliability and load over the next --pred-horizon s from a "
                "shared quality map (measurements of all vehicles, per cell) along the route (implies --qos-rat)",pred_rat);
  cmd.AddValue ("pred-horizon","Prediction horizon [s] (one route point per second)",pred_horizon);
  cmd.AddValue ("rem-cell","Quality map: cell size [m]",xgstats::remCell ());
  cmd.AddValue ("rem-tau","Quality map: forgetting time constant [s]",xgstats::remTau ());
  cmd.AddValue ("coex","Mutual interference of 802.11p and NR-V2X in the 5.9 GHz band (overlap of the bands + "
                "adjacent channel leakage); 802.11p on --coex-wifi-ch",xgcoex::on);
  cmd.AddValue ("coex-aclr","Coexistence: adjacent channel leakage ratio of both transmitters [dB]",coex_aclr);
  cmd.AddValue ("coex-wifi-ch","Coexistence: 802.11p channel (180 = 5900 MHz, ETSI ITS-G5 control channel)",coex_wifi_ch);
  cmd.AddValue ("nr-freq","NR-V2X sidelink centre frequency [Hz]",nr_freq);
  cmd.AddValue ("nr-bw","NR-V2X sidelink bandwidth [MHz] (0 = 40, authors)",nr_bw_mhz);
  cmd.AddValue ("equip","Equipment of the vehicles: shares of W (802.11p only), N (NR-V2X only), WN (both), NONE (no V2X), "
                "e.g. W:0.4,N:0.2,WN:0.2,NONE:0.2; every vehicle has LTE; empty = all WN",equip);
  cmd.AddValue ("phone-share","Share of vehicles with passengers' phones / infotainment using the LTE uplink of the "
                "vehicle (bursty ON/OFF); CAMs over LTE then use a dedicated V2X bearer (QCI 79)",phone_share);
  cmd.AddValue ("phone-kbps","Mean uplink rate of the phones in one vehicle [kbit/s]",phone_kbps);
  cmd.AddValue ("lte-bw","LTE cell bandwidth [PRB] (25 = 5 MHz ns-3 default, 50 = 10 MHz, 100 = 20 MHz; 0 = default)",lte_bw);
  cmd.AddValue ("dcc","802.11p reactive DCC (ETSI TS 102 687): gatekeeper after every frame + T_GenCam_DCC / T_GenCpm",dcc);
  cmd.AddValue ("nr-cr","NR-V2X congestion control: CR limit by SL-CBR (ETSI TS 103 574), copies over the limit are dropped",nr_cr);
  cmd.AddValue ("lte-bg-ue","Other (non-V2X) LTE users: number of static UEs, each a group of phones with bursty uplink",lte_bg_ue);
  cmd.AddValue ("lte-bg-kbps","Mean uplink rate of one background LTE UE [kbit/s] (ON/OFF 0.5 s / 0.5 s, 2x during ON)",xglbg::kbps);
  cmd.AddValue ("core-delay","One-way delay PGW -> server [ms]",core_delay_ms);
  cmd.AddValue ("nr-tx-power","NR-V2X tx power [dBm]",nr_tx_power);
  cmd.AddValue ("nr-mcs","NR-V2X sidelink MCS",nr_mcs);
  cmd.AddValue ("tx-power", "802.11p transmission power [dBm]", txPower);
  cmd.AddValue ("datarate", "802.11p channel data rate [Mbit/s]", datarate);
  cmd.AddValue ("zones","Local degradation: rsuIndex:radius:t0:t1:W|N:loss+... (reception of that technology is lost with this probability)",zones_s);
  cmd.AddValue ("wifi-rx-min","802.11p preamble detection threshold [dBm]; 0 = ns-3 default (-82). EN 302 663 tab. 1 at 6 Mbit/s: -88",wifi_rx_min);
  cmd.AddValue ("clean","Clean policy: one direct technology per CAM, no copies, no RSU beacon, LTE only far from the RSU",clean_policy);
  cmd.AddValue ("init-main","Clean policy: initial main technology (0 = 802.11p, 1 = NR, 2 = random per vehicle)",init_main);
  cmd.AddValue ("probe-hz","Clean policy: CAMs per second also on the other technology (to measure it)",probe_hz);
  cmd.AddValue ("cell-dist","Clean policy: LTE copy on an approach farther than this from the RSU [m]",cell_dist);
  cmd.AddValue ("rem-period","Quality map: report period over LTE [s] (0 = instant shared memory, idealised)",xgstats::remPeriod ());
  cmd.AddValue ("dcc-offload","Clean policy: CAM/CPM over NR when the DCC gate of 802.11p would hold it longer than this [ms]; 0 = off",dcc_offload_ms);
  cmd.AddValue ("nb-equip","Clean policy: choose the technology by the equipment of the neighbours",nb_equip);
  cmd.AddValue ("rsu-equip","Equipment of the RSUs in turn, repeated, e.g. W,N,WN (empty = all both)",rsu_equip);
  cmd.AddValue ("rsu-route","Clean policy: on an approach use the technology of the next RSU",rsu_route);
  cmd.AddValue ("dup-near","Clean policy: CAM copy on the other technology within this distance of the next traffic light [m]",dup_near);
  cmd.AddValue ("oracle","Reference: vehicles know the --zones exactly (with --qos-rat; look-ahead = --pred-horizon s)",oracle);
  cmd.AddValue ("geo-rat","Clean policy: CAM over NR within this distance of the next traffic light [m]",geo_rat);
  cmd.AddValue ("cpm-rat","CPM always on this technology (W or N) if the vehicle has it; empty = main technology",cpm_rat);
  cmd.AddValue ("bandit","Learning while driving (needs --clean and --qos-rat): reliability per situation from the vehicle's own reception",bandit);
  cmd.AddValue ("feat-log","Write the prediction dataset (CSV) to this file",feat_log);
  cmd.AddValue ("bandit-probe","With --bandit keep the regular probes of the other technology",bandit_probe);
  cmd.AddValue ("pred-gbm","Learned trees (xgregorm_gbm_export.py) replacing the current measurement in the quality rule; needs --clean and --qos-rat",pred_gbm);
  cmd.AddValue ("pred-model","Learned prediction model file (one line of coefficients per technology)",pred_model);
  cmd.AddValue ("wifi-bcast-rate", "802.11p rate of broadcast frames [Mbit/s]: 3, 6, 12 (0 = ns-3 default, the lowest rate, 3 Mbit/s; "
                "--datarate does not apply to broadcast)", wifi_bcast_rate);
  cmd.AddValue ("sim-time", "Total duration of the simulation [s]", simTime);
  cmd.AddValue ("warmup", "Statistics start at this time [s] (CAMs sent earlier are not counted; behaviour unchanged)",
                xgstats::warmup ());
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
  // xgregorm: dalsie realne vplyvy pri --channel urban (pri inom kanali bez zmeny = ako autori)
  bool w_fading = true;
  cmd.AddValue ("fading","--channel urban: Nakagami-m fast fading on 802.11p (m = 3 below 50 m, 1.5 to 150 m, 1 beyond; "
                "measurements at 5.9 GHz); NR has 3GPP fading of its own",w_fading);
  double nr_nf = -1;
  cmd.AddValue ("nr-noise-figure","NR-V2X UE noise figure [dB]; -1 = 9 with --channel urban (3GPP TR 37.885), "
                "otherwise 5 (5G-LENA default, authors)",nr_nf);
  double lte_iot = -1;
  cmd.AddValue ("lte-iot","Uplink interference over thermal from neighbouring LTE cells [dB], added to the eNB noise "
                "figure (one simulated cell has no neighbours); -1 = 3 with --channel urban, otherwise 0",lte_iot);
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
  xgstats::clockSeed () = (unsigned long) sumo_seed; // xgregorm: --clock-err, odchylky hodin podla seedu
  if (pred_rat)
    qos_rat = true;
  if (clean_policy)
    beacon_ms = 0;
  if (wifi_rx_min != 0)
    Config::SetDefault ("ns3::ThresholdPreambleDetectionModel::MinimumRssi", DoubleValue (wifi_rx_min));
  xgdcc::offload () = dcc_offload_ms / 1000.0;
  xgbg::cpmRat = cpm_rat == "W" ? xghyb::WIFI : cpm_rat == "N" ? xghyb::NR : 0;
  if (nb_equip)
    qos_rat = true; // znacky v CAM a zoznam susedov; pravidlo kvality sa pri nb_equip nepouzije
  if (!rsu_equip.empty ())
    {
      std::stringstream rs (rsu_equip);
      std::string it;
      while (std::getline (rs, it, ','))
        xgstats::rsuEqIdx ().push_back (it == "W" ? xghyb::WIFI : it == "N" ? xghyb::NR : xghyb::WIFI | xghyb::NR);
    }
  if (!feat_log.empty ())
    {
      xgstats::featLog () = new std::ofstream (feat_log);
      *xgstats::featLog () << "t,sid,main,k,r,qexp,cbr,remr,remn,remc,nb,nbk,nbf,v,appr,dist,cumexp,cumok,x,y,spatr,mapage,cpmn,spatn,cpmobj,sfresh,tsw,isg,nb30,nbstop,nbclose,nbfar,nbf30,dv,tstop\n";
      xgstats::bgRxOn () = true;
    }
  if (!pred_gbm.empty ())
    {
      std::string err = xgstats::gbm ().load (pred_gbm, xgstats::kGbmNames);
      if (!err.empty ())
        NS_FATAL_ERROR ("--pred-gbm: " << err);
      xgstats::bgRxOn () = true;
    }
  if (!pred_model.empty ())
    {
      std::ifstream mf (pred_model);
      if (!mf)
        NS_FATAL_ERROR ("Cannot read --pred-model " << pred_model);
      std::string line;
      for (int k = 0; k < 2 && std::getline (mf, line); k++)
        {
          std::stringstream ls (line);
          double v;
          while (ls >> v)
            xgstats::predCoef ()[k].push_back (v);
        }
    }
  if (nr_bw_mhz > 0)
    nr_bandwidth = (uint16_t) std::lround (nr_bw_mhz * 10); // jednotky 100 kHz
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
  if (!zones_s.empty ())
    {
      std::stringstream zs (zones_s);
      std::string it;
      xgstats::zoneRnd () = CreateObject<UniformRandomVariable> ();
      xgstats::zoneRnd ()->SetStream (3300);
      while (std::getline (zs, it, '+'))
        {
          int ri;
          double r, t0, t1, p;
          char w, c;
          std::stringstream one (it);
          if (!(one >> ri >> c >> r >> c >> t0 >> c >> t1 >> c >> w >> c >> p) || ri < 0 || ri >= (int) rsuData.size ())
            NS_FATAL_ERROR ("--zones: expected rsu:radius:t0:t1:W|N:loss, got " << it);
          xgstats::zones ().push_back ({std::get<1> (rsuData[ri]), std::get<2> (rsuData[ri]), r, t0, t1, p, w == 'N' ? 1 : 0});
        }
    }
  xmlFreeDoc(rou_xml_file);
  xmlCleanupParser();
  if(numberOfNodes==-1)
    NS_FATAL_ERROR("Fatal error: cannot gather the number of vehicles from the specified XML file: "<<path);

  ns3::Time simulationTime (ns3::Seconds(simTime));

  /*** 1. Uzly ***/
  NodeContainer obuNodes;
  obuNodes.Create (numberOfNodes);
  // xgregorm: --equip - vybava kazdeho auta (index v poole)
  xgeq::mask.assign (numberOfNodes, xghyb::WIFI | xghyb::NR);
  if (!equip.empty ())
    {
      xgeq::on = true;
      std::map<std::string, double> sh;
      std::stringstream ss (equip);
      std::string item;
      while (std::getline (ss, item, ','))
        {
          size_t c = item.find (':');
          if (c == std::string::npos)
            NS_FATAL_ERROR ("--equip: expected TYPE:share, got " << item);
          sh[item.substr (0, c)] = std::stod (item.substr (c + 1));
        }
      double tot = 0;
      for (auto &e : sh)
        {
          if (e.first != "W" && e.first != "N" && e.first != "WN" && e.first != "NONE")
            NS_FATAL_ERROR ("--equip: types are W, N, WN, NONE");
          tot += e.second;
        }
      Ptr<UniformRandomVariable> er = CreateObject<UniformRandomVariable> ();
      er->SetStream (3200);
      int cnt[4] = {0, 0, 0, 0};
      for (int i = 0; i < numberOfNodes; i++)
        {
          double u = er->GetValue () * tot, acc = 0;
          int m = xghyb::WIFI | xghyb::NR;
          for (auto t : {std::make_pair ("W", (int) xghyb::WIFI), std::make_pair ("N", (int) xghyb::NR),
                         std::make_pair ("WN", (int) (xghyb::WIFI | xghyb::NR)), std::make_pair ("NONE", 0)})
            {
              acc += sh.count (t.first) ? sh[t.first] : 0;
              if (u < acc)
                {
                  m = t.second;
                  break;
                }
            }
          xgeq::mask[i] = m;
          cnt[m == xghyb::WIFI ? 0 : m == xghyb::NR ? 1 : m ? 2 : 3]++;
        }
      std::cout << "XGREGORM-EQUIP,W:" << cnt[0] << ",N:" << cnt[1] << ",WN:" << cnt[2] << ",NONE:" << cnt[3] << std::endl;
    }
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
  // xgregorm: --coex - 802.11p na skutocnom kanali (prekrytie s NR sa pocita z frekvencii) a prah CCA podla energie
  // pre cudzie signaly podla ETSI EN 302 571 (-65 dBm); bez --coex nastavenie autorov
  if (xgcoex::on)
    {
      wifiPhy.Set ("ChannelSettings", StringValue ("{" + std::to_string (coex_wifi_ch) + ", 10, BAND_5GHZ, 0}"));
      wifiPhy.Set ("CcaEdThreshold", DoubleValue (-65));
    }
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
      // xgregorm: rychle uniky (viaccestne sirenie) - TR 37.885 dava len stredny utlm + tienenie; NR ma fading
      // v spektralnom modeli 3GPP, 802.11p (Yans) bez neho prijima kazdy ramec s rovnakym vykonom. Nakagami-m
      // (m klesa so vzdialenostou: blizko silna priama zlozka, dalej ~Rayleigh); ten isty retazec pouziva aj meranie CBR
      if (w_fading)
        {
          Ptr<NakagamiPropagationLossModel> nak = CreateObject<NakagamiPropagationLossModel> ();
          nak->SetAttribute ("Distance1", DoubleValue (50));
          nak->SetAttribute ("Distance2", DoubleValue (150));
          nak->SetAttribute ("m0", DoubleValue (3.0));
          nak->SetAttribute ("m1", DoubleValue (1.5));
          nak->SetAttribute ("m2", DoubleValue (1.0));
          nak->AssignStreams (2020);
          loss->SetNext (nak);
        }
      xgload::wLoss = loss;
    }
  else if (channel_model != "authors")
    NS_FATAL_ERROR ("--channel must be authors, 3gpp or urban");
  wifiPhy.SetChannel (channel);
  wifiPhy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11);
  NqosWaveMacHelper wifi80211pMac = NqosWaveMacHelper::Default ();
  Wifi80211pHelper wifi80211p = Wifi80211pHelper::Default ();
  // xgregorm: vsetky spravy (CAM, CPM, SPATEM...) su broadcast a idu rychlostou NonUnicastMode, nie DataMode
  if (wifi_bcast_rate)
    {
      if (wifi_bcast_rate != 3 && wifi_bcast_rate != 6 && wifi_bcast_rate != 12)
        NS_FATAL_ERROR ("--wifi-bcast-rate must be 3, 6 or 12");
      wifi80211p.SetRemoteStationManager ("ns3::ConstantRateWifiManager", "DataMode", StringValue (datarate_config),
                                          "ControlMode", StringValue (datarate_config), "NonUnicastMode",
                                          StringValue ("OfdmRate" + std::to_string (wifi_bcast_rate) + "MbpsBW10MHz"));
      xgdcc::bitsPerSymbol () = wifi_bcast_rate * 8.0;
    }
  else
    wifi80211p.SetRemoteStationManager ("ns3::ConstantRateWifiManager", "DataMode", StringValue (datarate_config), "ControlMode", StringValue (datarate_config));
  wifi80211p.Install (wifiPhy, wifi80211pMac, obuNodes);
  wifi80211p.Install (wifiPhy, wifi80211pMac, rsuNodes);
  // xgregorm: --equip - auto bez 802.11p: radio v spanku (zariadenie musi byt, GeoNet z neho berie adresu)
  for (uint32_t n = 0; n < obuNodes.GetN (); ++n)
    if (!(xgeq::mask[n] & xghyb::WIFI))
      DynamicCast<WifiNetDevice> (obuNodes.Get (n)->GetDevice (0))->GetPhy ()->SetSleepMode ();
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
  if (xgcoex::on && !(use_80211p && use_nr))
    {
      std::cout << "XGREGORM-COEX,off (needs 802.11p and NR)" << std::endl;
      xgcoex::on = false;
    }
  if (xgcoex::on)
    {
      // samostatny model sirenia pre rusenie medzi technologiami: rovnaky typ ako kanal 802.11p, bez rychlych unikov
      if (urban || channel_model == "3gpp")
        {
          Ptr<ChannelConditionModel> ccm;
          Ptr<ThreeGppPropagationLossModel> l;
          if (urban)
            {
              ccm = CreateObject<ThreeGppV2vUrbanChannelConditionModel> ();
              l = CreateObject<ThreeGppV2vUrbanPropagationLossModel> ();
            }
          else
            {
              ccm = CreateObject<ThreeGppV2vHighwayChannelConditionModel> ();
              l = CreateObject<ThreeGppV2vHighwayPropagationLossModel> ();
            }
          ccm->SetAttribute ("UpdatePeriod", TimeValue (MilliSeconds (urban ? 100 : 0)));
          ccm->AssignStreams (3000);
          l->SetAttribute ("Frequency", DoubleValue (5.9e9));
          l->SetAttribute ("ShadowingEnabled", BooleanValue (urban));
          l->SetChannelConditionModel (ccm);
          l->AssignStreams (3010);
          xgcoex::loss = l;
        }
      else
        xgcoex::loss = CreateObject<LogDistancePropagationLossModel> ();
      xgcoex::aclr = std::pow (10, coex_aclr / 10);
      xgcoex::ccaW = std::pow (10, (-65 - 30) / 10.0);
      for (NodeContainer *nc : {&obuNodes, &rsuNodes})
        for (uint32_t n = 0; n < nc->GetN (); ++n)
          {
            Ptr<Node> node = nc->Get (n);
            Ptr<YansWifiPhy> phy = DynamicCast<YansWifiPhy> (DynamicCast<WifiNetDevice> (node->GetDevice (0))->GetPhy ());
            xgcoex::idx[node->GetId ()] = xgcoex::sta.size ();
            bool hasW = nc == &rsuNodes || (xgeq::mask[n] & xghyb::WIFI);
            xgcoex::sta.push_back ({node->GetId (), node->GetObject<MobilityModel> (), hasW ? phy : nullptr, nullptr});
            phy->TraceConnectWithoutContext ("PhyTxPsduBegin", MakeBoundCallback (&xgcoex::onWifiTx, xgcoex::sta.size () - 1,
                                                                                  DynamicCast<WifiPhy> (phy)));
          }
      // xgregorm: prvy uzol s 802.11p (auto c. 0 ho pri --equip nemusi mat; RSU ho ma vzdy)
      Ptr<WifiPhy> p0;
      for (auto &st0 : xgcoex::sta)
        if (st0.w)
          {
            p0 = st0.w;
            break;
          }
      xgcoex::fW = p0->GetFrequency () * 1e6;
      xgcoex::bwW = p0->GetChannelWidth () * 1e6;
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
      // xgregorm: rusenie zo susednych buniek (IoT) ako zvyseny sum eNB - jedna simulovana bunka susedov nema
      if (lte_iot < 0)
        lte_iot = urban ? 3 : 0;
      if (lte_iot > 0)
        Config::SetDefault ("ns3::LteEnbPhy::NoiseFigure", DoubleValue (5.0 + lte_iot));
      // xgregorm: --lte-bw - sirka pasma bunky (ns-3 predvolene 25 PRB = 5 MHz; mestske bunky typicky 10-20 MHz)
      if (lte_bw > 0)
        {
          Config::SetDefault ("ns3::LteEnbNetDevice::UlBandwidth", UintegerValue (lte_bw));
          Config::SetDefault ("ns3::LteEnbNetDevice::DlBandwidth", UintegerValue (lte_bw));
        }
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
      // xgregorm: UE pozadia (--lte-bg-ue) - staticke, nahodne v oblasti RSU +-100 m, 1.5 m
      NodeContainer bgNodes;
      NetDeviceContainer bgDevs;
      Ptr<UniformRandomVariable> pr; // len pri pozadi (inak by posunul automaticke cisla prudov)
      if (lte_bg_ue > 0)
        {
          bgNodes.Create (lte_bg_ue);
          pr = CreateObject<UniformRandomVariable> ();
          pr->SetStream (3100);
          double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
          for (auto &r : rsuData)
            {
              x0 = std::min<double> (x0, std::get<1> (r) - 100);
              x1 = std::max<double> (x1, std::get<1> (r) + 100);
              y0 = std::min<double> (y0, std::get<2> (r) - 100);
              y1 = std::max<double> (y1, std::get<2> (r) + 100);
            }
          Ptr<ListPositionAllocator> bgPos = CreateObject<ListPositionAllocator> ();
          for (uint32_t i = 0; i < lte_bg_ue; i++)
            bgPos->Add (Vector (pr->GetValue (x0, x1), pr->GetValue (y0, y1), 1.5));
          MobilityHelper bgMob;
          bgMob.SetPositionAllocator (bgPos);
          bgMob.Install (bgNodes);
          if (urban)
            BuildingsHelper::Install (bgNodes);
          internet.Install (bgNodes);
          bgDevs = lteHelper->InstallUeDevice (bgNodes);
        }
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
      if ((lte_bg_ue > 0 || phone_share > 0) && !xglbg::onT)
        {
          xglbg::onT = CreateObject<ExponentialRandomVariable> ();
          xglbg::onT->SetAttribute ("Mean", DoubleValue (0.5));
          xglbg::onT->SetStream (3110);
          xglbg::offT = CreateObject<ExponentialRandomVariable> ();
          xglbg::offT->SetAttribute ("Mean", DoubleValue (0.5));
          xglbg::offT->SetStream (3111);
        }
      if (lte_bg_ue > 0)
        {
          epcHelper->AssignUeIpv4Address (bgDevs);
          for (uint32_t u = 0; u < bgNodes.GetN (); ++u)
            {
              Ptr<LteUeNetDevice> ue = DynamicCast<LteUeNetDevice> (bgDevs.Get (u));
              if (xgload::useL) // PRB pozadia patria do vytazenia bunky
                ue->GetPhy ()->TraceConnectWithoutContext (
                  "ReportUlPhyResourceBlocks", MakeBoundCallback (+[] (Ptr<LteUeNetDevice> d, uint16_t, const std::vector<int> &rbs) {
                    xgload::lteRb[d->GetRrc ()->GetCellId ()] += rbs.size ();
                  }, ue));
              Ptr<Ipv4> ipv4 = bgNodes.Get (u)->GetObject<Ipv4> ();
              ipv4RoutingHelper.GetStaticRouting (ipv4)->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (),
                                                                           ipv4->GetInterfaceForDevice (bgDevs.Get (u)));
              Ptr<Socket> so = Socket::CreateSocket (bgNodes.Get (u), UdpSocketFactory::GetTypeId ());
              so->Bind ();
              so->Connect (InetSocketAddress (serverAddr, 5001));
              // start po pripojeni (1-2 s), zaciname vo faze OFF
              Simulator::Schedule (Seconds (1.0 + pr->GetValue ()), &xglbg::burst, so, 0.0, xglbg::kbps, UINT32_MAX);
            }
        }
      if (lte_bg_ue > 0 || phone_share > 0 || xgstats::remPeriod () > 0)
        {
          // prijimac na serveri (inak by server posielal ICMP port unreachable)
          Ptr<Socket> sink = Socket::CreateSocket (server, UdpSocketFactory::GetTypeId ());
          sink->Bind (InetSocketAddress (Ipv4Address::GetAny (), 5001));
          sink->SetRecvCallback (MakeCallback (+[] (Ptr<Socket> s) { while (s->Recv ()) {} }));
        }
      // xgregorm: --phone-share - telefony pasazierov / infotainment v aute: uplink ON/OFF cez LTE zariadenie auta
      // (realne je to iny UE v tej istej bunke; tu na UE auta, aby sa neprekrocil limit UE bunky ~320); spusti sa
      // pri vstupe auta do mapy (setupNewNode)
      if (phone_share > 0)
        {
          Ptr<UniformRandomVariable> phr = CreateObject<UniformRandomVariable> ();
          phr->SetStream (3210);
          xgphone::sock.assign (obuNodes.GetN (), nullptr);
          for (uint32_t u = 0; u < obuNodes.GetN (); ++u)
            if (phr->GetValue () < phone_share)
              {
                Ptr<Socket> so = Socket::CreateSocket (obuNodes.Get (u), UdpSocketFactory::GetTypeId ());
                so->Bind ();
                so->Connect (InetSocketAddress (serverAddr, 5001));
                xgphone::sock[u] = so;
                xgphone::n++;
              }
          xgphone::kbps = phone_kbps;
          xgphone::start = CreateObject<UniformRandomVariable> ();
          xgphone::start->SetStream (3211);
        }
      for (uint32_t u = 0; u < obuNodes.GetN (); ++u)
        {
          // LTE je jedina cesta do siete (802.11p ide cez PacketSocket/GeoNet, NR len multicast 225.0.0.0)
          Ptr<Ipv4> ipv4 = obuNodes.Get (u)->GetObject<Ipv4> ();
          ipv4RoutingHelper.GetStaticRouting (ipv4)->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (),
                                                                       ipv4->GetInterfaceForDevice (ueDevs.Get (u)));
        }
      if (n_enb == 1)
        {
          lteHelper->Attach (ueDevs, enbDevs.Get (0));
          if (lte_bg_ue > 0)
            lteHelper->Attach (bgDevs, enbDevs.Get (0));
        }
      else
        {
          if (lte_bg_ue > 0)
            lteHelper->Attach (bgDevs); // najsilnejsia bunka (staticke UE)
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
      // xgregorm: pri telefonoch v aute idu CAM (UDP 9 na server) vlastnym V2X bearerom (QCI 79, 3GPP TS 23.203) -
      // inak by cakali v tej istej fronte RLC za datami telefonov
      if (phone_share > 0)
        {
          Ptr<EpcTft> tft = Create<EpcTft> ();
          EpcTft::PacketFilter pf;
          pf.direction = EpcTft::UPLINK;
          pf.remotePortStart = pf.remotePortEnd = 9;
          tft->Add (pf);
          lteHelper->ActivateDedicatedEpsBearer (ueDevs, EpsBearer (EpsBearer::NGBR_V2X), tft);
        }
    }

  /*** 5. NR-V2X sidelink (mode 2, bez gNB): auta aj RSU; nastavenie ako v2v-emergencyVehicleAlert-nrv2x ***/
  if (use_nr)
    {
      NodeContainer slNodes;
      // xgregorm: --equip - NR zariadenie len auta s NR-V2X (RSU vzdy)
      for (uint32_t n = 0; n < obuNodes.GetN (); ++n)
        if (xgeq::mask[n] & xghyb::NR)
          slNodes.Add (obuNodes.Get (n));
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
      // xgregorm: --nr-fading 0 - bez rychlych unikov 3GPP (len utlm + tienenie, bez matice kanala pre dvojicu);
      // len pre velke scenare (1000 aut), kde matice kanala zaberu najviac pamate a casu. Predvolene 1 = ako doteraz.
      if (nr_fading)
        nrHelper->InitializeOperationBand (&bandSl);
      else
        nrHelper->InitializeOperationBand (&bandSl, NrHelper::INIT_PROPAGATION | NrHelper::INIT_CHANNEL);
      BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps ({bandSl});
      // xgregorm: --nr-max-loss - prijimac, ku ktoremu je utlm (bez rychlych unikov) vacsi nez X dB, signal vobec nedostane
      // (ani ako rusenie): pri 23 dBm a X = 130 je to pod -107 dBm, 15 dB pod sumom kanala 20 MHz. Pre velke scenare
      // (1000 aut), 0 = vypnute (povodne spravanie, pocita sa kazda dvojica).
      if (nr_max_loss > 0)
        for (auto &b : allBwps)
          b.get ()->m_channel->SetAttribute ("MaxLossDb", DoubleValue (nr_max_loss));

      nrHelper->SetUeAntennaAttribute ("NumRows", UintegerValue (1));
      nrHelper->SetUeAntennaAttribute ("NumColumns", UintegerValue (2));
      nrHelper->SetUeAntennaAttribute ("AntennaElement", PointerValue (CreateObject<IsotropicAntennaModel> ()));
      nrHelper->SetUePhyAttribute ("TxPower", DoubleValue (nr_tx_power));
      // xgregorm: sumove cislo UE podla 3GPP TR 37.885 (9 dB); 5G-LENA ma predvolene 5 dB
      if (nr_nf < 0)
        nr_nf = urban ? 9 : 5;
      nrHelper->SetUePhyAttribute ("NoiseFigure", DoubleValue (nr_nf));
      nrHelper->SetUeMacAttribute ("EnableSensing", BooleanValue (false)); // autori: bez sledovania obsadenosti (so sensingom nepouzitelne pomale)
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
      // xgregorm: --nr-cr - vlastne obsadenie kanala (CR): kazdy vlastny PSSCH = subkanaly (10 RB) x sloty
      if (nr_cr)
        {
          xgdcc::nrCap () = xgload::nrSubch * xgload::nrSlotsPerSec;
          bandSl.GetBwpAt (0, 0)->m_channel->TraceConnectWithoutContext ("TxSigParams", MakeCallback (+[] (Ptr<SpectrumSignalParameters> p) {
            if (!DynamicCast<NrSpectrumSignalParametersSlDataFrame> (p) || !p->txPhy || !p->txPhy->GetDevice ())
              return;
            int rb = 0;
            for (auto v = p->psd->ConstValuesBegin (); v != p->psd->ConstValuesEnd (); ++v)
              rb += *v > 0;
            double slots = std::max (1.0, std::round (p->duration.GetSeconds () * xgload::nrSlotsPerSec));
            xgdcc::nrUsed (p->txPhy->GetDevice ()->GetNode ()->GetId (), std::ceil (rb / 10.0) * slots);
          }));
        }
      if (xgcoex::on)
        {
          for (uint32_t n = 0; n < slDevs.GetN (); ++n)
            xgcoex::sta[xgcoex::idx.at (slDevs.Get (n)->GetNode ()->GetId ())].n =
                DynamicCast<NrUeNetDevice> (slDevs.Get (n))->GetPhy (0)->GetSpectrumPhy ();
          bandSl.GetBwpAt (0, 0)->m_channel->TraceConnectWithoutContext ("TxSigParams", MakeCallback (&xgcoex::onNrTx));
          xgcoex::fN = nr_freq;
          xgcoex::bwN = nr_bandwidth * 1e5;
          std::cout << "XGREGORM-COEX,W_MHZ:" << xgcoex::fW / 1e6 << "/" << xgcoex::bwW / 1e6 << ",NR_MHZ:" << xgcoex::fN / 1e6
                    << "/" << xgcoex::bwN / 1e6 << ",OVERLAP_MHZ:"
                    << xgcoex::ovl (xgcoex::fW - xgcoex::bwW / 2, xgcoex::fW + xgcoex::bwW / 2, xgcoex::fN - xgcoex::bwN / 2,
                                    xgcoex::fN + xgcoex::bwN / 2) / 1e6
                    << ",ACLR_DB:" << coex_aclr << ",CCA_ED_DBM:-65" << std::endl;
        }

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
  if (load_rat && !(adaptive && load_stats))
    NS_FATAL_ERROR ("--load-rat needs --policy adaptive and --load-stats");
  clientHelper.SetAttribute ("XgLoadRat", BooleanValue (load_rat));
  clientHelper.SetAttribute ("XgCbrHigh", DoubleValue (cbr_high));
  clientHelper.SetAttribute ("XgCbrLow", DoubleValue (cbr_low));
  clientHelper.SetAttribute ("XgLteHigh", DoubleValue (lte_high));
  clientHelper.SetAttribute ("XgLoadPeriod", DoubleValue (load_period));
  if (qos_rat && !(adaptive && load_stats && security != 2))
    NS_FATAL_ERROR ("--qos-rat needs --policy adaptive, --load-stats and --security 0/1");
  clientHelper.SetAttribute ("XgQosRat", BooleanValue (qos_rat));
  clientHelper.SetAttribute ("XgQosMargin", DoubleValue (qos_margin));
  clientHelper.SetAttribute ("XgLatBudget", DoubleValue (lat_budget));
  clientHelper.SetAttribute ("XgPredRat", BooleanValue (pred_rat));
  clientHelper.SetAttribute ("XgPredHorizon", DoubleValue (pred_horizon));
  clientHelper.SetAttribute ("XgClean", BooleanValue (clean_policy));
  clientHelper.SetAttribute ("XgInitMain", UintegerValue (init_main));
  clientHelper.SetAttribute ("XgProbeHz", DoubleValue (probe_hz));
  clientHelper.SetAttribute ("XgCellDist", DoubleValue (cell_dist));
  clientHelper.SetAttribute ("XgNbEquip", BooleanValue (nb_equip));
  clientHelper.SetAttribute ("XgRsuRoute", BooleanValue (rsu_route));
  clientHelper.SetAttribute ("XgDupNear", DoubleValue (dup_near));
  clientHelper.SetAttribute ("XgGeoRat", DoubleValue (geo_rat));
  clientHelper.SetAttribute ("XgOracle", BooleanValue (oracle));
  clientHelper.SetAttribute ("XgBandit", BooleanValue (bandit));
  clientHelper.SetAttribute ("XgBanditProbe", BooleanValue (bandit_probe));

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
      uint32_t pool = nodeCounter;
      ++nodeCounter;
      xgload::active.insert (includedNode->GetId ());
      // xgregorm: --phone-share - telefony v aute zacnu vysielat 0.5-1.5 s po vstupe auta do mapy
      if (pool < xgphone::sock.size () && xgphone::sock[pool] && xgphone::started.insert (pool).second)
        Simulator::Schedule (Seconds (0.5 + xgphone::start->GetValue ()), &xglbg::burst, xgphone::sock[pool], 0.0,
                             xgphone::kbps, includedNode->GetId ());
      // xgregorm: --equip - priame technologie auta; auto bez V2X nema klienta (neposiela CAM)
      int eq = xgeq::mask[pool];
      xgstats::equip ()[std::stoul (vehicleID.substr (3))] = eq;
      if (xgeq::on && !eq)
        return includedNode;
      if (xgeq::on)
        clientHelper.SetAttribute ("XgPaths", UintegerValue (paths & (eq | xghyb::LTE)));
      ApplicationContainer ClientApp = clientHelper.Install (includedNode);
      ClientApp.Start (Seconds (0.0));
      ClientApp.Stop (simulationTime - Simulator::Now () - Seconds (0.1));
      return includedNode;
    };

  SHUTDOWN_FCN shutdownNode = [] (Ptr<Node> exNode,std::string vehicleID)
    {
      xgload::active.erase (exNode->GetId ());
      if (exNode->GetNApplications () == 0) // xgregorm: auto bez V2X (--equip) nema klienta
        return;
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
        xgbg::install (rsuNodes.Get (n), true, false, spat_on, rnd,
                       xgstats::rsuEqIdx ().empty () ? xghyb::WIFI | xghyb::NR : xgstats::rsuEqIdx ()[n % xgstats::rsuEqIdx ().size ()]);
      for (uint32_t n = 0; n < obuNodes.GetN (); ++n)
        {
          // senzory (CPM) len auta s V2X; nahodne cislo sa taha vzdy (rovnaka postupnost ako bez --equip)
          bool sensor = rnd->GetValue () < cpm_share;
          xgbg::install (obuNodes.Get (n), false, sensor && xgeq::mask[n], false, rnd, xgeq::mask[n]);
        }
    }

  // xgregorm: --dcc / --nr-cr - CBR stanice ako priemer poslednych dvoch 100 ms okien (ETSI TS 102 687 CBR_L0_Hop)
  xgdcc::wOn () = dcc && use_80211p && load_stats;
  xgdcc::nrOn () = nr_cr && use_nr && load_stats;
  if ((dcc && !xgdcc::wOn ()) || (nr_cr && !xgdcc::nrOn ()))
    std::cout << "XGREGORM-DCC,off (needs the technology and --load-stats)" << std::endl;
  xgdcc::cbrW () = [] (uint32_t n) { return 0.5 * (xgload::curW[n] + xgload::prevW[n]); };
  xgdcc::cbrN () = [] (uint32_t n) { return 0.5 * (xgload::curN[n] + xgload::prevN[n]); };
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
  if (xgphone::n)
    std::cout << "XGREGORM-PHONE,VEH:" << xgphone::n << ",PKT:" << xglbg::phoneN << ",BYTES:" << xglbg::phoneN * xglbg::pkt << std::endl;
  if (xgdcc::wOn () || xgdcc::nrOn ())
    {
      const auto &c = xgdcc::cnt ();
      std::cout << "XGREGORM-DCC,NOW:" << c[0] << ",QUEUED:" << c[1] << ",FROMQ:" << c[2] << ",REPLACED:" << c[3]
                << ",EXPIRED:" << c[4] << ",QDELAY_S:" << xgdcc::qDelay () << ",CAMSKIP:" << c[7] << ",CPMSKIP:" << c[8]
                << ",NR_SENT:" << c[5] << ",NR_DROP:" << c[6] << ",CR_SUM:" << xgdcc::crSum () << ",STATE:";
      for (int k = 0; k < 5; k++)
        std::cout << (k ? "/" : "") << xgdcc::stateN ()[k];
      std::cout << std::endl;
    }
  if (lte_bg_ue > 0)
    std::cout << "XGREGORM-LTEBG,UE:" << lte_bg_ue << ",PKT:" << xglbg::txN << ",BYTES:" << xglbg::txN * xglbg::pkt << std::endl;
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
  // xgregorm 2026-11-03: oneskorenie po technologiach cez vsetky kose zataze (2 = NR); kopie
  for (int k = 0; k < 3; k++)
    {
      xgstats::Hist h;
      for (int b = 0; b < xgstats::kLoadBins; b++)
        {
          const xgstats::Hist &q = xgstats::perf ()[k][b].lat;
          for (size_t i = 0; i < h.b.size (); i++)
            h.b[i] += q.b[i];
          h.n += q.n;
          h.sum += q.sum;
          h.max = std::max (h.max, q.max);
        }
      std::cout << "XGREGORM-LATP,TECH:" << techName[k] << ",N:" << h.n << ",P50_MS:" << 1000 * h.pct (0.5) << ",P95_MS:" << 1000 * h.pct (0.95)
                << ",P99_MS:" << 1000 * h.pct (0.99) << std::endl;
    }
  {
    const xgstats::Hist &h = xgstats::pir ();
    std::cout << "XGREGORM-PIR,N:" << h.n << ",MEAN_MS:" << (h.n ? 1000 * h.sum / h.n : 0) << ",P50_MS:" << 1000 * h.pct (0.5)
              << ",P95_MS:" << 1000 * h.pct (0.95) << ",P99_MS:" << 1000 * h.pct (0.99) << ",MAX_MS:" << 1000 * h.max << std::endl;
  }
  // straty pri prepnuti technologie (xgstats::comp, lostAlt): C<cesty>:nikto/len802.11p/lenNR/oboma
  std::cout << "XGREGORM-SWLOSS,WARMUP:" << xgstats::warmup ();
  for (int c = 0; c < 3; c++)
    {
      std::cout << ",C" << c << ":";
      for (int r = 0; r < 4; r++)
        std::cout << (r ? "/" : "") << xgstats::comp ()[c][r];
    }
  for (int c = 0; c < 2; c++)
    std::cout << ",ALT" << c << ":" << xgstats::lostAlt ()[c][0] << "/" << xgstats::lostAlt ()[c][1];
  std::cout << std::endl;
  // vzajomne rusenie 802.11p a NR (za cely beh): pocet cudzich signalov, priemerny vykon, CCA, zapocitane do CBR
  if (xgcoex::on)
    std::cout << "XGREGORM-COEX,TO_NR:" << xgcoex::toNr << ",TO_NR_DBM:" << (xgcoex::toNr ? xgcoex::sumNrDbm / xgcoex::toNr : 0)
              << ",TO_W:" << xgcoex::toW << ",TO_W_DBM:" << (xgcoex::toW ? xgcoex::sumWDbm / xgcoex::toW : 0)
              << ",CCA:" << xgcoex::ccaHits << ",CBR_W:" << xgcoex::cbrW << ",CBR_NR:" << xgcoex::cbrN << std::endl;
  if (xgstats::featLog ())
    xgstats::featLog ()->flush ();
  if (xgdcc::offload () > 0)
    std::cout << "XGREGORM-OFFLOAD,N:" << xgdcc::offloadN () << std::endl;
  // presnost predikcie (po warmup): E<k>:sucet |chyby| predikcie / "zostane ako teraz", B<k>: sucet chyb so znamienkom
  if (pred_rat)
    {
      std::cout << "XGREGORM-PRED,CELLS:" << xgstats::rem ().size () << ",COVER:" << xgstats::predCover ()[0] << "/"
                << xgstats::predCover ()[1] << ",REMTX:" << xgstats::remTx ()[0] << ",REMB:" << xgstats::remTx ()[1];
      for (int k = 0; k < 2; k++)
        std::cout << ",N" << k << ":" << xgstats::predN ()[k] << ",E" << k << ":" << xgstats::predErr ()[k][0] << "/"
                  << xgstats::predErr ()[k][1] << ",B" << k << ":" << xgstats::predBias ()[k][0] << "/" << xgstats::predBias ()[k][1];
      std::cout << std::endl;
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
