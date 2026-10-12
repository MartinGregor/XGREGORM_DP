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

  // xgregorm: moje algoritmy CAM (vsetky vypnute = povodne spravanie ETSI)
  bool camGate (bool dynamic);
  void xgTick ();
  void xgReceiveBeacon (Ptr<Socket> socket);
  void xgRetransmit ();
  bool xgRelevant ();
  void xgCount (uint64_t &counter);
  unsigned long m_sid = 0;
  bool m_xg_spat = false, m_xg_ack = false, m_xg_relax = false;
  double m_xg_hold_max = 1;            //!< [s] najdlhsie drzanie CAM pri cervenej (ETSI max 1 s)
  Ptr<Socket> m_xg_socket;
  EventId m_xg_tick_ev;
  Ptr<UniformRandomVariable> m_xg_jitter;
  std::string m_next_tls;              //!< najblizsi semafor na trase (z mapy, ako MAPEM)
  int m_next_idx = -1;
  double m_next_dist = 1e9;
  double m_x = 0, m_y = 0;
  bool m_red = false;                  //!< posledny SPaT pre moj signal hovori cervena/zlta
  std::string m_red_tls;
  double m_red_time = -10;
  std::map<std::string, std::pair<double, double>> m_rel_heard; //!< tls -> (cas beaconu, relevancia [m])
  bool m_relevant = true;
  double m_last_tx = -10;
  double m_retx_for = -1;              //!< CAM, ktoru ide algoritmus 2 zopakovat
  double m_gap_min = 1e9, m_gap_max = 0; //!< [s] odstupy CAM (kontrola ETSI)
  uint64_t m_gap_low = 0, m_gap_high = 0;
  bool m_ack_wait = false, m_last_was_retx = false, m_retx_now = false;
  double m_last_gate_count = -1;
  uint64_t m_xg_hold = 0, m_xg_relaxed = 0, m_xg_retx = 0, m_xg_force = 0, m_xg_beacons = 0;
};
}
#endif // trafficManagerClient80211p_H
