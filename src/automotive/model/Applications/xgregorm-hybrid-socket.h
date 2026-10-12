// xgregorm: hybridny "socket" pre GeoNet - jedna CAM (jedno rozhodnutie CA sluzby podla ETSI) sa
// podla aktualneho rezimu posle niektorou z technologii alebo viacerymi naraz:
//   WIFI = 802.11p (broadcast, auta aj RSU v dosahu)
//   LTE  = LTE cez eNB a jadro siete (UDP na server v sieti, ten ju po kabli doruci RSU)
//   NR   = NR-V2X sidelink (5G, priamo autam a RSU v dosahu, multicast 225.0.0.0)
// GeoNet o tom nevie, vola len Send(). Prijem ide cez povodne sockety (kazdy ma RecvCallback na GeoNet).
// Vypadok technologie pocas behu (--fail-*): xghyb::up() - prepinac na vypadnutu cestu nic neposle.
#ifndef XGREGORM_HYBRID_SOCKET_H
#define XGREGORM_HYBRID_SOCKET_H

#include "ns3/socket.h"
#include "ns3/packet.h"
#include "ns3/node.h"
#include "ns3/net-device.h"
#include "ns3/simulator.h"
#include <functional>
#include <string>
#include <map>
#include <array>

namespace ns3 {

namespace xghyb {

enum Path { WIFI = 1, LTE = 2, NR = 4 };
inline int idx (int path) { return path == WIFI ? 0 : path == LTE ? 1 : 2; }
inline const char *name (int i) { return i == 0 ? "80211p" : i == 1 ? "LTE" : "NR"; }

// vypadok technologie [s]: od-do, -1 = nikdy
inline double *downFrom () { static double d[3] = {-1, -1, -1}; return d; }
inline double *downTo () { static double d[3] = {-1, -1, -1}; return d; }
inline bool
up (int path)
{
  int i = idx (path);
  double now = Simulator::Now ().GetSeconds ();
  return !(downFrom ()[i] >= 0 && now >= downFrom ()[i] && (downTo ()[i] < 0 || now < downTo ()[i]));
}
// "start:end" alebo "start" -> vypadok technologie path
inline void
setOutage (int path, const std::string &spec)
{
  if (spec.empty ())
    return;
  size_t c = spec.find (':');
  downFrom ()[idx (path)] = std::stod (spec.substr (0, c));
  downTo ()[idx (path)] = c == std::string::npos ? -1 : std::stod (spec.substr (c + 1));
}

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
    Ptr<Socket> s[3] = {m_wifi, m_lte, m_nr};
    for (int b : {xghyb::WIFI, xghyb::LTE, xghyb::NR})
      if ((path & b) && s[xghyb::idx (b)] && xghyb::up (b))
        ret = std::max (ret, s[xghyb::idx (b)]->Send (p->Copy (), flags));
    // vsetky zvolene cesty vypadnute: CAM sa stratila, pre CA sluzbu je vsak odoslana (ako pri chybe radia)
    return ret == -1 ? (int) p->GetSize () : ret;
  }

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
};

} // namespace ns3

#endif /* XGREGORM_HYBRID_SOCKET_H */
