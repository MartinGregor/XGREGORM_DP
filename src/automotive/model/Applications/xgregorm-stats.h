// xgregorm: merania pre moje algoritmy CAM v trafficManager 802.11p.
// Len meria (cita SUMO cez TraCI), do siete nic neposiela ani nemeni.
//  - presnost informacie RSU: kazdych 100 ms pre kazde auto, ktore je naozaj na prijazde ku krizovatke
//    danej RSU a do 100 m od nej, porovna skutocnu polohu/cestu so stavom, ktory RSU pozna z poslednej prijatej CAM
//  - uplink: kolko CAM odoslanych do 100 m od RSU tato RSU naozaj prijala, zvlast 3 s po zacati zelenej
//  - hybrid (802.11p + LTE + NR-V2X): CAM sa paruju podla generationDeltaTime, meria sa oneskorenie po kazdej ceste,
//    vek informacie RSU a presnost aj na celom prijazde (nielen do 100 m)
//  - dorucenie podla vzdialenosti (--range-stats): auto-auto aj auto-RSU po 50 m do 1000 m (overenie dosahu technologii)
//  - zataz technologii (kazdych 100 ms) a vykonnost kazdej technologie podla zataze v case vyslania CAM:
//      802.11p: CBR podla ETSI TS 102 687 = podiel casu, ked je prijaty vykon > -85 dBm, okno 100 ms (MERANIE)
//      LTE:     vyuzitie PRB uplinku bunky podla 3GPP TS 36.314 = pridelene PRB / dostupne PRB, okno 100 ms (MERANIE)
//      NR-V2X:  SL-CBR podla 3GPP TS 38.215 je podiel subkanalov so S-RSSI nad prahom; ns-3 (5G-LENA) S-RSSI po
//               subkanaloch nevypisuje, preto len ODHAD z prijatych (dekodovanych) PSCCH = obsadene subkanaly v slote.
//               Je to dolny odhad (prenosy, ktorych PSCCH sa nedekodoval - slabe/kolizne - chybaju). Presne meranie by
//               vyzadovalo zasah do NR modulu (trace v NrSpectrumPhy::StartRx), zamerne nerobene.
//    vyplna xgregorm-hybrid.cc (loadFn, loadDist), vykonnost: PRR auto-auto po technologiach, oneskorenie, PIR
#ifndef XGREGORM_STATS_H
#define XGREGORM_STATS_H

#include "ns3/simulator.h"
#include "ns3/traci-client.h"
#include <map>
#include <set>
#include <string>
#include <vector>
#include <deque>
#include <array>
#include <cmath>
#include <functional>

namespace ns3 {
namespace xgstats {

const double kRange = 100;     // [m] ako baseline PRR
const double kGreenWin = 3;    // [s] okno po zacati zelenej
const double kPeriod = 0.1;    // [s] perioda vzorkovania
const double kBin = 50;        // [m] krok merania dorucenia podla vzdialenosti
const int kBins = 20;          // do 1000 m

struct Rsu
{
  std::string id;
  double x = 0, y = 0;
  std::string tls;                                  // riadeny semafor ("" = ziadny)
  std::set<std::string> approaches;                 // prijazdove cesty k nemu
  int lastPhase = -1;
  std::vector<double> greenStarts;                  // [s]
  uint64_t ulExp[2] = {0, 0}, ulRx[2] = {0, 0};     // [0] mimo okna, [1] do 3 s po zelenej
  std::map<unsigned long, std::pair<double, double>> believedPos;   // posledna prijata poloha auta
  std::map<unsigned long, double> believedT;                        // kedy bola ta CAM odoslana
  const std::map<unsigned long, std::string> *believedEdge = nullptr; // m_veh_position servera
  uint64_t samples = 0, unknown = 0, edgeWrong = 0, gt5 = 0, gt10 = 0;
  double errSum = 0;
  // cely prijazd (aj > 100 m), vek informacie [s]
  uint64_t samplesF = 0, unknownF = 0, gt10F = 0;
  double errSumF = 0, ageSum = 0, ageSumF = 0;
  // "fantomy": auta, ktore RSU (radic) eviduje na prijazde k svojmu semaforu, hoci tam uz nie su
  uint64_t believedOn = 0, phantom = 0;
  // oneskorenie auto -> RSU [0] 802.11p, [1] LTE + kabel, [2] NR-V2X
  double latSum[3] = {0, 0, 0};
  uint64_t latN[3] = {0, 0, 0};
};

// ---------------------------------------------------------------- zataz a vykonnost podla zataze
const double kCbrThrDbm = -85;  // ETSI TS 102 687: prah obsadenosti kanala 802.11p
const int kLoadBins = 5;        // kose zataze: < 0.1, 0.1-0.2, 0.2-0.3, 0.3-0.6, >= 0.6 (ETSI DCC: 0.3 / 0.6)
inline int loadBin (double c) { return c < 0.1 ? 0 : c < 0.2 ? 1 : c < 0.3 ? 2 : c < 0.6 ? 3 : 4; }

// logaritmicky histogram casov (0.1 us .. 1000 s, krok ~6 %) - percentily bez ukladania vzoriek
struct Hist
{
  std::array<uint64_t, 400> b {};
  uint64_t n = 0;
  double sum = 0, max = 0;
  void add (double s)
  {
    int i = s <= 1e-7 ? 0 : std::min (399, std::max (0, (int) std::floor ((std::log10 (s * 1e6) + 1) * 40)));
    b[i]++;
    n++;
    sum += s;
    max = std::max (max, s);
  }
  double pct (double q) const
  {
    if (!n)
      return NAN;
    uint64_t k = (uint64_t) std::ceil (q * n), c = 0;
    for (int i = 0; i < 400; i++)
      if ((c += b[i]) >= k)
        return std::min (max, std::pow (10, (i + 0.5) / 40.0 - 1) * 1e-6);
    return max;
  }
};

// rozdelenie zataze (0..1 po 0.01), jedna vzorka = jedna stanica (802.11p, NR) / bunka (LTE) za 100 ms
struct LoadHist
{
  std::array<uint64_t, 101> b {};
  uint64_t n = 0;
  double sum = 0, max = 0;
  void add (double c)
  {
    c = std::min (1.0, std::max (0.0, c));
    b[(int) std::lround (c * 100)]++;
    n++;
    sum += c;
    max = std::max (max, c);
  }
  double pct (double q) const
  {
    uint64_t k = (uint64_t) std::ceil (q * n), c = 0;
    for (int i = 0; i <= 100; i++)
      if ((c += b[i]) >= k)
        return i / 100.0;
    return max;
  }
  uint64_t atLeast (double t) const
  {
    uint64_t c = 0;
    for (int i = (int) std::lround (t * 100); i <= 100; i++)
      c += b[i];
    return c;
  }
};

// technologia: 0 = 802.11p, 1 = LTE, 2 = NR-V2X (bity cesty 1, 2, 4 ako xghyb::WIFI/LTE/NR)
inline std::array<LoadHist, 3> &loadDist () { static std::array<LoadHist, 3> a; return a; }
// aktualna zataz, ktoru vidi uzol (posledne okno 100 ms); nastavuje xgregorm-hybrid.cc, inak sa nemeria
inline std::array<std::function<double (uint32_t)>, 3> &loadFn () { static std::array<std::function<double (uint32_t)>, 3> a; return a; }
// vykonnost podla zataze odosielatela v case vyslania: 802.11p a NR = auto-auto do 100 m (kazda technologia zvlast,
// aj druha kopia), LTE = auto -> RSU (exp = CAM poslane cez LTE, rx = dostala ju aspon jedna RSU)
struct Perf
{
  uint64_t cam = 0, exp = 0, rx = 0;
  Hist lat;
};
inline std::array<std::array<Perf, kLoadBins>, 3> &perf () { static std::array<std::array<Perf, kLoadBins>, 3> a; return a; }
// priebezny vypis hybridu: uzol auta -> funkcia vracajuca masku ciest, ktorymi by prave odisla CAM (bez skusky 802.11p)
inline std::map<uint32_t, std::function<int ()>> &pathNow () { static std::map<uint32_t, std::function<int ()>> m; return m; }
// uzol auta -> maska priamych technologii, ktorymi je auto "pripojene" k infrastrukture (pocul RSU za poslednu 1 s);
// pripojenie k LTE (RRC CONNECTED) urcuje xgregorm-hybrid.cc
inline std::map<uint32_t, std::function<int ()>> &connNow () { static std::map<uint32_t, std::function<int ()>> m; return m; }

// PIR (3GPP TR 37.885): cas medzi dvoma prijatymi CAM od toho isteho auta (auto-auto do 100 m, prva kopia);
// medzery > 5 s sa nepocitaju (auto bolo medzitym mimo dosahu)
inline Hist &pir () { static Hist h; return h; }
inline std::map<std::pair<unsigned long, unsigned long>, double> &lastRxT () { static std::map<std::pair<unsigned long, unsigned long>, double> m; return m; }

struct Tx
{
  double t, x, y;
  unsigned long sid = 0;
  long gdt;                           // generationDeltaTime CAM
  bool direct;                        // odisla priamo autam (802.11p a/alebo NR-V2X)
  std::set<unsigned long> inRange;
  std::set<unsigned long> pending;    // auta v dosahu, ktore ju este nedostali
  std::set<const Rsu *> got;          // RSU, ktore ju uz prijali (prva kopia)
  std::map<unsigned long, int> farV;  // --range-stats: auta do 1000 m -> index vzdialenosti (este neprijate)
  std::map<const Rsu *, int> farR;    // --range-stats: RSU do 1000 m -> index vzdialenosti
  int paths = 0;                      // cesty, ktorymi CAM odisla (1 802.11p, 2 LTE, 4 NR)
  std::array<int, 3> lbin {{-1, -1, -1}}; // kos zataze odosielatela pri vyslani (-1 = nemerane)
  std::set<unsigned long> gotW, gotN; // auta v dosahu, ktore ju dostali cez 802.11p / NR
  bool gotL = false;                  // aspon jedna RSU ju dostala cez LTE
};

// NR sidelink bearer vznika az v case nrFrom (autori: 2 s) - CAM poslane cez NR skor sa nemohli dorucit (artefakt
// nastavenia simulacie, nie vlastnost technologie), do vykonnosti NR podla zataze sa nepocitaju
inline double &nrFrom () { static double t = 0; return t; }

// --warmup: merania sa zacnu az v tomto case (autá startuju vsetky v case 0 stojace, prvych ~30 s je rozbeh);
// CAM poslane skor sa do ziadnej statistiky nezapocitaju, ani ked su prijate neskor. Spravanie algoritmov sa nemeni.
inline double &warmup () { static double t = 0; return t; }
inline bool measuring () { return Simulator::Now ().GetSeconds () >= warmup (); }

// straty pri prepnuti technologie: pre kazdu CAM a suseda do 100 m (meria sa len pri CAM poslanej po nrFrom)
//  comp[c][r]: c = priame cesty CAM (0 = len 802.11p, 1 = len NR, 2 = obe), r = dostal ju 0 nikto, 1 len 802.11p,
//              2 len NR, 3 oboma -> pri c = 2 je to doplnkovost technologii (r = 1: CAM by sa stratila, keby auto
//              poslalo len cez NR; r = 2: keby len cez 802.11p)
//  lostAlt[c][j]: CAM poslana len jednou technologiou (c = 0 / 1) a sused ju nedostal, hoci od toho isteho auta
//              prijal druhou technologiou (j = 0) za poslednu 1 s / (j = 1) za posledne 3 s pred vyslanim -> odhad
//              strat, ktore by druha technologia zachranila. Pri c = 1 je dolny odhad: auto na NR posiela 802.11p
//              len obcas (skuska), takze dokaz, ze 802.11p fungovalo, casto chyba.
inline std::array<std::array<uint64_t, 4>, 3> &comp () { static std::array<std::array<uint64_t, 4>, 3> a {}; return a; }
inline std::array<std::array<uint64_t, 2>, 2> &lostAlt () { static std::array<std::array<uint64_t, 2>, 2> a {}; return a; }
// (prijemca, odosielatel) -> cas posledneho prijatia CAM [0] cez 802.11p, [1] cez NR
inline std::map<std::pair<unsigned long, unsigned long>, std::array<double, 2>> &lastRxTech ()
{
  static std::map<std::pair<unsigned long, unsigned long>, std::array<double, 2>> m;
  return m;
}

// mapa kvality (radio environment map) pre prediktivny vyber technologie (XgPredRat): zdielana medzi autami - v realite
// server, kam auta posielaju svoje merania (crowdsourcing, ako 3GPP predikcia QoS); tu jedna struktura. Bunka
// remCell x remCell m (suradnice SUMO). Pre kazdu priamu technologiu (0 = 802.11p, 1 = NR):
//  exp/ok = ocakavane / nacas dorucene CAM susedov do 100 m, ktore auto namerajo, ked bolo v bunke (prijimac);
//  cbr    = sucet vzoriek zataze kanala (100 ms) v bunke, cbrN ich pocet.
// Sucty zabudaju s casovou konstantou remTau (premavka sa meni). Mapa obsahuje len merania z minulosti.
struct RemCell
{
  double t = 0;
  double exp[2] = {0, 0}, ok[2] = {0, 0}, cbr[2] = {0, 0}, cbrN[2] = {0, 0};
};
inline double &remCell () { static double c = 20; return c; }
inline double &remTau () { static double t = 30; return t; }
inline std::map<std::pair<int, int>, RemCell> &rem () { static std::map<std::pair<int, int>, RemCell> m; return m; }
inline RemCell *
remAt (double x, double y, bool create)
{
  std::pair<int, int> key {(int) std::floor (x / remCell ()), (int) std::floor (y / remCell ())};
  auto it = rem ().find (key);
  if (it == rem ().end ())
    {
      if (!create)
        return nullptr;
      it = rem ().emplace (key, RemCell ()).first;
    }
  RemCell &c = it->second;
  double now = Simulator::Now ().GetSeconds ();
  if (now > c.t)
    {
      double f = std::exp (-(now - c.t) / remTau ());
      for (int k = 0; k < 2; k++)
        {
          c.exp[k] *= f;
          c.ok[k] *= f;
          c.cbr[k] *= f;
          c.cbrN[k] *= f;
        }
      c.t = now;
    }
  return &c;
}
// presnost predikcie (len po warmup): predikovana spolahlivost na nasledujuci horizont vs. potom namerana autom;
// [k][0] = predikcia z mapy, [k][1] = "zostane ako teraz" (sucasne meranie, zaklad pre porovnanie)
inline std::array<std::array<double, 2>, 2> &predErr () { static std::array<std::array<double, 2>, 2> a {}; return a; }
inline std::array<std::array<double, 2>, 2> &predBias () { static std::array<std::array<double, 2>, 2> a {}; return a; }
inline std::array<uint64_t, 2> &predN () { static std::array<uint64_t, 2> a {}; return a; }
inline std::array<uint64_t, 2> &predCover () { static std::array<uint64_t, 2> a {}; return a; } // [0] s mapou, [1] bez

// vybava aut (--equip): ID auta -> priame technologie (1 = 802.11p, 4 = NR-V2X, 0 = bez V2X); chyba = obe.
// Auto bez V2X nie je prijemca CAM (nema ju dostat) a RSU o nom vediet nemoze; auto bez technologie sa do jej
// vykonnosti nepocita, do skutocneho PRR ano (CAM poslanu len technologiou, ktoru nema, nedostane = strata).
inline std::map<unsigned long, int> &equip () { static std::map<unsigned long, int> m; return m; }
inline int
eqOf (unsigned long sid)
{
  auto it = equip ().find (sid);
  return it == equip ().end () ? 5 : it->second;
}
inline uint64_t
countEq (const std::set<unsigned long> &s, int bit)
{
  uint64_t n = 0;
  for (unsigned long r : s)
    n += (eqOf (r) & bit) != 0;
  return n;
}

// vykonnost podla zataze sa zapocita, ked CAM vypadne z evidencie (2 s) alebo na konci behu
inline void
finishTx (const Tx &tx)
{
  if (tx.t < warmup ())
    return;
  int direct = tx.paths & 5;
  if (direct && tx.t >= nrFrom ())
    {
      int c = direct == 1 ? 0 : direct == 4 ? 1 : 2;
      for (unsigned long r : tx.inRange)
        {
          int k = (tx.gotW.count (r) ? 1 : 0) + (tx.gotN.count (r) ? 2 : 0);
          comp ()[c][k]++;
          if (c < 2 && k == 0)
            {
              auto lr = lastRxTech ().find ({r, tx.sid});
              double alt = lr == lastRxTech ().end () ? -1e9 : lr->second[c == 0 ? 1 : 0];
              lostAlt ()[c][0] += tx.t - alt <= 1.0;
              lostAlt ()[c][1] += tx.t - alt <= 3.0;
            }
        }
    }
  if (tx.lbin[0] >= 0 && (tx.paths & 1))
    {
      Perf &p = perf ()[0][tx.lbin[0]];
      p.cam++;
      p.exp += countEq (tx.inRange, 1);
      p.rx += tx.gotW.size ();
    }
  if (tx.lbin[2] >= 0 && (tx.paths & 4) && tx.t >= nrFrom ())
    {
      Perf &p = perf ()[2][tx.lbin[2]];
      p.cam++;
      p.exp += countEq (tx.inRange, 4);
      p.rx += tx.gotN.size ();
    }
  if (tx.lbin[1] >= 0 && (tx.paths & 2))
    {
      Perf &p = perf ()[1][tx.lbin[1]];
      p.cam++;
      p.exp++;
      p.rx += tx.gotL;
    }
}

// dorucenie podla vzdialenosti [index po 50 m]: [0] auto-auto ocakavane, [1] prijate, [2] auto-RSU ocakavane, [3] prijate
inline bool &rangeOn () { static bool b = false; return b; }
inline std::array<std::array<uint64_t, kBins>, 4> &rangeBins () { static std::array<std::array<uint64_t, kBins>, 4> a {}; return a; }

// skutocne PRR medzi autami (poloha zo SUMO, nie z GeoNet EPV, ktora sa obnovuje len raz za 1 s)
inline std::map<unsigned long, std::pair<double, double>> &vehPos () { static std::map<unsigned long, std::pair<double, double>> m; return m; }
inline uint64_t &v2vExp () { static uint64_t n = 0; return n; }
inline uint64_t &v2vRx () { static uint64_t n = 0; return n; }
// oneskorenie auto -> auto podla technologie [0] 802.11p, [2] NR (prva kopia CAM)
inline double *v2vLatSum () { static double d[3] = {0, 0, 0}; return d; }
inline uint64_t *v2vLatN () { static uint64_t n[3] = {0, 0, 0}; return n; }

// casova os po 1 s (vsetky RSU spolu): [0] uplink ocakavane, [1] uplink prijate (do 100 m od RSU, podla
// casu vyslania), [2] auto-auto ocakavane, [3] auto-auto prijate, [4] vzorky na prijazde, [5] RSU o aute nevie,
// [6] sucet veku informacie RSU [ms], [7] chyba polohy > 10 m
inline std::map<int, std::array<uint64_t, 8>> &timeline () { static std::map<int, std::array<uint64_t, 8>> m; return m; }

inline std::vector<Rsu *> &rsus () { static std::vector<Rsu *> v; return v; }
inline std::map<unsigned long, std::deque<Tx>> &lastTx () { static std::map<unsigned long, std::deque<Tx>> m; return m; }
inline Ptr<TraciClient> &client () { static Ptr<TraciClient> c; return c; }
inline bool &running () { static bool r = false; return r; }

inline int
bucket (const Rsu *r, double t)
{
  for (auto it = r->greenStarts.rbegin (); it != r->greenStarts.rend (); ++it)
    {
      if (*it <= t)
        return t - *it < kGreenWin ? 1 : 0;
    }
  return 0;
}

inline void
onCamTx (unsigned long sid, double x, double y, long gdt, bool direct = true, int paths = 0, uint32_t node = UINT32_MAX)
{
  double t = Simulator::Now ().GetSeconds ();
  Tx tx {t, x, y, sid, gdt, direct, {}, {}, {}};
  tx.paths = paths;
  bool meas = t >= warmup ();
  for (int k = 0; k < 3; k++)
    if (node != UINT32_MAX && loadFn ()[k] && (paths & (1 << k)))
      tx.lbin[k] = loadBin (loadFn ()[k] (node));
  // susedia do 100 m maju CAM dostat vzdy; ak odisla len cez LTE (direct = false), nedostanu ju -> strata
  (void) direct;
  for (auto &v : vehPos ())
    if (v.first != sid && eqOf (v.first) && std::hypot (x - v.second.first, y - v.second.second) <= kRange)
      tx.inRange.insert (v.first);
  tx.pending = tx.inRange;
  if (rangeOn () && meas)
    {
      for (auto &v : vehPos ())
        {
          double d = std::hypot (x - v.second.first, y - v.second.second);
          if (v.first != sid && d < kBin * kBins)
            {
              tx.farV[v.first] = (int) (d / kBin);
              rangeBins ()[0][(int) (d / kBin)]++;
            }
        }
      for (Rsu *r : rsus ())
        {
          double d = std::hypot (x - r->x, y - r->y);
          if (d < kBin * kBins)
            {
              tx.farR[r] = (int) (d / kBin);
              rangeBins ()[2][(int) (d / kBin)]++;
            }
        }
    }
  if (meas)
    v2vExp () += tx.inRange.size ();
  timeline ()[(int) t][2] += tx.inRange.size ();
  std::deque<Tx> &d = lastTx ()[sid];
  d.push_back (tx);
  while (d.size () > 1 && t - d.front ().t > 2.0)
    {
      finishTx (d.front ());
      d.pop_front ();
    }
  for (Rsu *r : rsus ())
    if (std::hypot (x - r->x, y - r->y) <= kRange)
      {
        if (meas)
          r->ulExp[bucket (r, t)]++;
        timeline ()[(int) t][0]++;
      }
}

// path = 0 802.11p, 1 LTE + kabel, 2 NR-V2X; parovanie podla generationDeltaTime (LTE moze meskat aj > 100 ms)
inline void
onCamRx (Rsu *r, unsigned long sid, long gdt, int path = 0)
{
  auto it = lastTx ().find (sid);
  if (it == lastTx ().end ())
    return;
  for (auto tx = it->second.rbegin (); tx != it->second.rend (); ++tx)
    {
      if (tx->gdt != gdt)
        continue;
      double now = Simulator::Now ().GetSeconds ();
      bool meas = tx->t >= warmup ();
      if (meas)
        {
          r->latSum[path] += now - tx->t;
          r->latN[path]++;
        }
      if (path == 1)
        {
          tx->gotL = true;
          if (tx->lbin[1] >= 0 && meas)
            perf ()[1][tx->lbin[1]].lat.add (now - tx->t);
        }
      if (!tx->got.insert (r).second)
        return; // druha kopia tej istej CAM (DUAL)
      auto fr = tx->farR.find (r);
      if (fr != tx->farR.end ())
        rangeBins ()[3][fr->second]++;
      auto bt = r->believedT.find (sid);
      if (bt == r->believedT.end () || tx->t > bt->second)
        {
          r->believedPos[sid] = {tx->x, tx->y};
          r->believedT[sid] = tx->t;
        }
      if (std::hypot (tx->x - r->x, tx->y - r->y) <= kRange)
        {
          if (meas)
            r->ulRx[bucket (r, tx->t)]++;
          timeline ()[(int) tx->t][1]++;
        }
      return;
    }
}

// path = 0 802.11p, 2 NR-V2X; kazda CAM sa auto-auto zapocita najviac raz (pri oboch cestach prva kopia)
inline void
onVehCamRx (unsigned long receiver, unsigned long sender, long gdt, int path = 0)
{
  auto it = lastTx ().find (sender);
  if (it == lastTx ().end ())
    return;
  for (auto tx = it->second.rbegin (); tx != it->second.rend (); ++tx)
    {
      if (tx->gdt != gdt)
        continue;
      auto fv = tx->farV.find (receiver);
      if (fv != tx->farV.end ())
        {
          rangeBins ()[1][fv->second]++;
          tx->farV.erase (fv);
        }
      double now = Simulator::Now ().GetSeconds ();
      bool meas = tx->t >= warmup ();
      lastRxTech ().emplace (std::make_pair (receiver, sender), std::array<double, 2> {{-1e9, -1e9}})
          .first->second[path == 2 ? 1 : 0] = tx->t;
      // vykonnost po technologiach: kazda kopia svojou technologiou (prva kopia kazdou)
      if (tx->inRange.count (receiver))
        {
          int k = path == 2 ? 2 : 0;
          std::set<unsigned long> &got = k == 2 ? tx->gotN : tx->gotW;
          if (got.insert (receiver).second && tx->lbin[k] >= 0 && meas)
            perf ()[k][tx->lbin[k]].lat.add (now - tx->t);
        }
      if (tx->pending.erase (receiver))
        {
          timeline ()[(int) tx->t][3]++;
          if (meas)
            {
              v2vRx ()++;
              v2vLatSum ()[path] += now - tx->t;
              v2vLatN ()[path]++;
            }
          auto key = std::make_pair (receiver, sender);
          auto lr = lastRxT ().find (key);
          if (lr != lastRxT ().end () && now - lr->second <= 5.0 && meas)
            pir ().add (now - lr->second);
          lastRxT ()[key] = now;
        }
      return;
    }
}

inline void
sample ()
{
  if (!running ())
    return;
  Ptr<TraciClient> c = client ();
  double now = Simulator::Now ().GetSeconds ();
  for (Rsu *r : rsus ())
    {
      if (r->tls.empty ())
        continue;
      int ph = c->TraCIAPI::trafficlights.getPhase (r->tls);
      if (ph != r->lastPhase && (ph == 0 || ph == 2))
        r->greenStarts.push_back (now);
      r->lastPhase = ph;
    }
  std::map<unsigned long, std::string> trueRoad;
  for (const std::string &vid : c->TraCIAPI::vehicle.getIDList ())
    {
      std::string road = c->TraCIAPI::vehicle.getRoadID (vid);
      trueRoad[std::stoul (vid.substr (3))] = road;
      libsumo::TraCIPosition p = c->TraCIAPI::vehicle.getPosition (vid);
      unsigned long sid = std::stoul (vid.substr (3));
      vehPos ()[sid] = {p.x, p.y};
      for (Rsu *r : rsus ())
        {
          if (!eqOf (sid))
            break; // auto bez V2X RSU poznat nemoze
          if (!r->approaches.count (road))
            continue;
          auto bp = r->believedPos.find (sid);
          double e = bp == r->believedPos.end () ? 0 : std::hypot (p.x - bp->second.first, p.y - bp->second.second);
          double age = bp == r->believedPos.end () ? 0 : now - r->believedT[sid];
          std::array<uint64_t, 8> &tl = timeline ()[(int) now];
          tl[4]++;
          if (bp == r->believedPos.end ())
            tl[5]++;
          else
            {
              tl[6] += (uint64_t) (age * 1000);
              tl[7] += e > 10;
            }
          if (now < warmup ())
            continue;
          // cely prijazd (hybrid: LTE doplna informaciu aj mimo dosahu 802.11p)
          r->samplesF++;
          if (bp == r->believedPos.end ())
            r->unknownF++;
          else
            {
              r->errSumF += e;
              r->ageSumF += age;
              r->gt10F += e > 10;
            }
          if (std::hypot (p.x - r->x, p.y - r->y) > kRange)
            continue; // dalej uz rozhoduje dosah, nie algoritmus
          r->samples++;
          if (bp == r->believedPos.end ())
            {
              r->unknown++;
              r->edgeWrong++;
              continue;
            }
          r->ageSum += age;
          r->errSum += e;
          r->gt5 += e > 5;
          r->gt10 += e > 10;
          auto be = r->believedEdge->find (sid);
          if (be == r->believedEdge->end () || be->second != road)
            r->edgeWrong++;
        }
    }
  // fantomy: radic pocita auto na prijazde (posledna CAM), ale auto uz je inde (vynechane: CAM starsia nez 0.5 s
  // je pri vzorke v pohybe normalna - pocitame len, ak posledna CAM, z ktorej to RSU vie, je starsia nez 2 s)
  for (Rsu *r : rsus ())
    {
      if (!r->believedEdge || r->tls.empty () || now < warmup ())
        continue;
      for (const auto &be : *r->believedEdge)
        {
          if (be.second.empty () || !r->approaches.count (be.second))
            continue;
          r->believedOn++;
          auto tr = trueRoad.find (be.first);
          auto bt = r->believedT.find (be.first);
          if (tr != trueRoad.end () && tr->second != be.second && bt != r->believedT.end () && now - bt->second > 2.0)
            r->phantom++;
        }
    }
  Simulator::Schedule (Seconds (kPeriod), &sample);
}

// na konci behu: zapocita CAM, ktore este su v evidencii
inline void
finishAllTx ()
{
  for (auto &e : lastTx ())
    {
      for (const Tx &tx : e.second)
        finishTx (tx);
      e.second.clear ();
    }
}

inline void
registerRsu (Rsu *r, Ptr<TraciClient> c)
{
  rsus ().push_back (r);
  if (!running ())
    {
      client () = c;
      running () = true;
      Simulator::Schedule (Seconds (kPeriod), &sample);
    }
}

} // namespace xgstats
} // namespace ns3

#endif /* XGREGORM_STATS_H */
