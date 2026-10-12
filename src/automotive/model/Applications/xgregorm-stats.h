// xgregorm: merania pre moje algoritmy CAM v trafficManager 802.11p.
// Len meria (cita SUMO cez TraCI), do siete nic neposiela ani nemeni.
//  - presnost informacie RSU: kazdych 100 ms pre kazde auto, ktore je naozaj na prijazde ku krizovatke
//    danej RSU a do 100 m od nej, porovna skutocnu polohu/cestu so stavom, ktory RSU pozna z poslednej prijatej CAM
//  - uplink: kolko CAM odoslanych do 100 m od RSU tato RSU naozaj prijala, zvlast 3 s po zacati zelenej
#ifndef XGREGORM_STATS_H
#define XGREGORM_STATS_H

#include "ns3/simulator.h"
#include "ns3/traci-client.h"
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cmath>

namespace ns3 {
namespace xgstats {

const double kRange = 100;     // [m] ako baseline PRR
const double kGreenWin = 3;    // [s] okno po zacati zelenej
const double kPeriod = 0.1;    // [s] perioda vzorkovania

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
  const std::map<unsigned long, std::string> *believedEdge = nullptr; // m_veh_position servera
  uint64_t samples = 0, unknown = 0, edgeWrong = 0, gt5 = 0, gt10 = 0;
  double errSum = 0;
};

struct Tx { double t, x, y; std::set<unsigned long> inRange; };

// skutocne PRR medzi autami (poloha zo SUMO, nie z GeoNet EPV, ktora sa obnovuje len raz za 1 s)
inline std::map<unsigned long, std::pair<double, double>> &vehPos () { static std::map<unsigned long, std::pair<double, double>> m; return m; }
inline uint64_t &v2vExp () { static uint64_t n = 0; return n; }
inline uint64_t &v2vRx () { static uint64_t n = 0; return n; }

inline std::vector<Rsu *> &rsus () { static std::vector<Rsu *> v; return v; }
inline std::map<unsigned long, Tx> &lastTx () { static std::map<unsigned long, Tx> m; return m; }
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
onCamTx (unsigned long sid, double x, double y)
{
  double t = Simulator::Now ().GetSeconds ();
  Tx tx {t, x, y, {}};
  for (auto &v : vehPos ())
    if (v.first != sid && std::hypot (x - v.second.first, y - v.second.second) <= kRange)
      tx.inRange.insert (v.first);
  v2vExp () += tx.inRange.size ();
  lastTx ()[sid] = tx;
  for (Rsu *r : rsus ())
    if (std::hypot (x - r->x, y - r->y) <= kRange)
      r->ulExp[bucket (r, t)]++;
}

inline void
onCamRx (Rsu *r, unsigned long sid)
{
  auto it = lastTx ().find (sid);
  if (it == lastTx ().end () || Simulator::Now ().GetSeconds () - it->second.t > 0.05)
    return;
  const Tx &tx = it->second;
  r->believedPos[sid] = {tx.x, tx.y};
  if (std::hypot (tx.x - r->x, tx.y - r->y) <= kRange)
    r->ulRx[bucket (r, tx.t)]++;
}

inline void
onVehCamRx (unsigned long receiver, unsigned long sender)
{
  auto it = lastTx ().find (sender);
  if (it != lastTx ().end () && Simulator::Now ().GetSeconds () - it->second.t <= 0.05 &&
      it->second.inRange.count (receiver))
    v2vRx ()++;
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
  for (const std::string &vid : c->TraCIAPI::vehicle.getIDList ())
    {
      std::string road = c->TraCIAPI::vehicle.getRoadID (vid);
      libsumo::TraCIPosition p = c->TraCIAPI::vehicle.getPosition (vid);
      unsigned long sid = std::stoul (vid.substr (3));
      vehPos ()[sid] = {p.x, p.y};
      for (Rsu *r : rsus ())
        {
          if (!r->approaches.count (road))
            continue;
          if (std::hypot (p.x - r->x, p.y - r->y) > kRange)
            continue; // dalej uz rozhoduje dosah, nie algoritmus
          r->samples++;
          auto bp = r->believedPos.find (sid);
          if (bp == r->believedPos.end ())
            {
              r->unknown++;
              r->edgeWrong++;
              continue;
            }
          double e = std::hypot (p.x - bp->second.first, p.y - bp->second.second);
          r->errSum += e;
          r->gt5 += e > 5;
          r->gt10 += e > 10;
          auto be = r->believedEdge->find (sid);
          if (be == r->believedEdge->end () || be->second != road)
            r->edgeWrong++;
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
