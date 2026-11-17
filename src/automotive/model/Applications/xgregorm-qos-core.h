// xgregorm: jadro vyberu siete podla nameranej kvality, nezavisle od simulatora (cisty C++17, bez ns-3 aj OMNeT++).
//
// Pouziva ho klient v ns-3 (trafficManagerClient80211p, --qos-rat) a aplikacia v OMNeT++ / Veins
// (xgregorm_omnet/src/XgCamApp), aby obe simulacie pocitali kvalitu a rozhodovali presne tym istym kodom.
//
// Princip (podrobne v praci):
//  1. Kazda CAM nesie 1 bajt (pole Reserved zakladnej hlavicky GeoNet): bit 7 = poslana cez technologiu 0 (802.11p),
//     bit 6 = cez technologiu 1 (NR-V2X), bity 0-5 = poradove cislo mod 64.
//  2. Prijimac si pre kazdeho suseda drzi CAM cakajuce na vyhodnotenie. Po 200 ms sa CAM uzavrie: pre kazdu technologiu
//     zo znacky je ocakavana 1 (plus CAM, ktore chybaju v poradovych cislach, ak ich ta technologia mala niest),
//     dorucena nacas, ak prisla touto technologiou s oneskorenim <= rozpoctu (3GPP TS 22.186, 100 ms).
//  3. Kvalita = klzavy podiel dorucenych nacas / ocakavanych (zabudanie s casovou konstantou 2 s).
//  4. Raz za sekundu: druha technologia lepsia aspon o margin a nie zahltena -> prejdi s pravdepodobnostou zisk / 0.4.
//
// Rozhranie je volane z kodu simulatora: ten dodava cas, prijate znacky a oneskorenie, polohy (kto je sused do 100 m)
// a nahodne cislo; jadro vracia, co sa ma zapocitat a ci prepnut.
#ifndef XGREGORM_QOS_CORE_H
#define XGREGORM_QOS_CORE_H

#include <algorithm>
#include <cmath>
#include <deque>
#include <utility>

namespace xgcore {

// jedna CAM suseda cakajuca na vyhodnotenie
struct QRec
{
  int seq;       // poradove cislo mod 64
  bool has[2];   // technologie, ktorymi ju odosielatel poslal (zo znacky)
  double t0;     // cas prveho prijatia [s]
  bool got[2];   // prisla touto technologiou
  double lat[2]; // oneskorenie prvej kopie touto technologiou [s]
};

// stav jedneho suseda
struct QNeighbor
{
  std::deque<QRec> recs;          // CAM cakajuce na vyhodnotenie
  int seq = -1;                   // poradove cislo poslednej vyhodnotenej CAM
  bool has[2] = {false, false};   // jej technologie
  double t = -10;                 // jej cas
  std::deque<std::pair<double, double>> clk; // (cas prijatia, surove oneskorenie) pre relativne oneskorenie (qosRelLat)
};

// relativne oneskorenie (xgregorm 2026-11-17, --lat-rel): surove oneskorenie = cas prijatia podla mojich hodin - cas
// vzniku podla hodin odosielatela = skutocne oneskorenie + rozdiel hodin. Rozdiel hodin je pre dvojicu staly (alebo sa
// meni pomaly), preto sa od suroveho oneskorenia odcita jeho minimum za poslednych win s zo vsetkych sprav toho isteho
// odosielatela (oboma technologiami): CAM, ktora prisla neskoro oproti ostatnym od neho, ma velke relativne oneskorenie
// a nezapocita sa ako nacas. Presne hodiny teda netreba; chyba = najmensie skutocne oneskorenie v okne (pri 802.11p
// ~0.3 ms). Klzave minimum monotonnou frontou.
inline double
qosRelLat (QNeighbor &st, double raw, double now, double win = 10.0)
{
  auto &q = st.clk;
  while (!q.empty () && q.back ().second >= raw)
    q.pop_back ();
  q.push_back ({now, raw});
  while (now - q.front ().first > win)
    q.pop_front ();
  return raw - q.front ().second;
}

inline int
qosMark (bool tech0, bool tech1, int seq)
{
  return (tech0 ? 0x80 : 0) | (tech1 ? 0x40 : 0) | (seq & 63);
}

// pocet ocakavanych CAM technologie k za vyhodnotenu CAM (1 + chybajuce pred nou, ak ich ta technologia mala niest)
inline double
qosExpected (const QNeighbor &st, int miss, int k)
{
  return 1 + (st.has[k] ? miss : 0);
}

// CAM prisla technologiou k nacas
inline bool
qosOnTime (const QRec &r, int k, double budget)
{
  return r.got[k] && r.lat[k] <= budget;
}

// uzavretie CAM r: urci pocet chybajucich CAM pred nou, zavola eval (r, miss) a posunie stav suseda.
// Ta ista CAM este raz (rovnake poradove cislo) sa nevyhodnoti. Medzera > 3 s = novy kontakt (chybajuce sa nepocitaju).
template <class Eval>
inline void
qosFinish (QNeighbor &st, const QRec &r, Eval &&eval)
{
  int miss = 0;
  if (st.seq >= 0 && r.t0 - st.t < 3.0)
    {
      int g = (r.seq - st.seq) & 63;
      if (g == 0)
        return;
      miss = g - 1;
    }
  eval (r, miss);
  st.seq = r.seq;
  st.has[0] = r.has[0];
  st.has[1] = r.has[1];
  st.t = r.t0;
}

// prijatie kopie CAM technologiou k so znackou mark a oneskorenim lat [s] v case now [s]. CAM starsie nez 200 ms sa
// uzavru (eval (r, miss) pre kazdu).
template <class Eval>
inline void
qosRx (QNeighbor &st, int k, int mark, double lat, double now, Eval &&eval)
{
  bool has[2] = {(mark & 0x80) != 0, (mark & 0x40) != 0};
  if (!has[0] && !has[1])
    return;
  int seq = mark & 63;
  QRec *r = nullptr;
  for (auto it = st.recs.rbegin (); it != st.recs.rend (); ++it)
    if (it->seq == seq && now - it->t0 < 1.0)
      {
        r = &*it;
        break;
      }
  if (!r)
    {
      st.recs.push_back (QRec {seq, {has[0], has[1]}, now, {false, false}, {0, 0}});
      r = &st.recs.back ();
    }
  if (!r->got[k])
    {
      r->got[k] = true;
      r->lat[k] = lat;
    }
  while (!st.recs.empty () && now - st.recs.front ().t0 > 0.2)
    {
      qosFinish (st, st.recs.front (), eval);
      st.recs.pop_front ();
    }
}

// zabudanie klzavych suctov za krok dt [s] s casovou konstantou tau [s] (v praci dt = 0.1, tau = 2)
inline double
qosDecay (double dt = 0.1, double tau = 2.0)
{
  return std::exp (-dt / tau);
}

// kvalita je znama, ked ma kazda technologia v okne aspon qmin ocakavanych CAM
inline bool
qosKnown (const double exp[2], double qmin)
{
  return exp[0] >= qmin && exp[1] >= qmin;
}

enum class QDecision
{
  NotEligible, // podmienky prepnutia nesplnene (moze rozhodnut ine pravidlo)
  Stay,        // podmienky splnene, ale los nevysiel - tento raz ostan
  Switch       // prejdi na druhu technologiu
};

// pravidlo kvality: gain = kvalita druhej - kvalita hlavnej; loadOther = obsadenost druhej; u = nahodne cislo z [0, 1)
inline QDecision
qosDecide (bool known, double gain, double margin, double loadOther, double loadHigh, double u, double scale = 0.4)
{
  if (!(known && gain > margin && loadOther < loadHigh))
    return QDecision::NotEligible;
  return u < std::min (1.0, gain / scale) ? QDecision::Switch : QDecision::Stay;
}

// xgregorm 2026-11-17: vylepsena predikcia (PX). Zaklad je pravidlo kvality (zisk z aktualneho merania). Mapa kvality
// (spolahlivost technologii v bunkach, z minulych merani aut) sa pouzije len ak je "cerstva": jej hodnota v bunke, kde auto
// prave je, sa s aktualnym meranim auta zhoduje do dev pre obe technologie. Po zmene podmienok (zona zmizla / vznikla)
// mapa drzi stare hodnoty (zabuda pomaly), auto to zisti z nesuhlasu a rozhoduje len z merania (ako Q). Pri cerstvej mape:
//  - prepnutie vopred: druha technologia bude na trase lepsia (zisk podla mapy na horizonte > zisk teraz) a teraz nie je
//    preukazatelne horsia (zisk teraz >= -margin) -> pouzije sa zisk z mapy,
//  - zbytocne prepnutie: teraz je druha lepsia, ale na trase bude preukazatelne horsia (zisk z mapy < -margin) -> 0.
// Vstupy: rNow = aktualne meranie, mNow / hNow = mapa v sucasnej bunke (a ci ma data), mFut / hFut = priemer mapy v bodoch
// trasy o 1..H s, im = index hlavnej technologie. Vracia zisk druhej technologie, ktory ide do qosDecide.
inline double
pxGain (const double rNow[2], const double mNow[2], const bool hNow[2], const double mFut[2], const bool hFut[2], int im,
        double margin, double dev = 0.15, bool *fresh = nullptr)
{
  int io = 1 - im;
  double gNow = rNow[io] - rNow[im];
  bool ok = hNow[0] && hNow[1] && hFut[0] && hFut[1] && std::abs (rNow[0] - mNow[0]) <= dev && std::abs (rNow[1] - mNow[1]) <= dev;
  if (fresh)
    *fresh = ok;
  if (!ok)
    return gNow;
  double gFut = mFut[io] - mFut[im];
  if (gNow > margin && gFut < -margin)
    return 0;
  if (gNow >= -margin && gFut > gNow)
    return gFut;
  return gNow;
}

} // namespace xgcore

#endif
