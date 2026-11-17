// xgregorm: hybridny "socket" pre GeoNet - jedna CAM (jedno rozhodnutie CA sluzby podla ETSI) sa
// podla aktualneho rezimu posle niektorou z technologii alebo viacerymi naraz:
//   WIFI = 802.11p (broadcast, auta aj RSU v dosahu)
//   LTE  = LTE cez eNB a jadro siete (UDP na server v sieti, ten ju po kabli doruci RSU)
//   NR   = NR-V2X sidelink (5G, priamo autam a RSU v dosahu, multicast 225.0.0.0)
// GeoNet o tom nevie, vola len Send(). Prijem ide cez povodne sockety (kazdy ma RecvCallback na GeoNet).
// Vypadky technologie pocas behu (--fail-*, aj viac intervalov): xghyb::up() - prepinac na vypadnutu cestu nic neposle.
#ifndef XGREGORM_HYBRID_SOCKET_H
#define XGREGORM_HYBRID_SOCKET_H

#include "ns3/socket.h"
#include "ns3/packet.h"
#include "ns3/node.h"
#include "ns3/net-device.h"
#include "ns3/simulator.h"
#include "ns3/header.h"
#include "ns3/tag.h"
#include <functional>
#include <string>
#include <map>
#include <array>
#include <deque>
#include <algorithm>
#include <cmath>
#include <vector>
#include <sstream>

namespace ns3 {

namespace xghyb {

enum Path { WIFI = 1, LTE = 2, NR = 4 };
inline int idx (int path) { return path == WIFI ? 0 : path == LTE ? 1 : 2; }
inline const char *name (int i) { return i == 0 ? "80211p" : i == 1 ? "LTE" : "NR"; }

// vypadky technologie [s]: zoznam intervalov od-do (do < 0 = do konca behu)
inline std::vector<std::pair<double, double>> *outages () { static std::vector<std::pair<double, double>> v[3]; return v; }
inline bool
up (int path)
{
  double now = Simulator::Now ().GetSeconds ();
  for (const auto &o : outages ()[idx (path)])
    if (now >= o.first && (o.second < 0 || now < o.second))
      return false;
  return true;
}
// "od:do", "od" (do konca) alebo viac intervalov "od:do,od:do,..." -> vypadky technologie path
inline void
setOutage (int path, const std::string &spec)
{
  std::stringstream ss (spec);
  std::string one;
  while (std::getline (ss, one, ','))
    {
      if (one.empty ())
        continue;
      size_t c = one.find (':');
      outages ()[idx (path)].push_back ({std::stod (one.substr (0, c)),
                                         c == std::string::npos ? -1 : std::stod (one.substr (c + 1))});
    }
}

// model velkosti podpisu ETSI TS 103 097 (--security 1): bajty pridane ku kazdej sprave; namerane na implementacii
// autorov (GeoNet security): s odtlackom certifikatu +93 B, s celym certifikatom (raz za 1 s) +242 B; 0 = vypnute
inline uint32_t &secDigest () { static uint32_t v = 0; return v; }
inline uint32_t &secCert () { static uint32_t v = 0; return v; }

// casova os: pocet CAM za sekundu podla kombinacie ciest, ktorymi naozaj odisli (index = maska 0..7)
inline std::map<int, std::array<uint64_t, 8>> &timeline () { static std::map<int, std::array<uint64_t, 8>> m; return m; }

// zariadenie daneho typu na uzle (napr. "ns3::NrUeNetDevice"), bez zavislosti modulu automotive na nr
inline Ptr<NetDevice>
findDevice (Ptr<Node> node, const std::string &type)
{
  for (uint32_t i = 0; i < node->GetNDevices (); i++)
    if (node->GetDevice (i)->GetInstanceTypeId ().GetName () == type)
      return node->GetDevice (i);
  return nullptr;
}

} // namespace xghyb

// xgregorm: riadenie zahltenia (--dcc, --nr-cr), spolocne pre CAM (prepinac) aj dalsiu prevadzku (SPATEM, MAPEM, CPM)
//  802.11p: reaktivne DCC podla ETSI TS 102 687 (hodnoty ako modul DCC autorov, tab. A.1/A.2): stav podla CBR stanice
//           (Relaxed < 0.30, Active1 < 0.40, Active2 < 0.50, Active3 < 0.65, inak Restrictive; vyhodnotenie raz za
//           100 ms, vzdy len do susedneho stavu). Po kazdom ramci je brana zatvorena Toff (Ton <= 0.5 ms: 50/100/200/250/
//           1000 ms, Ton <= 1 ms: 100/200/400/500/1000 ms; dlhsi ramec Toff x Ton/1 ms, najviac 1 s - norma tabulky pre
//           Ton > 1 ms nema, zachovava sa podiel casu vysielania stavu, napr. Relaxed 1 %); spravy cakaju vo fronte podla priority
//           (DP1 SPATEM/MAPEM, DP2 CAM, DP3 CPM a beacon GeoNet), novsia sprava rovnakeho druhu nahradi cakajucu,
//           po 1 s sa zahodi. Navyse CA a CP sluzba (T_GenCam_DCC, T_GenCpm podla EN 302 637-2 / TS 103 324) nevytvaraju
//           spravy castejsie nez Toff stavu (camGap). Znizenie vykonu z tabulky autorov sa nepouziva (len frekvencia).
//  NR-V2X: limit CR (podiel vlastnych obsadenych subkanalov x slotov za poslednu 1 s) podla SL-CBR, ETSI TS 103 574
//           tab. 1, PPPP3-5: <= 0.3 bez limitu, <= 0.65 0.03, <= 0.8 0.006, inak 0.003. Pri CR >= limit sa kopia
//           spravy cez NR zahodi (3GPP TS 38.321: UE musi CR udrzat pod limitom). SL-CBR = odhad zo scenara (dolny odhad).
namespace xgdcc {
enum Kind { CAM = 0, GNB = 1, SPAT = 2, MAP = 3, CPM = 4 };
inline bool &wOn () { static bool b = false; return b; }
inline bool &nrOn () { static bool b = false; return b; }
inline std::function<double (uint32_t)> &cbrW () { static std::function<double (uint32_t)> f; return f; }
inline std::function<double (uint32_t)> &cbrN () { static std::function<double (uint32_t)> f; return f; }
inline double &nrCap () { static double c = 0; return c; } // subkanaly x sloty za 1 s
struct QPkt
{
  Ptr<Socket> s;
  Ptr<Packet> p;
  double t;
  int prio, kind;
};
struct WSt
{
  double open = 0, eval = -1;
  int state = 0;
  bool sched = false;
  std::deque<QPkt> q;
};
inline std::map<uint32_t, WSt> &wst () { static std::map<uint32_t, WSt> m; return m; }
inline std::map<uint32_t, std::deque<std::pair<double, double>>> &nrUse ()
{
  static std::map<uint32_t, std::deque<std::pair<double, double>>> m;
  return m;
}
// [0] 802.11p hned, [1] do fronty, [2] z fronty, [3] nahradene novsou, [4] zahodene po 1 s, [5] NR odoslane,
// [6] NR zahodene (CR), [7] CAM nevytvorene (T_GenCam_DCC), [8] CPM nevytvorene
inline std::array<uint64_t, 9> &cnt () { static std::array<uint64_t, 9> a {}; return a; }
inline std::array<uint64_t, 5> &stateN () { static std::array<uint64_t, 5> a {}; return a; } // stav pri sprave
inline double &qDelay () { static double d = 0; return d; }
inline double &crSum () { static double d = 0; return d; }

// trvanie ramca 802.11p (OFDM, 10 MHz): preambula + SIGNAL 40 us, symbol 8 us; bity dat na symbol podla rychlosti
// broadcastu (--wifi-bcast-rate): 3 Mbit/s = 24 (predvolene ns-3, pozn. xgregorm 2026-11-02: povodny komentar tu chybne
// uvadzal 6 Mbit/s), 6 Mbit/s = 48. MAC hlavicka + LLC + FCS 36 B, SERVICE 16 + tail 6 bitov
inline double &bitsPerSymbol () { static double b = 24; return b; }
inline double
airtime (uint32_t bytes)
{
  return 40e-6 + std::ceil ((22 + 8.0 * (bytes + 36)) / bitsPerSymbol ()) * 8e-6;
}
inline int stateOf (double c) { return c < 0.30 ? 0 : c < 0.40 ? 1 : c < 0.50 ? 2 : c < 0.65 ? 3 : 4; }
inline double
toff (int st, double ton)
{
  static const double a[5] = {0.05, 0.1, 0.2, 0.25, 1.0}, b[5] = {0.1, 0.2, 0.4, 0.5, 1.0};
  if (ton <= 0.5e-3)
    return a[st];
  return std::min (1.0, b[st] * std::max (1.0, ton / 1e-3));
}
// TS 102 687 cl. 5.3: stav sa vyhodnoti raz za T_CBR (100 ms) a prechadza len do susedneho stavu
inline void
updState (uint32_t n, WSt &w)
{
  double now = Simulator::Now ().GetSeconds ();
  if (w.eval >= 0 && now - w.eval < 0.1 - 1e-9)
    return;
  w.eval = now;
  int t = stateOf (cbrW () ? cbrW () (n) : 0);
  w.state += t > w.state ? 1 : t < w.state ? -1 : 0;
}
// minimalny odstup sprav, ktore facility vrstva vytvara (T_GenCam_DCC): Toff stavu pre typicku CAM (< 0.5 ms)
inline double
camGap (uint32_t n)
{
  if (!wOn ())
    return 0;
  WSt &w = wst ()[n];
  updState (n, w);
  return toff (w.state, 0);
}
inline void flush (uint32_t n);
inline void
schedule (uint32_t n, WSt &w)
{
  if (w.sched || w.q.empty ())
    return;
  w.sched = true;
  double dt = std::max (0.0, w.open - Simulator::Now ().GetSeconds ());
  Simulator::Schedule (Seconds (dt), &flush, n);
}
inline void
txNow (WSt &w, const QPkt &q)
{
  q.s->Send (q.p);
  w.open = Simulator::Now ().GetSeconds () + toff (w.state, airtime (q.p->GetSize ()));
}
inline void
flush (uint32_t n)
{
  WSt &w = wst ()[n];
  w.sched = false;
  double now = Simulator::Now ().GetSeconds ();
  for (auto it = w.q.begin (); it != w.q.end ();)
    if (now - it->t > 1.0)
      {
        it = w.q.erase (it);
        cnt ()[4]++;
      }
    else
      ++it;
  if (w.q.empty ())
    return;
  if (now < w.open - 1e-9)
    {
      schedule (n, w);
      return;
    }
  updState (n, w);
  auto best = std::min_element (w.q.begin (), w.q.end (), [] (const QPkt &a, const QPkt &b) {
    return a.prio != b.prio ? a.prio < b.prio : a.t < b.t;
  });
  QPkt q = *best;
  w.q.erase (best);
  qDelay () += now - q.t;
  cnt ()[2]++;
  txNow (w, q);
  schedule (n, w);
}
// odoslanie cez 802.11p cez branu DCC
inline int
wSend (Ptr<Socket> s, Ptr<Packet> p, int kind)
{
  if (!wOn ())
    return s->Send (p);
  static const int prio[5] = {2, 3, 1, 1, 3};
  uint32_t n = s->GetNode ()->GetId ();
  WSt &w = wst ()[n];
  updState (n, w);
  stateN ()[w.state]++;
  double now = Simulator::Now ().GetSeconds ();
  QPkt q {s, p, now, prio[kind], kind};
  if (w.q.empty () && now >= w.open - 1e-9)
    {
      cnt ()[0]++;
      txNow (w, q);
      return p->GetSize ();
    }
  for (QPkt &e : w.q)
    if (e.kind == kind)
      {
        e = q;
        cnt ()[3]++;
        return p->GetSize ();
      }
  w.q.push_back (q);
  cnt ()[1]++;
  schedule (n, w);
  return p->GetSize ();
}
// xgregorm: napad 1 - za ako dlho by sa sprava dostala cez branu DCC (zatvorena brana + cakajuce spravy po ~Toff stavu)
inline double &offload () { static double b = 0; return b; } // rozpocet [s]; 0 = vypnute
inline double
gateWait (uint32_t n)
{
  if (!wOn ())
    return 0;
  WSt &w = wst ()[n];
  return std::max (0.0, w.open - Simulator::Now ().GetSeconds ()) + w.q.size () * toff (w.state, 0);
}
inline uint64_t &offloadN () { static uint64_t c = 0; return c; }
// vlastne obsadenie NR (subkanaly x sloty) pri kazdom vlastnom PSSCH - vola scenar
inline void
nrUsed (uint32_t n, double occ)
{
  nrUse ()[n].push_back ({Simulator::Now ().GetSeconds (), occ});
}
inline double
crOf (uint32_t n)
{
  auto &d = nrUse ()[n];
  double now = Simulator::Now ().GetSeconds (), u = 0;
  while (!d.empty () && now - d.front ().first > 1.0)
    d.pop_front ();
  for (auto &e : d)
    u += e.second;
  return nrCap () > 0 ? u / nrCap () : 0;
}
inline double
crLimit (double cbr)
{
  return cbr <= 0.3 ? 1.0 : cbr <= 0.65 ? 0.03 : cbr <= 0.8 ? 0.006 : 0.003;
}
// odoslanie cez NR s limitom CR
inline int
nrSend (Ptr<Socket> s, Ptr<Packet> p)
{
  if (!nrOn ())
    return s->Send (p);
  uint32_t n = s->GetNode ()->GetId ();
  double cr = crOf (n);
  crSum () += cr;
  if (cr >= crLimit (cbrN () ? cbrN () (n) : 0))
    {
      cnt ()[6]++;
      return p->GetSize ();
    }
  cnt ()[5]++;
  return s->Send (p);
}
} // namespace xgdcc

// xgregorm: porovnavacia metoda DICART (Karoui a kol., IWCMC 2022) - oneskorenie v pristupovej vrstve (AD): cas od
// odovzdania spravy prepinacu po zaciatok jej vysielania (fronta DCC 802.11p + pristup CSMA, pri NR vyber prostriedku
// mode 2). Kopia spravy dostane znacku (byte tag prezije hlavicky aj skladanie RLC), pri vysielani sa zmeria; stanica
// pozna len svoje vlastne casy. Bez --dicart sa nic neznaci.
class XgSendTimeTag : public Tag
{
public:
  uint64_t id = 0;
  static TypeId
  GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::XgSendTimeTag").SetParent<Tag> ().AddConstructor<XgSendTimeTag> ();
    return tid;
  }
  TypeId GetInstanceTypeId (void) const override { return GetTypeId (); }
  uint32_t GetSerializedSize (void) const override { return 8; }
  void Serialize (TagBuffer i) const override { i.WriteU64 (id); }
  void Deserialize (TagBuffer i) override { id = i.ReadU64 (); }
  void Print (std::ostream &os) const override { os << "xgId=" << id; }
};
namespace xgdic {
inline bool &on () { static bool b = false; return b; }
struct Pend
{
  uint32_t node;
  int k; // 0 = 802.11p, 1 = NR
  double t;
};
inline std::map<uint64_t, Pend> &pend () { static std::map<uint64_t, Pend> m; return m; }
// vzorky AD po uzloch: kluc uzol * 2 + k -> (cas vysielania, oneskorenie)
inline std::map<uint32_t, std::deque<std::pair<double, double>>> &ad ()
{
  static std::map<uint32_t, std::deque<std::pair<double, double>>> m;
  return m;
}
inline std::array<double, 2> &adSum () { static std::array<double, 2> a {}; return a; }
inline std::array<uint64_t, 2> &adN () { static std::array<uint64_t, 2> a {}; return a; }
inline std::array<double, 2> &rate () { static std::array<double, 2> a {6e6, 6e6}; return a; } // nominalna rychlost [b/s]
inline void
tag (Ptr<Packet> p, uint32_t node, int k)
{
  static uint64_t next = 1;
  XgSendTimeTag t;
  t.id = next++;
  p->AddByteTag (t);
  double now = Simulator::Now ().GetSeconds ();
  pend ()[t.id] = {node, k, now};
  // spravy, ktore sa nikdy nevyslali (nahradene vo fronte DCC, zahodene limitom CR): zabudnut po 2 s
  while (!pend ().empty () && now - pend ().begin ()->second.t > 2.0)
    pend ().erase (pend ().begin ());
}
// zaciatok vysielania paketu (prvy prenos; opakovania HARQ uz znacku v zozname nemaju)
inline void
onTx (Ptr<const Packet> p)
{
  ByteTagIterator it = p->GetByteTagIterator ();
  while (it.HasNext ())
    {
      ByteTagIterator::Item i = it.Next ();
      if (i.GetTypeId () != XgSendTimeTag::GetTypeId ())
        continue;
      XgSendTimeTag t;
      i.GetTag (t);
      auto e = pend ().find (t.id);
      if (e == pend ().end ())
        continue;
      double now = Simulator::Now ().GetSeconds (), d = now - e->second.t;
      auto &q = ad ()[e->second.node * 2 + e->second.k];
      q.push_back ({now, d});
      while (q.size () > 200)
        q.pop_front ();
      adSum ()[e->second.k] += d;
      adN ()[e->second.k]++;
      pend ().erase (e);
    }
}
} // namespace xgdic

// xgregorm: zakladna hlavicka GeoNet (ETSI EN 302 636-4-1, 4 B) ako surove bajty - prepinac do jej pola Reserved
// (bajt 1, GeoNet autorov ho pise 0 a pri prijme ignoruje) zapise znacku pre vyber technologie podla kvality
// (XgQosRat): bit 7 = CAM ide aj cez 802.11p, bit 6 = aj cez NR-V2X, bity 0-5 = poradove cislo CAM (mod 64)
class XgGnBasicRaw : public Header
{
public:
  uint8_t b[4] = {0, 0, 0, 0};
  static TypeId
  GetTypeId (void)
  {
    static TypeId tid = TypeId ("ns3::XgGnBasicRaw").SetParent<Header> ().SetGroupName ("Automotive");
    return tid;
  }
  TypeId GetInstanceTypeId (void) const override { return GetTypeId (); }
  uint32_t GetSerializedSize (void) const override { return 4; }
  void Serialize (Buffer::Iterator i) const override { for (int k = 0; k < 4; k++) i.WriteU8 (b[k]); }
  uint32_t Deserialize (Buffer::Iterator i) override { for (int k = 0; k < 4; k++) b[k] = i.ReadU8 (); return 4; }
  void Print (std::ostream &os) const override { os << (int) b[1]; }
};

// xgregorm: socket, ktory GeoNet podstrcime pri prijme - klient paket precita sam (znacka v hlavicke), potom ho
// cez tento socket odovzda GeoNet::receiveGN (ten zavola RecvFrom prave raz)
class XgRxProxySocket : public Socket
{
public:
  explicit XgRxProxySocket (Ptr<Node> node) : m_node (node) {}
  void Set (Ptr<Packet> p, const Address &from) { m_p = p; m_from = from; }
  Ptr<Packet>
  RecvFrom (uint32_t, uint32_t, Address &from) override
  {
    from = m_from;
    Ptr<Packet> p = m_p;
    m_p = nullptr;
    return p;
  }
  Ptr<Packet> Recv (uint32_t, uint32_t) override { Ptr<Packet> p = m_p; m_p = nullptr; return p; }
  Ptr<Node> GetNode (void) const override { return m_node; }
  enum SocketErrno GetErrno (void) const override { return ERROR_NOTERROR; }
  enum SocketType GetSocketType (void) const override { return NS3_SOCK_DGRAM; }
  int Bind (const Address &) override { return 0; }
  int Bind () override { return 0; }
  int Bind6 () override { return 0; }
  int Close (void) override { return 0; }
  int ShutdownSend (void) override { return 0; }
  int ShutdownRecv (void) override { return 0; }
  int Connect (const Address &) override { return 0; }
  int Listen (void) override { return -1; }
  uint32_t GetTxAvailable (void) const override { return 0; }
  int Send (Ptr<Packet>, uint32_t) override { return -1; }
  int SendTo (Ptr<Packet>, uint32_t, const Address &) override { return -1; }
  uint32_t GetRxAvailable (void) const override { return m_p ? m_p->GetSize () : 0; }
  int GetSockName (Address &) const override { return -1; }
  int GetPeerName (Address &) const override { return -1; }
  bool SetAllowBroadcast (bool) override { return true; }
  bool GetAllowBroadcast () const override { return true; }

private:
  Ptr<Node> m_node;
  Ptr<Packet> m_p;
  Address m_from;
};

class XgHybridSocket : public Socket
{
public:
  // wifi je vzdy (zariadenie 0, z neho GeoNet berie adresu); lte a nr mozu byt nullptr (technologia vypnuta)
  XgHybridSocket (Ptr<Socket> wifi, Ptr<Socket> lte, Ptr<Socket> nr, std::function<int ()> select)
    : m_wifi (wifi), m_lte (lte), m_nr (nr), m_select (select) {}

  int
  Send (Ptr<Packet> p, uint32_t flags) override
  {
    int path = m_select ();
    int ret = -1;
    // xgregorm: --security 1 - podpis ako doplnenie na konci (GeoNet prijemcu cita payload podla dlzky v hlavicke)
    if (xghyb::secDigest ())
      {
        double now = Simulator::Now ().GetSeconds ();
        bool cert = now - m_lastCert >= 1.0;
        if (cert)
          m_lastCert = now;
        p = p->Copy ();
        p->AddPaddingAtEnd (cert ? xghyb::secCert () : xghyb::secDigest ());
      }
    // xgregorm: znacka do pola Reserved zakladnej hlavicky GeoNet (len CAM a ine spravy, nie beacon GeoNet: ten
    // nema poradove cislo CAM; len bez skutocnej kryptografie - pri --security 2 nasleduje zabezpecena hlavicka)
    if (m_mark && p->GetSize () >= 6)
      {
        uint8_t h[6];
        p->CopyData (h, 6);
        if ((h[0] & 0x0f) == 1 && (h[5] >> 4) != 1)
          {
            int m = m_mark ();
            if (m >= 0)
              {
                p = p->Copy ();
                XgGnBasicRaw raw;
                p->RemoveHeader (raw);
                raw.b[1] = (uint8_t) m;
                p->AddHeader (raw);
              }
          }
      }
    Ptr<Socket> s[3] = {m_wifi, m_lte, m_nr};
    // xgregorm: --dcc / --nr-cr - 802.11p cez branu DCC, NR s limitom CR (druh spravy: beacon GeoNet / CAM)
    int kind = xgdcc::CAM;
    if (p->GetSize () >= 6)
      {
        uint8_t h[6];
        p->CopyData (h, 6);
        if ((h[0] & 0x0f) == 1 && (h[5] >> 4) == 1)
          kind = xgdcc::GNB;
      }
    for (int b : {xghyb::WIFI, xghyb::LTE, xghyb::NR})
      if ((path & b) && s[xghyb::idx (b)] && xghyb::up (b))
        {
          Ptr<Packet> c = p->Copy ();
          if (xgdic::on () && b != xghyb::LTE)
            xgdic::tag (c, m_wifi->GetNode ()->GetId (), b == xghyb::NR);
          int r = b == xghyb::WIFI ? xgdcc::wSend (s[0], c, kind) : b == xghyb::NR ? xgdcc::nrSend (s[2], c)
                                                                                  : s[1]->Send (c, flags);
          ret = std::max (ret, r);
        }
    // vsetky zvolene cesty vypadnute: CAM sa stratila, pre CA sluzbu je vsak odoslana (ako pri chybe radia)
    return ret == -1 ? (int) p->GetSize () : ret;
  }

  void SetMark (std::function<int ()> mark) { m_mark = mark; }

  // GeoNet z tohto socketu cita len uzol (adresa GN = zariadenie 0 = 802.11p), ostatne len deleguje
  Ptr<Node> GetNode (void) const override { return m_wifi->GetNode (); }
  enum SocketErrno GetErrno (void) const override { return m_wifi->GetErrno (); }
  enum SocketType GetSocketType (void) const override { return m_wifi->GetSocketType (); }
  int Bind (const Address &a) override { return m_wifi->Bind (a); }
  int Bind () override { return m_wifi->Bind (); }
  int Bind6 () override { return m_wifi->Bind6 (); }
  int Close (void) override { return m_wifi->Close (); }
  int ShutdownSend (void) override { return m_wifi->ShutdownSend (); }
  int ShutdownRecv (void) override { return m_wifi->ShutdownRecv (); }
  int Connect (const Address &a) override { return m_wifi->Connect (a); }
  int Listen (void) override { return -1; }
  uint32_t GetTxAvailable (void) const override { return m_wifi->GetTxAvailable (); }
  int SendTo (Ptr<Packet> p, uint32_t flags, const Address &to) override { return m_wifi->SendTo (p, flags, to); }
  uint32_t GetRxAvailable (void) const override { return 0; }
  Ptr<Packet> Recv (uint32_t, uint32_t) override { return nullptr; }
  Ptr<Packet> RecvFrom (uint32_t, uint32_t, Address &) override { return nullptr; }
  int GetSockName (Address &a) const override { return m_wifi->GetSockName (a); }
  int GetPeerName (Address &a) const override { return m_wifi->GetPeerName (a); }
  bool SetAllowBroadcast (bool b) override { return m_wifi->SetAllowBroadcast (b); }
  bool GetAllowBroadcast () const override { return m_wifi->GetAllowBroadcast (); }

private:
  Ptr<Socket> m_wifi, m_lte, m_nr;
  std::function<int ()> m_select;
  std::function<int ()> m_mark;
  double m_lastCert = -10;
};

} // namespace ns3

#endif /* XGREGORM_HYBRID_SOCKET_H */
