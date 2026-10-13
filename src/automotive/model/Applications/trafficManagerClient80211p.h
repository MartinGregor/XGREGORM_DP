#ifndef trafficManagerClient80211p_H
#define trafficManagerClient80211p_H


#include "ns3/application.h"
#include "ns3/asn_utils.h"

#include "ns3/denBasicService.h"
#include "ns3/caBasicService.h"

#include "ns3/btp.h"
#include "ns3/traci-client.h"
#include "ns3/random-variable-stream.h" // xgregorm
#include <map> // xgregorm
#include <array> // xgregorm
#include <set> // xgregorm

namespace ns3 {


class trafficManagerClient80211p : public Application
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

  trafficManagerClient80211p ();

  virtual ~trafficManagerClient80211p ();

  void receiveDENM(denData denm, Address from);
  void receiveCAM (asn1cpp::Seq<CAM>, Address from);
  void StopApplicationNow ();

protected:
  virtual void DoDispose (void);

private:

  DENBasicService m_denService; //!< DEN Basic Service object
  CABasicService m_caService; //!< CA Basic Service object

  Ptr<btp> m_btp; //! BTP object
  Ptr<GeoNet> m_geoNet; //! GeoNetworking Object

  Ptr<Socket> m_socket; //!< Client socket

  virtual void StartApplication (void);
  virtual void StopApplication (void);

  Ptr<TraciClient> m_client; //!< TraCI client
  std::string m_id; //!< vehicle id
  bool m_real_time; //!< To decide wheter to use realtime scheduler
  bool m_print_summary; //!< To print a small summary when vehicle leaves the simulation
  bool m_already_print; //!< To avoid printing two summary
  Ipv4Address m_server_addr; //!< Remote addr

  /* Counters */
  int m_cam_sent;
  int m_denm_received;


  bool m_send_cam;

  Ptr<MetricSupervisor> m_metric_supervisor = nullptr;

  // xgregorm: moj algoritmus CAM podla relevancie (vypnuty = povodne spravanie ETSI) + merania
  bool camGate (bool dynamic);
  void xgTick ();
  void xgReceiveBeacon (Ptr<Socket> socket);
  bool xgRelevant ();
  void xgHybridDecide ();
  int xgEffectivePath ();
  void xgCount (uint64_t &counter);
  unsigned long m_sid = 0;
  bool m_xg_relax = false;
  double m_xg_relevance = 250;         //!< [m] pred semaforom plna frekvencia CAM
  Ptr<Socket> m_xg_socket;             //!< beacony RSU cez 802.11p (adaptivny hybrid)
  EventId m_xg_tick_ev;
  std::string m_next_tls;              //!< najblizsi semafor na trase (z mapy, ako MAPEM)
  double m_next_dist = 1e9;
  bool m_relevant = true;
  double m_last_tx = -10;
  double m_gap_min = 1e9, m_gap_max = 0; //!< [s] odstupy CAM (kontrola ETSI)
  uint64_t m_gap_low = 0, m_gap_high = 0;
  // hybrid 802.11p + LTE + NR-V2X
  uint32_t m_xg_hybrid = 0;            //!< 0 vypnute, 1 vsetky cesty z masky, 2 adaptivne
  uint32_t m_hyb_mask = 1;             //!< technologie v aute: 1 802.11p, 2 LTE, 4 NR-V2X
  double m_hyb_range = 250, m_hyb_qmin = 0.9, m_hyb_dwell = 1.0;
  bool m_on_approach = false;          //!< auto je na ceste, ktoru riadi najblizsi semafor
  std::map<std::string, std::set<std::string>> m_tls_approach; //!< semafor -> jeho prijazdove cesty
  int m_hyb_path = 1;                  //!< maska ciest aktualnej CAM (xghyb::Path)
  double m_hyb_need_t = -10;           //!< kedy auto naposledy potreboval radic (dobeh LTE po opusteni prijazdu)
  int m_hyb_base = 1;                  //!< zvolena kombinacia bez skusky 802.11p (pocitanie prepnuti)
  struct HybLink { double last = -10, q = 1.0, eval = -1; };
  std::map<std::string, std::array<HybLink, 3>> m_hyb_link; //!< tls -> [802.11p, -, NR]: beacon RSU, podiel potvrdenych CAM
  double m_hyb_last_tx[3] = {-10, -10, -10}, m_hyb_ok_since = -1, m_hyb_probe_t = -10;
  uint64_t m_hyb_probes = 0;           //!< skusky povodnej technologie
  Ptr<Socket> m_nr_beacon_socket;      //!< beacony RSU cez NR-V2X
  uint64_t m_hyb_cnt[8] = {0, 0, 0, 0, 0, 0, 0, 0}, m_hyb_switch = 0;
  double m_last_gate_count = -1;
  uint64_t m_xg_relaxed = 0, m_xg_force = 0, m_xg_beacons = 0;
};
}
#endif // trafficManagerClient80211p_H
