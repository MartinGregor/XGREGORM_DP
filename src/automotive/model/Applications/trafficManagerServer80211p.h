#ifndef trafficManagerServer80211p_H
#define trafficManagerServer80211p_H



#include "ns3/application.h"
#include "ns3/asn_utils.h"
#include "ns3/vdp.h"
#include "ns3/denBasicService.h"
#include "ns3/caBasicService.h"
#include "ns3/btp.h"
#include "ns3/btpHeader.h"
#include <mutex>
#include <unordered_map>
#include "ns3/traci-client.h"
#include "ns3/ipv4-address.h" // xgregorm
#include "ns3/xgregorm-stats.h" // xgregorm
#include <set>
namespace ns3 {

class trafficManagerServer80211p : public Application
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId (void);

    trafficManagerServer80211p ();

    virtual ~trafficManagerServer80211p ();

    /**
     * \brief Callback to handle a CAM reception.
     *
     * This function is called everytime a packet is received by the CABasicService.
     *
     * \param the ASN.1 CAM structure containing the info of the packet that was received.
     */
    void receiveCAM (asn1cpp::Seq<CAM> cam, Address address);

    void receiveDENM(denData denm, Address from);

    void StopApplicationNow ();
 
  protected:
    virtual void DoDispose (void);

  private:

    DENBasicService m_denService; //!< DEN Basic Service object
    CABasicService m_caService; //!< CA Basic Service object

    Ptr<btp> m_btp; //! BTP object
    Ptr<GeoNet> m_geoNet; //! GeoNetworking Object

    Ptr<Socket> m_socket; //!< Server socket

    virtual void StartApplication (void);
    virtual void StopApplication (void);

     /**
     * @brief This function compute the milliseconds elapsed from 2004-01-01
    */
    long compute_timestampIts ();

    void phase1Timeout(void);
    void phase2Timeout(void);
    void phaseTimeout(std::string intersection);
    double tlsDistance(std::string tlsid, libsumo::TraCIPosition rsuPos); // xgregorm
    // xgregorm: koordinacia krizovatiek po kabli
    void applyVehiclePosition(unsigned long stationId, std::string edgeID);
    void sendCoord(unsigned long stationId, std::string edgeID);
    void receiveCoord(Ptr<Socket> socket);
    void applyCoordDelayed(unsigned long stationId, std::string edgeID, Time reported);
    // xgregorm: adaptivny hybrid - beacon so zoznamom pocutych CAM (spatna vazba pre auta)
    void xgSendBeacon();

    uint8_t edgeCheck(double,double,double);

    Ptr<TraciClient> m_client; //!< TraCI client
    bool m_real_time; //!< To decide wheter to use realtime scheduler

    std::unordered_map<std::string,std::vector<std::string>> m_tls_lane_map;
    unsigned long m_tls_lanes_number;

    /* Counters */
    u_int m_cam_received;

    bool m_send_cam;

    std::string m_id;

    uint16_t m_phase_time;
    double m_threshold;
    double m_tls_radius; //!< xgregorm: 0 = all traffic lights, > 0 = only those within this radius [m]
    bool m_coord; //!< xgregorm: share vehicle positions with other RSUs over the wired backhaul
    Ipv4Address m_backhaul_bcast; //!< xgregorm
    Ptr<Socket> m_coord_socket; //!< xgregorm
    std::map <unsigned long, std::string> m_last_edge_seen; //!< xgregorm: last edge announced/known per vehicle
    std::map <unsigned long, Time> m_veh_last_update; //!< xgregorm: time of the newest position per vehicle
    uint64_t m_coord_tx; //!< xgregorm
    double m_coord_lookahead; //!< xgregorm: count announced vehicles this many s before arrival (0 = at once)
    uint64_t m_coord_rx; //!< xgregorm
    uint32_t m_xg_beacon_ms; //!< xgregorm: perioda beaconu adaptivneho hybridu [ms]
    Ptr<Socket> m_xg_socket; //!< xgregorm
    EventId m_xg_beacon_ev; //!< xgregorm
    std::set<unsigned long> m_xg_heard; //!< xgregorm: CAM pocute od posledneho beaconu
    double m_xg_win_start = 0; //!< xgregorm
    double m_xg_last_cam = -10; //!< xgregorm
    uint64_t m_xg_beacon_tx = 0, m_xg_beacon_bytes = 0; //!< xgregorm
    xgstats::Rsu m_xg_rsu; //!< xgregorm: merania
    bool m_xg_printed = false; //!< xgregorm
    uint32_t m_xg_hybrid = 0; //!< xgregorm: hybrid 802.11p + LTE: 1 CAM aj z kabla, 2 aj beacon s potvrdeniami
    Ptr<Socket> m_hyb_socket; //!< xgregorm: CAM preposlane zo siete LTE
    std::map<unsigned long, long> m_hyb_last_gdt; //!< xgregorm: posledna spracovana CAM auta
    uint64_t m_hyb_rx[3] = {0, 0, 0}, m_hyb_stale = 0; //!< xgregorm: prijate [802.11p, LTE, NR], zahodene stare
    bool m_xg_nr = false; //!< xgregorm: RSU ma NR-V2X sidelink
    Ptr<Socket> m_nr_socket, m_nr_beacon_socket; //!< xgregorm: CAM a beacony cez NR-V2X
    std::set<unsigned long> m_xg_heard_nr; //!< xgregorm: CAM pocute cez NR od posledneho beaconu
    double m_xg_last_cam_nr = -10; //!< xgregorm
    uint64_t m_nr_beacon_tx = 0; //!< xgregorm
    std::map <unsigned long, std::string> m_veh_position;
    std::map <std::string, unsigned long> m_veh_number;
    std::map <std::string,std::pair<double,double>> m_load;
    std::map <std::string,unsigned long> m_phase_map;
    std::map <std::string,EventId> m_timeout_map;
    std::mutex m_load_Mutex;

    Ptr<MetricSupervisor> m_metric_supervisor = nullptr;

    uint64_t m_stationId_baseline = 1000000;
  };

} // namespace ns3

#endif /* trafficManagerServer80211p_H */

