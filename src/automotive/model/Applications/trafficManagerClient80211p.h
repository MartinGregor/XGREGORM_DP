#ifndef trafficManagerClient80211p_H
#define trafficManagerClient80211p_H


#include "ns3/xgregorm-stats.h" // xgregorm
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
#include <deque> // xgregorm
#include "ns3/xgregorm-hybrid-socket.h" // xgregorm

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
  int xgPathNow ();
  int xgConnNow ();                    //!< xgregorm: priame technologie s kontaktom s RSU za poslednu 1 s (priebezny vypis)
  double m_rsu_heard[3] = {-10, -10, -10}; //!< kedy auto naposledy pocul RSU cez 802.11p / - / NR                    //!< xgregorm: cesty CAM prave teraz bez skusky 802.11p (farba, priebezny vypis)
  void xgColorTick ();                 //!< xgregorm: farba auta v SUMO podla technologie (vizualizacia)
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
  bool m_xg_color = false;             //!< farbit auto podla technologie
  bool m_xg_security = false;          //!< xgregorm: podpis sprav (GeoNet security autorov)
  int m_xg_color_last = -1;
  EventId m_xg_color_ev;
  // xgregorm: vyber technologie podla zataze (XgLoadRat): hlavna priama technologia 802.11p -> NR-V2X pri zahlteni
  void xgLoadUpdate (int avail);
  static int xgOtherDirect (int b) { return b == 1 ? 4 : 1; } // 802.11p (1) <-> NR-V2X (4)
  bool m_xg_load = false;
  double m_cbr_high = 0.3, m_cbr_low = 0.2, m_lte_high = 0.6, m_load_period = 1.0;
  double m_load_avg[3] = {0, 0, 0};    //!< klzavy priemer zataze (~1 s): CBR 802.11p, PRB LTE bunky, SL-CBR NR
  bool m_load_init = false;
  int m_main = 1;                      //!< hlavna priama technologia (xghyb::WIFI / NR)
  double m_load_eval_t = -10;
  uint64_t m_load_sw = 0;              //!< presuny hlavnej technologie kvoli zatazi
  Ptr<UniformRandomVariable> m_load_rnd;
  double m_lte_thr = 0.6;              //!< prah PRB bunky pre zalohu LTE tohto auta (nahodne okolo XgLteHigh)
  // xgregorm: vyber technologie podla nameranej kvality (XgQosRat): spolahlivost = podiel CAM susedov do 100 m
  // dorucenych nacas (<= XgLatBudget), kazda priama technologia zvlast; straty z poradoveho cisla v znacke GeoNet
  void xgRxGn (Ptr<Socket> socket);
  static void xgRxGnStatic (Ptr<GeoNet>, trafficManagerClient80211p *c, Ptr<Socket> socket) { c->xgRxGn (socket); }
  void xgQosRx (unsigned long sender, int k, int mark, long gdt);
  int xgMark ();
  bool m_xg_qos = false;
  double m_qos_margin = 0.05, m_lat_budget = 0.1;
  Ptr<XgRxProxySocket> m_rx_proxy;
  int m_rx_mark = -1;                  //!< znacka prave spracuvaneho prijateho paketu (-1 = ziadna)
  uint8_t m_tx_seq = 0;
  struct QRec { int seq; bool has[2]; double t0; bool got[2]; double lat[2]; };
  struct QSt { std::deque<QRec> recs; int seq = -1; bool has[2] = {false, false}; double t = -10;
               double x = 0, y = 0, vx = 0, vy = 0, pt = -10; };
  std::map<unsigned long, QSt> m_qst;  //!< odosielatel -> CAM cakajuce na vyhodnotenie + posledna vyhodnotena
  void xgQosFinish (unsigned long sender, QSt &st, const QRec &r);
  double m_q_exp[2] = {0, 0}, m_q_ok[2] = {0, 0}; //!< klzave sucty (~2 s): ocakavane / dorucene nacas
  uint64_t m_qt_exp[2] = {0, 0}, m_qt_rx[2] = {0, 0}, m_qt_ok[2] = {0, 0}; //!< celkove sucty (vypis, porovnanie so skutocnostou)
  uint64_t m_qos_sw = 0;               //!< presuny hlavnej technologie kvoli kvalite
  // xgregorm: prediktivny vyber (XgPredRat): spolahlivost a zataz technologii na nasledujucich XgPredHorizon s
  // z mapy kvality (xgstats::rem) v bodoch trasy auta; rozhodnutie inak rovnake ako pravidlo kvality
  bool m_xg_pred = false;
  double m_pred_h = 3;
  void xgPredict (double q[2], int nq[2], double c[2], int nc[2], std::vector<std::pair<double, double>> *pts = nullptr);
  double m_qc_exp[2] = {0, 0}, m_qc_ok[2] = {0, 0}; //!< celkove sucty od zaciatku (vyhodnotenie predikcie)
  struct PRec { double t; double pred[2]; double now[2]; double e0[2]; double o0[2]; bool map[2]; };
  std::deque<PRec> m_pred_q;           //!< predikcie cakajuce na vyhodnotenie (po XgPredHorizon s)
  std::vector<std::string> m_route;
  uint64_t m_pred_sw = 0;              //!< presuny hlavnej technologie podla predikcie
  // xgregorm: cista politika (XgClean): kazda CAM jednou priamou technologiou, bez kopii a bez beaconu RSU; LTE len na
  // vzdialenom prijazde (> XgCellDist od RSU). Kvalita druhej technologie sa zistuje riedkymi skuskami (XgProbeHz)
  bool m_xg_clean = false;
  uint32_t m_init_main = 0;            //!< pociatocna hlavna technologia: 0 802.11p, 1 NR, 2 nahodne pre auto
  double m_probe_hz = 0, m_cell_dist = 150, m_probe_t = -10;
  bool m_probe_due = false;            //!< dalsia CAM ide aj druhou technologiou
  // xgregorm: dalsie sposoby vyberu (plan, cast 8): 1 DCC po spravach, 2 vybava susedov, 3 vybava RSU na trase,
  // 4 selektivna kopia pri krizovatke, 5 pri krizovatke technologia odolnejsia za rohom
  double m_dcc_off_t = -1;
  bool m_dcc_off_v = false;
  bool m_nb_equip = false, m_rsu_route = false;
  double m_dup_near = 0, m_geo_rat = 0;
  // xgregorm 2026-11-03: vstupy z prijatych sprav (len do --feat-log) a ucenie za jazdy (XgBandit)
  void xgMsgFeatures (double now, double h, xgstats::MsgFeat &f);
  double m_stop_since = -1, m_prev_v = 0;
  bool m_bandit = false;
  bool m_bandit_probe = false;
  double m_bd_exp[2] = {0, 0}, m_bd_ok[2] = {0, 0}; //!< CAM susedov za posledne vyhodnotenie
  double m_bd_s[8][2] = {}, m_bd_n[8][2] = {};      //!< zabudajuce sucty podla situacie a technologie
  int m_bd_c = 0;                                   //!< situacia v poslednom vyhodnoteni
  bool m_oracle = false; //!< referencia: auto presne pozna zony --zones (nie je realizovatelna politika)
  void xgRemAdd (double x, double y, int k, double exp, double ok, double cbr, double cbrN);
  void xgRemFlush (double now);
  xgstats::RemBuf m_rem_buf;
  double m_rem_last = 0;
  Ptr<Socket> m_rem_sock;
  void xgNeighbours (double nb[1], double nbk[2], double nbf[1], double h);
  double m_my_x = 0, m_my_y = 0, m_my_vx = 0, m_my_vy = 0, m_my_t = -10;
};
}
#endif // trafficManagerClient80211p_H
