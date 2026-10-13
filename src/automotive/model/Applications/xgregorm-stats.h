// xgregorm: merania pre moje algoritmy CAM v trafficManager 802.11p.
// Len meria (cita SUMO cez TraCI), do siete nic neposiela ani nemeni.
//  - presnost informacie RSU: kazdych 100 ms pre kazde auto, ktore je naozaj na prijazde ku krizovatke
//    danej RSU a do 100 m od nej, porovna skutocnu polohu/cestu so stavom, ktory RSU pozna z poslednej prijatej CAM
//  - uplink: kolko CAM odoslanych do 100 m od RSU tato RSU naozaj prijala, zvlast 3 s po zacati zelenej
//  - hybrid (802.11p + LTE + NR-V2X): CAM sa paruju podla generationDeltaTime, meria sa oneskorenie po kazdej ceste,
//    vek informacie RSU a presnost aj na celom prijazde (nielen do 100 m)
//  - dorucenie podla vzdialenosti (--range-stats): auto-auto aj auto-RSU po 50 m do 1000 m (overenie dosahu technologii)
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

struct Tx
{
  double t, x, y;
  long gdt;                           // generationDeltaTime CAM
  bool direct;                        // odisla priamo autam (802.11p a/alebo NR-V2X)
  std::set<unsigned long> inRange;
  std::set<unsigned long> pending;    // auta v dosahu, ktore ju este nedostali
  std::set<const Rsu *> got;          // RSU, ktore ju uz prijali (prva kopia)
  std::map<unsigned long, int> farV;  // --range-stats: auta do 1000 m -> index vzdialenosti (este neprijate)
  std::map<const Rsu *, int> farR;    // --range-stats: RSU do 1000 m -> index vzdialenosti
};

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

// hustota po 1 s (vzorka = auto kazdych 100 ms): [0] vzorky, [1] sucet CBR x 1e4, [2] max CBR x 1e4,
// [3] sucet susedov 802.11p, [4] sucet susedov NR, [5] izolovane (0 susedov 802.11p), [6] CBR > 0.3, [7] CBR > 0.6
inline std::map<int, std::array<uint64_t, 8>> &densT () { static std::map<int, std::array<uint64_t, 8>> m; return m; }
inline void
onDensity (double cbr, int nW, int nNr)
{
  std::array<uint64_t, 8> &d = densT ()[(int) Simulator::Now ().GetSeconds ()];
  uint64_t c = (uint64_t) std::lround (cbr * 1e4);
  d[0]++;
  d[1] += c;
  d[2] = std::max (d[2], c);
  d[3] += nW;
  d[4] += nNr;
  d[5] += nW == 0;
  d[6] += cbr > 0.3;
  d[7] += cbr > 0.6;
}

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
onCamTx (unsigned long sid, double x, double y, long gdt, bool direct = true)
{
  double t = Simulator::Now ().GetSeconds ();
  Tx tx {t, x, y, gdt, direct, {}, {}, {}};
  // susedia do 100 m maju CAM dostat vzdy; ak odisla len cez LTE (direct = false), nedostanu ju -> strata
  (void) direct;
  for (auto &v : vehPos ())
    if (v.first != sid && std::hypot (x - v.second.first, y - v.second.second) <= kRange)
      tx.inRange.insert (v.first);
  tx.pending = tx.inRange;
  if (rangeOn ())
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
  v2vExp () += tx.inRange.size ();
  timeline ()[(int) t][2] += tx.inRange.size ();
  std::deque<Tx> &d = lastTx ()[sid];
  d.push_back (tx);
  while (d.size () > 1 && t - d.front ().t > 2.0)
    d.pop_front ();
  for (Rsu *r : rsus ())
    if (std::hypot (x - r->x, y - r->y) <= kRange)
      {
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
      r->latSum[path] += now - tx->t;
      r->latN[path]++;
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
      if (tx->pending.erase (receiver))
        {
          v2vRx ()++;
          timeline ()[(int) tx->t][3]++;
          v2vLatSum ()[path] += Simulator::Now ().GetSeconds () - tx->t;
          v2vLatN ()[path]++;
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
          if (!r->approaches.count (road))
            continue;
          auto bp = r->believedPos.find (sid);
          double e = bp == r->believedPos.end () ? 0 : std::hypot (p.x - bp->second.first, p.y - bp->second.second);
          double age = bp == r->believedPos.end () ? 0 : now - r->believedT[sid];
          // cely prijazd (hybrid: LTE doplna informaciu aj mimo dosahu 802.11p)
          r->samplesF++;
          std::array<uint64_t, 8> &tl = timeline ()[(int) now];
          tl[4]++;
          if (bp == r->believedPos.end ())
            {
              r->unknownF++;
              tl[5]++;
            }
          else
            {
              r->errSumF += e;
              r->ageSumF += age;
              tl[6] += (uint64_t) (age * 1000);
              tl[7] += e > 10;
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
      if (!r->believedEdge || r->tls.empty ())
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
