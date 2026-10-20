#include "ns3/random-variable-stream.h" // xgregorm
#include "trafficManagerServer80211p.h"

#include "ns3/CAM.h"
#include "ns3/DENM.h"
#include "ns3/socket.h"
#include "ns3/btpdatarequest.h"
#include "ns3/network-module.h"
#include "ns3/gn-utils.h"
#include "ns3/udp-socket-factory.h" // xgregorm
#include "ns3/inet-socket-address.h" // xgregorm
#include "ns3/xgregorm-hybrid-socket.h" // xgregorm
#include <sstream>

#define YELLOW_DURATION 4
#define NS_GREEN_PHASE 0
#define NS_YELLOW_PHASE 1
#define EW_GREEN_PHASE 2
#define EW_YELLOW_PHASE 3
#define PERCENTAGE 100
#define COORD_PORT 7000 // xgregorm
#define XG_ETHERTYPE 0x88B5 // xgregorm: IEEE "Local Experimental Ethertype 1" pre RSU beacon

#define NORTH 0
#define EAST 1
#define SOUTH 2
#define WEST 3
namespace ns3
{
  NS_LOG_COMPONENT_DEFINE("trafficManagerServer80211p");

  NS_OBJECT_ENSURE_REGISTERED(trafficManagerServer80211p);

  TypeId
  trafficManagerServer80211p::GetTypeId (void)
  {
    static TypeId tid =
        TypeId ("ns3::trafficManagerServer80211p")
        .SetParent<Application> ()
        .SetGroupName ("Applications")
        .AddConstructor<trafficManagerServer80211p> ()
        .AddAttribute ("RealTime",
           "To compute properly timestamps",
           BooleanValue(false),
           MakeBooleanAccessor (&trafficManagerServer80211p::m_real_time),
           MakeBooleanChecker ())
        .AddAttribute ("PhaseTime",
           "PhaseTime",
           UintegerValue (0),
           MakeUintegerAccessor (&trafficManagerServer80211p::m_phase_time),
           MakeUintegerChecker<uint16_t> ())
        .AddAttribute ("Threshold",
           "Threshold",
           DoubleValue (0),
           MakeDoubleAccessor (&trafficManagerServer80211p::m_threshold),
           MakeDoubleChecker<double> ())
        .AddAttribute ("Client",
           "TraCI client for SUMO",
           PointerValue (0),
           MakePointerAccessor (&trafficManagerServer80211p::m_client),
           MakePointerChecker<TraciClient> ())
        .AddAttribute ("MetricSupervisor",
            "Metric Supervisor to compute metrics according to 3GPP TR36.885 V14.0.0 page 70",
            PointerValue (0),
            MakePointerAccessor (&trafficManagerServer80211p::m_metric_supervisor),
            MakePointerChecker<MetricSupervisor> ())
        .AddAttribute ("SendCAM",
            "To enable/disable the transmission of CAM messages",
            BooleanValue(true),
            MakeBooleanAccessor (&trafficManagerServer80211p::m_send_cam),
            MakeBooleanChecker ())
        // xgregorm: lokalne riadenie krizovatiek
        .AddAttribute ("TlsRadius",
            "Control only the traffic lights closer than this distance [m] to the RSU (default: its own intersection); 0 = control all traffic lights (authors' original behaviour)",
            DoubleValue (50),
            MakeDoubleAccessor (&trafficManagerServer80211p::m_tls_radius),
            MakeDoubleChecker<double> (0))
        // xgregorm: koordinacia krizovatiek po kabli
        .AddAttribute ("Coordination",
            "Share vehicle positions with the other RSUs over the wired backhaul (needs an IPv4 interface)",
            BooleanValue (false),
            MakeBooleanAccessor (&trafficManagerServer80211p::m_coord),
            MakeBooleanChecker ())
        .AddAttribute ("BackhaulBroadcast",
            "Broadcast address of the wired network between RSUs",
            Ipv4AddressValue ("10.10.0.255"),
            MakeIpv4AddressAccessor (&trafficManagerServer80211p::m_backhaul_bcast),
            MakeIpv4AddressChecker ())
        .AddAttribute ("CoordLookahead",
            "A vehicle announced by a neighbour on one of my approaches is counted only this many seconds "
            "before its estimated arrival (lane length / speed limit); 0 = count it immediately",
            DoubleValue (0),
            MakeDoubleAccessor (&trafficManagerServer80211p::m_coord_lookahead),
            MakeDoubleChecker<double> (0))
        // xgregorm: hybrid 802.11p + LTE + NR-V2X
        .AddAttribute ("XgBeaconMs",
            "Adaptive hybrid: period of the RSU beacon listing the CAMs heard over each direct technology [ms]",
            UintegerValue (200),
            MakeUintegerAccessor (&trafficManagerServer80211p::m_xg_beacon_ms),
            MakeUintegerChecker<uint32_t> (1))
        .AddAttribute ("XgHybrid",
            "Hybrid 802.11p + LTE: 1 = also receive CAMs relayed from the cellular network over the wired backhaul "
            "(UDP port 9); 2 = in addition send beacons with the list of CAMs heard over 802.11p (link-quality "
            "feedback for adaptive vehicles)",
            UintegerValue (0),
            MakeUintegerAccessor (&trafficManagerServer80211p::m_xg_hybrid),
            MakeUintegerChecker<uint32_t> (0, 2))
        .AddAttribute ("XgNr",
            "Hybrid: the RSU has an NR-V2X sidelink device, receives CAMs over it (UDP 225.0.0.0:19) and, with "
            "XgHybrid = 2, sends its beacon also over NR (port 20)",
            BooleanValue (false),
            MakeBooleanAccessor (&trafficManagerServer80211p::m_xg_nr),
            MakeBooleanChecker ())
        .AddAttribute ("XgSecurity",
            "Sign every GeoNet packet of the RSU (ETSI TS 103 097, authors' GeoNet security)",
            BooleanValue (false),
            MakeBooleanAccessor (&trafficManagerServer80211p::m_xg_security),
            MakeBooleanChecker ());
        return tid;
  }

  trafficManagerServer80211p::trafficManagerServer80211p ()
  {
    NS_LOG_FUNCTION(this);
    m_client = nullptr;
    m_cam_received = 0;
    m_threshold = 70;
    m_phase_time = 60;
    m_tls_lanes_number =0;
    m_tls_radius = 50;
    m_coord = false;
    m_coord_lookahead = 0;
    m_coord_tx = 0;
    m_coord_rx = 0;
    m_xg_beacon_ms = 200;
  }

  trafficManagerServer80211p::~trafficManagerServer80211p ()
  {
    NS_LOG_FUNCTION(this);
  }

  void
  trafficManagerServer80211p::DoDispose (void)
  {
    NS_LOG_FUNCTION(this);
    Application::DoDispose ();
  }

  void
  trafficManagerServer80211p::StartApplication (void)
  {
    NS_LOG_FUNCTION(this);

    /*
     * In this example, the speed control is actuated in a small area at the center of the map (a circular area with 90m of radius).
     * In the inner area the maximum allowed speed is 6.94 m/s (25 km/h) while outside is 20.83 m/s (75 km/h).
     * The application will first of all determine this area, and then it will GeoBroadcast DENMs in that area, by specifying
     * in the RoadWorks container, the speed limit to be applied.
     * The DENM dissemination starts only when the RSU receives CAMs from vehicles. The DENMs generation is stopped
     * if no CAM is received by the RSU for more than 5 seconds.
    */

    m_id = m_client->GetStationId (this -> GetNode ());

    /* TX socket for DENMs and RX socket for CAMs (one socket only is necessary) */
    TypeId tid = TypeId::LookupByName ("ns3::PacketSocketFactory");

    m_socket = Socket::CreateSocket (GetNode (), tid);

    /* Bind the socket to local address */
    PacketSocketAddress local_denm;
    local_denm.SetSingleDevice (GetNode ()->GetDevice (0)->GetIfIndex ());
    local_denm.SetPhysicalAddress (GetNode ()->GetDevice (0)->GetAddress ());
    local_denm.SetProtocol (GN_ETHERTYPE);
    if (m_socket->Bind (local_denm) == -1)
    {
      NS_FATAL_ERROR ("Failed to bind server socket");
    }

    /* Set socket to broacdcast */
    PacketSocketAddress remote;
    remote.SetSingleDevice (GetNode ()->GetDevice (0)->GetIfIndex ());
    remote.SetPhysicalAddress (GetNode ()->GetDevice (0)->GetBroadcast ());
    remote.SetProtocol (GN_ETHERTYPE);
    m_socket->Connect(remote);

    /* Create new BTP and GeoNet objects and set them in DENBasicService and CABasicService */
    m_btp = CreateObject <btp>();
    m_geoNet = CreateObject <GeoNet>();

    if(m_metric_supervisor!=nullptr)
    {
      m_geoNet->setMetricSupervisor(m_metric_supervisor);
    }
    // xgregorm: podpis sprav ETSI TS 103 097 (implementacia autorov v GeoNet)
    if (m_xg_security)
      m_geoNet->setSecurity (true);

    m_btp->setGeoNet(m_geoNet);
    m_denService.setBTP(m_btp);
    m_caService.setBTP(m_btp);

    /* Set sockets, callback and station properties in DENBasicService */
    m_denService.setSocketTx (m_socket);
    m_denService.setSocketRx (m_socket);
    m_denService.addDENRxCallback (std::bind(&trafficManagerServer80211p::receiveDENM,this,std::placeholders::_1,std::placeholders::_2));

    size_t start = m_id.find("_") + 1;
    size_t end = m_id.find_first_not_of("0123456789", start); // find the end of the id
    std::string id_str = m_id.substr(start, end - start);
    uint64_t id = std::stoull(id_str);
    m_denService.setStationProperties (m_stationId_baseline + id, StationType_roadSideUnit);

    /* Set callback and station properties in CABasicService (which will only be used to receive CAMs) */
    m_caService.setStationProperties (m_stationId_baseline + id, StationType_roadSideUnit);
    m_caService.setSocketTx (m_socket);
    // xgregorm: hybrid - CAM samotnej RSU (802.11p) cez prepinac, aby pri vypadku 802.11p (xghyb::up) nevysielala
    if (m_xg_hybrid)
      m_caService.setSocketTx (Create<XgHybridSocket> (m_socket, nullptr, nullptr, [] () { return (int) xghyb::WIFI; }));
    m_caService.setSocketRx (m_socket);
    m_caService.addCARxCallback (std::bind(&trafficManagerServer80211p::receiveCAM,this,std::placeholders::_1,std::placeholders::_2));

    /*
     * Set the RSU position in the CA and DEN basic service (mandatory for any RSU object)
     * As the position must be specified in (lat, lon), we must take it from the mobility model
     * and then convert it to Latitude and Longitude
     * As SUMO is used here, we can rely on the TraCIAPI for this conversion
     */
    libsumo::TraCIPosition rsuPosXY = m_client->TraCIAPI::poi.getPosition (m_id);
    libsumo::TraCIPosition rsuPosLonLat = m_client->TraCIAPI::simulation.convertXYtoLonLat (rsuPosXY.x,rsuPosXY.y);

    /* Compute GeoArea for DENMs */
    GeoArea_t geoArea;
    // Longitude and Latitude in [0.1 microdegree]
    geoArea.posLong = rsuPosLonLat.x*DOT_ONE_MICRO;
    geoArea.posLat = rsuPosLonLat.y*DOT_ONE_MICRO;
    // Radius [m] of the circle that covers the whole square area of the map in (x,y)
    geoArea.distA = 50;
    // DistB [m] and angle [deg] equal to zero because we are defining a circular area as specified in ETSI EN 302 636-4-1 [9.8.5.2]
    geoArea.distB = 0;
    geoArea.angle = 0;
    geoArea.shape = CIRCULAR;

    m_denService.setGeoArea (geoArea);

    m_denService.setFixedPositionRSU (rsuPosLonLat.y,rsuPosLonLat.x);
    m_caService.setFixedPositionRSU (rsuPosLonLat.y,rsuPosLonLat.x);

    VDP* traci_vdp = new VDPTraCI(m_client, m_id, true);

    m_caService.setVDP(traci_vdp);

    m_denService.setVDP(traci_vdp);

    if(m_send_cam)
      {
        // xgregorm: srand(Now) dal autam vzniknutym v rovnakom kroku rovnaky posun -> CAM sa vysielali
        // naraz a kolidovali; oprava rovnaka ako autori urobili v emergencyVehicleAlert.cc
        Ptr<UniformRandomVariable> desync_rvar = CreateObject<UniformRandomVariable> ();
        desync_rvar->SetAttribute ("Min", DoubleValue (0.0));
        desync_rvar->SetAttribute ("Max", DoubleValue (1.0));
        double desync = desync_rvar->GetValue ();
        m_caService.startCamDissemination(desync);
      }


    std::vector<std::string> tls_id_vec = m_client->TraCIAPI::trafficlights.getIDList ();
    for (std::string tlsid : tls_id_vec)
      {
        // xgregorm: with TlsRadius > 0 every RSU controls only its own intersection(s),
        // otherwise all RSU servers would fight over the same traffic lights
        if (m_tls_radius > 0 && tlsDistance (tlsid, rsuPosXY) > m_tls_radius)
          continue;
        NS_LOG_INFO ("RSU " << m_id << " controls traffic light " << tlsid);
        for (std::string lanes : m_client->TraCIAPI::trafficlights.getControlledLanes (tlsid))
        {
            if(std::find(m_tls_lane_map[tlsid].begin (),m_tls_lane_map[tlsid].end (),lanes.substr(0,lanes.find_last_of("_")))==m_tls_lane_map[tlsid].end ())
              {
                m_tls_lane_map[tlsid].push_back(lanes.substr(0,lanes.find_last_of("_")));
                m_tls_lanes_number++;
              }
        }
        m_timeout_map[tlsid] = Simulator::Schedule(Seconds(m_phase_time),&trafficManagerServer80211p::phaseTimeout,this,tlsid);
      }

    // xgregorm: merania (presnost informacie RSU, uplink) - len citanie zo SUMO
    m_xg_rsu.id = m_id;
    m_xg_rsu.x = rsuPosXY.x;
    m_xg_rsu.y = rsuPosXY.y;
    if (!m_tls_lane_map.empty ())
      {
        m_xg_rsu.tls = m_tls_lane_map.begin ()->first;
        for (auto &e : m_tls_lane_map.begin ()->second)
          m_xg_rsu.approaches.insert (e);
      }
    m_xg_rsu.believedEdge = &m_veh_position;
    xgstats::registerRsu (&m_xg_rsu, m_client);

    // xgregorm: adaptivny hybrid - beacon RSU (zoznam CAM pocutych kazdou priamou technologiou) = spatna
    // vazba pre auta; cez 802.11p vlastny ramec (nie GeoNet, ethertype 0x88B5), simulovany v ns-3 rovnako ako
    // CAM (kanal, kolizie); MetricSupervisor ho do PRR nezapocita, pocty vypisujeme
    if (m_xg_hybrid == 2)
      {
        m_xg_socket = Socket::CreateSocket (GetNode (), TypeId::LookupByName ("ns3::PacketSocketFactory"));
        PacketSocketAddress xlocal;
        xlocal.SetSingleDevice (GetNode ()->GetDevice (0)->GetIfIndex ());
        xlocal.SetPhysicalAddress (GetNode ()->GetDevice (0)->GetAddress ());
        xlocal.SetProtocol (XG_ETHERTYPE);
        if (m_xg_socket->Bind (xlocal) == -1)
          NS_FATAL_ERROR ("Failed to bind the xgregorm beacon socket");
        PacketSocketAddress xremote;
        xremote.SetSingleDevice (GetNode ()->GetDevice (0)->GetIfIndex ());
        xremote.SetPhysicalAddress (GetNode ()->GetDevice (0)->GetBroadcast ());
        xremote.SetProtocol (XG_ETHERTYPE);
        m_xg_socket->Connect (xremote);
        Ptr<UniformRandomVariable> off = CreateObject<UniformRandomVariable> ();
        m_xg_beacon_ev = Simulator::Schedule (MilliSeconds (off->GetInteger (0, m_xg_beacon_ms)),
                                              &trafficManagerServer80211p::xgSendBeacon, this);
      }

    // xgregorm: hybrid - CAM, ktore auta poslali cez LTE, preposiela server v sieti po kabli; idu do tej
    // istej GeoNet/CA sluzby ako CAM z 802.11p (receiveCAM ich rozlisi podla typu adresy)
    if (m_xg_hybrid)
      {
        m_hyb_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
        if (m_hyb_socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), 9)) == -1)
          NS_FATAL_ERROR ("Hybrid mode needs an IPv4 interface on the RSU (wired backhaul)");
        m_hyb_socket->SetRecvCallback (MakeCallback (&GeoNet::receiveGN, m_geoNet));
      }
    // xgregorm: hybrid - NR-V2X sidelink (RSU ako UE v sidelinku, ako C-V2X RSU v realite)
    if (m_xg_nr)
      {
        Ptr<NetDevice> nrDev = xghyb::findDevice (GetNode (), "ns3::NrUeNetDevice");
        if (!nrDev)
          NS_FATAL_ERROR ("XgNr needs an NR sidelink device on the RSU");
        m_nr_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
        if (m_nr_socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), 19)) == -1)
          NS_FATAL_ERROR ("Failed to bind the NR-V2X socket on the RSU");
        m_nr_socket->BindToNetDevice (nrDev);
        m_nr_socket->SetRecvCallback (MakeCallback (&GeoNet::receiveGN, m_geoNet));
        if (m_xg_hybrid == 2)
          {
            m_nr_beacon_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
            m_nr_beacon_socket->Bind ();
            m_nr_beacon_socket->BindToNetDevice (nrDev);
            m_nr_beacon_socket->Connect (InetSocketAddress (Ipv4Address ("225.0.0.0"), 20));
          }
      }

    // xgregorm: UDP socket na kablovej sieti medzi RSU
    if (m_coord)
      {
        m_coord_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
        if (m_coord_socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), COORD_PORT)) == -1)
          NS_FATAL_ERROR ("Coordination needs an IPv4 interface on the RSU (wired backhaul)");
        m_coord_socket->SetAllowBroadcast (true);
        m_coord_socket->SetRecvCallback (MakeCallback (&trafficManagerServer80211p::receiveCoord, this));
      }
  }

  double
  trafficManagerServer80211p::tlsDistance (std::string tlsid, libsumo::TraCIPosition rsuPos)
  {
    // xgregorm: netconvert names a traffic light after its junction; if not, use the end
    // of the first controlled lane (the stop line), which lies at the junction as well
    libsumo::TraCIPosition p;
    try
      {
        p = m_client->TraCIAPI::junction.getPosition (tlsid);
      }
    catch (const libsumo::TraCIException &)
      {
        std::vector<std::string> lanes = m_client->TraCIAPI::trafficlights.getControlledLanes (tlsid);
        if (lanes.empty ())
          return std::numeric_limits<double>::max ();
        p = m_client->TraCIAPI::lane.getShape (lanes[0]).back ();
      }
    return std::hypot (p.x - rsuPos.x, p.y - rsuPos.y);
  }

  void
  trafficManagerServer80211p::StopApplication ()
  {
    NS_LOG_FUNCTION(this);
    m_denService.cleanup();
    Simulator::Cancel (m_xg_beacon_ev);
    if (!m_xg_printed)
      {
        // xgregorm: merania; vzorkovanie konci s prvou zastavenou RSU (potom uz SUMO nemusi bezat)
        m_xg_printed = true;
        xgstats::running () = false;
        const xgstats::Rsu &r = m_xg_rsu;
        std::cout << "XGREGORM-AW," << m_id << ",SAMPLES:" << r.samples << ",UNKNOWN:" << r.unknown
                  << ",EDGE_WRONG:" << r.edgeWrong << ",ERR_SUM:" << r.errSum << ",GT5:" << r.gt5
                  << ",GT10:" << r.gt10 << ",UL_EXP:" << r.ulExp[0] << ",UL_RX:" << r.ulRx[0]
                  << ",ULG_EXP:" << r.ulExp[1] << ",ULG_RX:" << r.ulRx[1] << ",GREENS:" << r.greenStarts.size ()
                  << ",BEACON_TX:" << m_xg_beacon_tx << ",BEACON_BYTES:" << m_xg_beacon_bytes
                  << ",SAMPLES_F:" << r.samplesF << ",UNKNOWN_F:" << r.unknownF << ",ERR_SUM_F:" << r.errSumF
                  << ",GT10_F:" << r.gt10F << ",AGE_SUM:" << r.ageSum << ",AGE_SUM_F:" << r.ageSumF
                  << ",LAT_W_SUM:" << r.latSum[0] << ",LAT_W_N:" << r.latN[0] << ",LAT_L_SUM:" << r.latSum[1]
                  << ",LAT_L_N:" << r.latN[1] << ",LAT_N_SUM:" << r.latSum[2] << ",LAT_N_N:" << r.latN[2]
                  << ",RX_W:" << m_hyb_rx[0] << ",RX_L:" << m_hyb_rx[1] << ",RX_N:" << m_hyb_rx[2]
                  << ",STALE:" << m_hyb_stale << ",BEACON_NR_TX:" << m_nr_beacon_tx
                  << ",BEL_ON:" << r.believedOn << ",PHANTOM:" << r.phantom << std::endl;
        static bool v2v_printed = false;
        if (!v2v_printed)
          std::cout << "XGREGORM-V2V,EXP:" << xgstats::v2vExp () << ",RX:" << xgstats::v2vRx () << std::endl;
        v2v_printed = true;
      }
    if (m_coord)
      {
        std::cout << "XGREGORM-COORD," << m_id << ",TX:" << m_coord_tx << ",RX:" << m_coord_rx << std::endl;
        m_coord = false; // vypis len raz (StopApplication sa moze volat dvakrat)
      }
  }

  void
  trafficManagerServer80211p::StopApplicationNow ()
  {
    NS_LOG_FUNCTION(this);
    StopApplication ();
  }

  void
  trafficManagerServer80211p::receiveCAM (asn1cpp::Seq<CAM> cam, Address address)
  {
    (void) address;

    m_cam_received++;


    double lat = asn1cpp::getField(cam->cam.camParameters.basicContainer.referencePosition.latitude,double)/DOT_ONE_MICRO;
    double lon = asn1cpp::getField(cam->cam.camParameters.basicContainer.referencePosition.longitude,double)/DOT_ONE_MICRO;

    /* Based on the Lon,Lat exctracted by the map, get the edgeId using TraCI */
    libsumo::TraCIRoadPosition roadMap = m_client->TraCIAPI::simulation.convertLonLattoRoadmap (lon,lat);

    unsigned long stationId = asn1cpp::getField (cam->header.stationId, unsigned long);

    // xgregorm: merania + zoznam pocutych CAM pre beacon (hybrid)
    long gdt = asn1cpp::getField (cam->cam.generationDeltaTime, long);
    // cesta: 802.11p = PacketSocket; UDP od servera z kabla (10.10.0.0/24) = LTE; ine UDP = NR-V2X sidelink
    int path = 0;
    if (InetSocketAddress::IsMatchingType (address))
      path = Ipv4Mask ("255.255.255.0").IsMatch (InetSocketAddress::ConvertFrom (address).GetIpv4 (),
                                                  Ipv4Address ("10.10.0.0")) ? 1 : 2;
    xgstats::onCamRx (&m_xg_rsu, stationId, gdt, path);
    if (m_xg_hybrid)
      {
        // ta ista CAM moze prist viac cestami a LTE kopia neskor nez novsia CAM cez priamu technologiu:
        // spracujeme len CAM novsiu nez posledna (generationDeltaTime, modulo 65536). Porovnanie modulo plati len
        // pre CAM blizko seba v case: auto, ktore RSU nepocula dlhsie nez 10 s (napr. objazd okruhu 33-65 s),
        // ma CAM vzdy novu (inak by ju porovnanie modulo 65.536 s mohlo vyhodnotit ako staru a RSU by auto ignorovala)
        m_hyb_rx[path]++;
        double now = Simulator::Now ().GetSeconds ();
        auto last = m_hyb_last_gdt.find (stationId);
        if (last != m_hyb_last_gdt.end () && now - last->second.second < 10.0)
          {
            long d = (gdt - last->second.first + 65536) % 65536;
            if (d == 0 || d >= 32768)
              {
                m_hyb_stale++;
                return;
              }
          }
        m_hyb_last_gdt[stationId] = {gdt, now};
      }
    // beacon (aj jeho potvrdenia) sa tyka len priameho spojenia, kazda technologia ma vlastny zoznam
    if (path == 0)
      {
        m_xg_heard.insert (stationId);
        m_xg_last_cam = Simulator::Now ().GetSeconds ();
      }
    else if (path == 2)
      {
        m_xg_heard_nr.insert (stationId);
        m_xg_last_cam_nr = Simulator::Now ().GetSeconds ();
      }

    // xgregorm: koordinacia krizovatiek - zmenu cesty auta oznamime ostatnym RSU po kabli
    if (m_coord && !roadMap.edgeID.empty () && roadMap.edgeID[0] != ':')
      {
        m_veh_last_update[stationId] = Simulator::Now ();
        if (m_last_edge_seen[stationId] != roadMap.edgeID)
          {
            m_last_edge_seen[stationId] = roadMap.edgeID;
            sendCoord (stationId, roadMap.edgeID);
          }
      }

    applyVehiclePosition (stationId, roadMap.edgeID);
  }

  void
  trafficManagerServer80211p::applyVehiclePosition (unsigned long stationId, std::string edgeID)
  {
    // xgregorm: povodna logika z receiveCAM (autori), len vyclenena, aby ju mohli volat
    // aj spravy od susednych RSU; spravanie pri vypnutej koordinacii je identicke
    bool isInside = false;
    std::map<std::string,unsigned long> current_load_map = {};

    for (auto tls_it = m_tls_lane_map.begin (); tls_it != m_tls_lane_map.end (); tls_it++)
    {
        if(std::find(tls_it->second.begin (),tls_it->second.end (),edgeID)!=tls_it->second.end ())
        {
          isInside=true;
        }
    }

    /* If is the first time the this veh sends a CAM, just check if it is inside and edge of interest(1-8) or outside(0), then return */
    if (m_veh_position.find (stationId) == m_veh_position.end ())
    {
        if(isInside)
        {
          m_veh_position[stationId] = edgeID;
          m_veh_number[edgeID]++;
        }
        else
        {
          // Empty string -> we are not interested in an edge which is outside the ones we are interested in
          m_veh_position[stationId] = "";
          m_veh_number[""]++;
        }
        return;
    }

    /* Otherwise, check if a transition happened */
    if (isInside)
    {
      /* The vehice is now in an edge of interest
       * If it was registered in other edge, update traffic lights state */
      if (m_veh_position[stationId] != edgeID)
      {
          m_veh_number[m_veh_position[stationId]]--;
          m_veh_number[edgeID]++;
          m_veh_position[stationId] = edgeID;
      }
    }
    else
    {
      /* The vehicle is now outside of any edge of interest
       * If it was registered inside one, then update traffic lights state */
      if (!m_veh_position[stationId].empty ())
      {
          m_veh_number[m_veh_position[stationId]]--;
          m_veh_number[""]++;
          m_veh_position[stationId] = "";
      }
    }

    /* Compute current traffic load on each intersection*/
    for (auto veh_number = m_veh_number.begin (); veh_number != m_veh_number.end (); veh_number++)
    {
        for(auto intersection = m_tls_lane_map.begin (); intersection != m_tls_lane_map.end (); intersection++)
          {
            auto intersection_name = std::find(intersection->second.begin (),intersection->second.end (),veh_number->first);
            if(intersection_name!=intersection->second.end ())
            {
                current_load_map[intersection->first] +=  veh_number->second;
            }
          }
    }

    /*If all lanes relative to the tls are populated*/
    if(m_veh_number.size ()> m_tls_lanes_number)
      {
        for( auto intersection = m_tls_lane_map.begin ();intersection != m_tls_lane_map.end (); intersection++)
          {
            /* In this example the intersections have North-South and East-West directions
             * This application will work with any direction as long as the tls lanes are introduced consecutively
             * in this case NORTH-EAST-SOUTH-WEST
             */
            // xgregorm: pri prazdnej krizovatke by vyslo 0/0 = NaN a phaseTimeout by z (uint16_t)NaN
            // naplanoval prepnutie o 0 s -> nekonecna slucka v tom istom case (chyba aj v povodnom kode);
            // ponechame posledny platny pomer
            if (current_load_map[intersection->first] == 0)
              continue;
            std::pair<double,double> ratio;
            /*Compute North-South direction load ratio*/
            ratio.first = (double)(m_veh_number[intersection->second[NORTH]]+m_veh_number[intersection->second[SOUTH]])/current_load_map[intersection->first];
            /*Compute East-West direction load ratio*/
            ratio.second =(double)(m_veh_number[intersection->second[EAST]]+m_veh_number[intersection->second[WEST]])/current_load_map[intersection->first];

            m_load_Mutex.lock ();
            m_load[intersection->first] = ratio; //In case this map is being read by the phaseTimeout function
            m_load_Mutex.unlock ();

            /* Store current phase of this intersection*/
            m_phase_map[intersection->first] = m_client->TraCIAPI::trafficlights.getPhase (intersection->first);

            if(((m_load[intersection->first].first > m_threshold/PERCENTAGE) && (m_phase_map[intersection->first] == EW_GREEN_PHASE)) ||
                    ((m_load[intersection->first].second > m_threshold/PERCENTAGE) && (m_phase_map[intersection->first] == NS_GREEN_PHASE)))
              {
                 /*If any direction ratio is higher than the threshold, and we are in the green phase of the opposite direction, change phase now */
                  Simulator::Cancel (m_timeout_map[intersection->first]);
                  m_timeout_map[intersection->first] = Simulator::Schedule(MilliSeconds (1),&trafficManagerServer80211p::phaseTimeout,this,intersection->first);
              }
          }
      }
  }

  void
  trafficManagerServer80211p::sendCoord (unsigned long stationId, std::string edgeID)
  {
    // xgregorm: sprava po kabli "stationId,edgeID,cas_ns" (UDP broadcast v sieti RSU)
    std::ostringstream msg;
    msg << stationId << "," << edgeID << "," << Simulator::Now ().GetNanoSeconds ();
    std::string txt = msg.str ();
    Ptr<Packet> p = Create<Packet> ((const uint8_t *) txt.c_str (), txt.size ());
    m_coord_socket->SendTo (p, 0, InetSocketAddress (m_backhaul_bcast, COORD_PORT));
    m_coord_tx++;
  }

  void
  trafficManagerServer80211p::receiveCoord (Ptr<Socket> socket)
  {
    // xgregorm: sused hlasi, ze auto preslo na inu cestu (napr. na prijazd k mojej krizovatke)
    Ptr<Packet> p;
    while ((p = socket->Recv ()))
      {
        std::vector<uint8_t> buf (p->GetSize ());
        p->CopyData (buf.data (), buf.size ());
        std::string txt (buf.begin (), buf.end ());
        size_t c1 = txt.find (','), c2 = txt.rfind (',');
        if (c1 == std::string::npos || c1 == c2)
          continue;
        unsigned long stationId = std::stoul (txt.substr (0, c1));
        std::string edgeID = txt.substr (c1 + 1, c2 - c1 - 1);
        Time t = NanoSeconds (std::stoll (txt.substr (c2 + 1)));
        m_coord_rx++;

        // starsia informacia, nez akou uz disponujem (vlastna CAM alebo iny sused), sa ignoruje
        if (m_veh_last_update.count (stationId) && t <= m_veh_last_update[stationId])
          continue;
        m_veh_last_update[stationId] = t;
        m_last_edge_seen[stationId] = edgeID;

        // xgregorm: auto na mojom prijazde zapocitame az kratko pred odhadovanym prichodom ku krizovatke,
        // inak by sa zelena davala smeru, kde auto este len ide, a cakajuce auta by stali
        bool mine = false;
        for (auto &tls : m_tls_lane_map)
          if (std::find (tls.second.begin (), tls.second.end (), edgeID) != tls.second.end ())
            mine = true;
        if (m_coord_lookahead > 0 && mine)
          {
            double len = m_client->TraCIAPI::lane.getLength (edgeID + "_0");
            double vmax = m_client->TraCIAPI::lane.getMaxSpeed (edgeID + "_0");
            double eta = vmax > 0 ? len / vmax : 0;
            Time delay = Seconds (std::max (0.0, eta - m_coord_lookahead));
            Simulator::Schedule (delay, &trafficManagerServer80211p::applyCoordDelayed, this, stationId, edgeID, t);
          }
        else
          applyVehiclePosition (stationId, edgeID);
      }
  }

  void
  trafficManagerServer80211p::applyCoordDelayed (unsigned long stationId, std::string edgeID, Time reported)
  {
    // xgregorm: medzicasom prisla novsia informacia (vlastna CAM alebo iny sused) -> ta plati
    if (m_veh_last_update[stationId] > reported)
      return;
    applyVehiclePosition (stationId, edgeID);
  }

  void
  trafficManagerServer80211p::xgSendBeacon ()
  {
    // xgregorm: adaptivny hybrid - "XG|semafor|zaciatok_okna_ms|id;id;..." = auta, ktorych CAM RSU touto
    // technologiou pocula od zaciatku okna; ide cez 802.11p aj NR-V2X, kazdy so svojim zoznamom.
    // Technologiou, ktorou RSU za posledne 2 s nic nepocula, sa beacon neposiela (nie je komu).
    double now = Simulator::Now ().GetSeconds ();
    m_xg_beacon_ev = Simulator::Schedule (MilliSeconds (m_xg_beacon_ms), &trafficManagerServer80211p::xgSendBeacon, this);
    auto build = [&] (const std::set<unsigned long> &heard) {
      std::ostringstream msg;
      msg << "XG|" << m_xg_rsu.tls << "|" << (long) (m_xg_win_start * 1000) << "|";
      bool first = true;
      for (unsigned long sid : heard)
        {
          msg << (first ? "" : ";") << sid;
          first = false;
        }
      return msg.str ();
    };
    if (now - m_xg_last_cam <= 2.0 && xghyb::up (xghyb::WIFI))
      {
        std::string txt = build (m_xg_heard);
        m_xg_socket->Send (Create<Packet> ((const uint8_t *) txt.c_str (), txt.size ()));
        m_xg_beacon_tx++;
        m_xg_beacon_bytes += txt.size ();
      }
    if (m_nr_beacon_socket && now - m_xg_last_cam_nr <= 2.0 && xghyb::up (xghyb::NR))
      {
        std::string txt = build (m_xg_heard_nr);
        m_nr_beacon_socket->Send (Create<Packet> ((const uint8_t *) txt.c_str (), txt.size ()));
        m_nr_beacon_tx++;
      }
    m_xg_heard.clear ();
    m_xg_heard_nr.clear ();
    m_xg_win_start = now;
  }

  void
  trafficManagerServer80211p::receiveDENM (denData denm, Address from)
  {
    /* This is just a sample dummy receiveDENM function. The user can customize it to parse the content of a DENM when it is received. */
    (void) denm; // Contains the data received from the DENM
    (void) from; // Contains the address from which the DENM has been received
    std::cout<<"Received a new DENM."<<std::endl;
  }

  long
  trafficManagerServer80211p::compute_timestampIts ()
  {
    /* To get millisec since  2004-01-01T00:00:00:000Z */
    auto time = std::chrono::system_clock::now(); // get the current time
    auto since_epoch = time.time_since_epoch(); // get the duration since epoch
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch); // convert it in millisecond since epoch

    long elapsed_since_2004 = millis.count() - TIME_SHIFT; // in TIME_SHIFT we saved the millisec from epoch to 2004-01-01
    return elapsed_since_2004;
  }

  void
  trafficManagerServer80211p::phaseTimeout (std::string intersection)
  {
    uint16_t phase;

    phase = m_client->TraCIAPI::trafficlights.getPhase (intersection);

    m_load_Mutex.lock ();
    if(phase == NS_GREEN_PHASE)
      {
        m_timeout_map[intersection] = Simulator::Schedule(Seconds((uint16_t)((m_phase_time * m_load[intersection].second) + YELLOW_DURATION)),&trafficManagerServer80211p::phaseTimeout,this,intersection);
        phase = NS_YELLOW_PHASE;
      }
    else
      {
        m_timeout_map[intersection] = Simulator::Schedule(Seconds((uint16_t)((m_phase_time * m_load[intersection].first) + YELLOW_DURATION)),&trafficManagerServer80211p::phaseTimeout,this,intersection);
        phase = EW_YELLOW_PHASE;
      }
    m_load_Mutex.unlock ();

    m_client->TraCIAPI::trafficlights.setPhase (intersection,phase);
  }


}
