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
#include <functional>
#include <string>
#include <map>
#include <array>
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
    for (int b : {xghyb::WIFI, xghyb::LTE, xghyb::NR})
      if ((path & b) && s[xghyb::idx (b)] && xghyb::up (b))
        ret = std::max (ret, s[xghyb::idx (b)]->Send (p->Copy (), flags));
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
