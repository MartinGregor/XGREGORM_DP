#include "ns3/random-variable-stream.h" // xgregorm
#include "trafficManagerClient80211p.h"

#include "ns3/CAM.h"
#include "ns3/DENM.h"
#include "ns3/vdpTraci.h"
#include "ns3/socket.h"
#include "ns3/network-module.h"
#include "ns3/gn-utils.h" // Needed for GN_ETHERTYPE
#include "ns3/xgregorm-stats.h" // xgregorm
#include "ns3/xgregorm-hybrid-socket.h" // xgregorm
#include "ns3/udp-socket-factory.h" // xgregorm
#include "ns3/inet-socket-address.h" // xgregorm
#include <sstream> // xgregorm
#define XG_ETHERTYPE 0x88B5 // xgregorm: beacon RSU

namespace ns3
{
  NS_LOG_COMPONENT_DEFINE("trafficManagerClient80211p");

  NS_OBJECT_ENSURE_REGISTERED(trafficManagerClient80211p);

  TypeId
  trafficManagerClient80211p::GetTypeId (void)
  {
    static TypeId tid =
        TypeId ("ns3::trafficManagerClient80211p")
        .SetParent<Application> ()
        .SetGroupName ("Applications")
        .AddConstructor<trafficManagerClient80211p> ()
        .AddAttribute ("PrintSummary",
            "To print summary at the end of simulation",
            BooleanValue(false),
            MakeBooleanAccessor (&trafficManagerClient80211p::m_print_summary),
            MakeBooleanChecker ())
        .AddAttribute ("RealTime",
            "To compute properly timestamps",
            BooleanValue(false),
            MakeBooleanAccessor (&trafficManagerClient80211p::m_real_time),
            MakeBooleanChecker ())
        .AddAttribute ("ServerAddr",
            "Ip Addr of the server",
            Ipv4AddressValue("10.0.0.1"),
            MakeIpv4AddressAccessor (&trafficManagerClient80211p::m_server_addr),
            MakeIpv4AddressChecker ())
        .AddAttribute ("Client",
            "TraCI client for SUMO",
            PointerValue (0),
            MakePointerAccessor (&trafficManagerClient80211p::m_client),
            MakePointerChecker<TraciClient> ())
        .AddAttribute ("MetricSupervisor",
            "Metric Supervisor to compute metrics according to 3GPP TR36.885 V14.0.0 page 70",
            PointerValue (0),
            MakePointerAccessor (&trafficManagerClient80211p::m_metric_supervisor),
            MakePointerChecker<MetricSupervisor> ())
        .AddAttribute ("SendCAM",
            "To enable/disable the transmission of CAM messages",
            BooleanValue(true),
            MakeBooleanAccessor (&trafficManagerClient80211p::m_send_cam),
            MakeBooleanChecker ())
        // xgregorm: moj algoritmus CAM (podla relevancie)
        .AddAttribute ("XgRelax",
            "Relevance-based CAM rate: farther than XgRelevance before the next traffic light (along the route, "
            "from the map) send at most 1 CAM per second (ETSI T_GenCamMax), full ETSI rate only on the approach",
            BooleanValue (false),
            MakeBooleanAccessor (&trafficManagerClient80211p::m_xg_relax),
            MakeBooleanChecker ())
        .AddAttribute ("XgRelevance",
            "Relevance-based CAM rate: distance to the next traffic light with full-rate CAMs [m]",
            DoubleValue (250),
            MakeDoubleAccessor (&trafficManagerClient80211p::m_xg_relevance),
            MakeDoubleChecker<double> (0))
        // xgregorm: hybrid 802.11p + LTE + NR-V2X
        .AddAttribute ("XgHybrid",
            "CAM path policy: 0 = off (802.11p only, original), 1 = every CAM on all technologies in XgPaths, "
            "2 = adaptive (one direct technology for the neighbours, a backup path added only when the controller "
            "of the next traffic light needs the vehicle and does not confirm hearing it)",
            UintegerValue (0),
            MakeUintegerAccessor (&trafficManagerClient80211p::m_xg_hybrid),
            MakeUintegerChecker<uint32_t> (0, 2))
        .AddAttribute ("XgPaths",
            "Technologies available in the vehicle (bit mask): 1 = 802.11p, 2 = LTE (to ServerAddr), 4 = NR-V2X sidelink",
            UintegerValue (1),
            MakeUintegerAccessor (&trafficManagerClient80211p::m_hyb_mask),
            MakeUintegerChecker<uint32_t> (1, 7))
        .AddAttribute ("XgHybRange",
            "Adaptive hybrid: distance to the next traffic light (along the route) where its controller needs the vehicle [m]",
            DoubleValue (250),
            MakeDoubleAccessor (&trafficManagerClient80211p::m_hyb_range),
            MakeDoubleChecker<double> (0))
        .AddAttribute ("XgHybQmin",
            "Adaptive hybrid: lowest share of own direct CAMs confirmed by the RSU (moving average) to rely on that technology only",
            DoubleValue (0.9),
            MakeDoubleAccessor (&trafficManagerClient80211p::m_hyb_qmin),
            MakeDoubleChecker<double> (0, 1))
        .AddAttribute ("XgHybDwell",
            "Adaptive hybrid: the direct link must be good this long before the backup path is switched off again [s] (hysteresis)",
            DoubleValue (1.0),
            MakeDoubleAccessor (&trafficManagerClient80211p::m_hyb_dwell),
            MakeDoubleChecker<double> (0))
        .AddAttribute ("XgColor",
            "Hybrid: colour the vehicle in SUMO by the technology its CAMs currently use (visualisation only)",
            BooleanValue (false),
            MakeBooleanAccessor (&trafficManagerClient80211p::m_xg_color),
            MakeBooleanChecker ())
        .AddAttribute ("XgSecurity",
            "Sign every GeoNet packet (ETSI TS 103 097, authors' GeoNet security: ECDSA signature, certificate once "
            "per second, digest otherwise) - realistic message size",
            BooleanValue (false),
            MakeBooleanAccessor (&trafficManagerClient80211p::m_xg_security),
            MakeBooleanChecker ())
        .AddAttribute ("XgLoadRat",
            "Adaptive hybrid: choose the main direct technology by load - 802.11p, under congestion part of the vehicles "
            "move to NR-V2X, LTE backup only when the cell is not overloaded (needs the load measurement of the scenario)",
            BooleanValue (false),
            MakeBooleanAccessor (&trafficManagerClient80211p::m_xg_load),
            MakeBooleanChecker ())
        .AddAttribute ("XgCbrHigh",
            "Load rule: CBR from which a direct channel counts as congested (ETSI TS 102 687 DCC: 0.3 relaxed -> active)",
            DoubleValue (0.3),
            MakeDoubleAccessor (&trafficManagerClient80211p::m_cbr_high),
            MakeDoubleChecker<double> (0, 1))
        .AddAttribute ("XgCbrLow",
            "Load rule: CBR of 802.11p below which vehicles return from NR-V2X (hysteresis)",
            DoubleValue (0.2),
            MakeDoubleAccessor (&trafficManagerClient80211p::m_cbr_low),
            MakeDoubleChecker<double> (0, 1))
        .AddAttribute ("XgLteHigh",
            "Load rule: uplink PRB usage of the LTE cell above which LTE is not added as a backup",
            DoubleValue (0.6),
            MakeDoubleAccessor (&trafficManagerClient80211p::m_lte_high),
            MakeDoubleChecker<double> (0, 1))
        .AddAttribute ("XgLoadPeriod",
            "Load rule: how often a vehicle re-evaluates its main technology [s] (also the minimum dwell time)",
            DoubleValue (1.0),
            MakeDoubleAccessor (&trafficManagerClient80211p::m_load_period),
            MakeDoubleChecker<double> (0.1))
        .AddAttribute ("XgQosRat",
            "Adaptive hybrid: choose the main direct technology by the measured reliability (share of the neighbours' "
            "CAMs within 100 m delivered within XgLatBudget, per technology; losses from a sequence number in the GeoNet "
            "basic header); moves only to a technology that is not congested (needs the load measurement)",
            BooleanValue (false),
            MakeBooleanAccessor (&trafficManagerClient80211p::m_xg_qos),
            MakeBooleanChecker ())
        .AddAttribute ("XgQosMargin",
            "Quality rule: the other technology must be this much more reliable to move to it (hysteresis)",
            DoubleValue (0.05),
            MakeDoubleAccessor (&trafficManagerClient80211p::m_qos_margin),
            MakeDoubleChecker<double> (0, 1))
        .AddAttribute ("XgLatBudget",
            "Quality rule: latency budget of a CAM [s]; a later CAM counts as lost (3GPP reliability)",
            DoubleValue (0.1),
            MakeDoubleAccessor (&trafficManagerClient80211p::m_lat_budget),
            MakeDoubleChecker<double> (0.001))
        .AddAttribute ("XgPredRat",
            "Quality rule with prediction: the reliability and load of each direct technology over the next "
            "XgPredHorizon s come from a shared quality map (measurements of all vehicles, per 20 m cell) at the "
            "points of the vehicle's route, averaged with its own current measurement (needs XgQosRat)",
            BooleanValue (false),
            MakeBooleanAccessor (&trafficManagerClient80211p::m_xg_pred),
            MakeBooleanChecker ())
        .AddAttribute ("XgPredHorizon",
            "Prediction: horizon [s]; one route point per second",
            DoubleValue (3.0),
            MakeDoubleAccessor (&trafficManagerClient80211p::m_pred_h),
            MakeDoubleChecker<double> (0, 10))
        .AddAttribute ("XgClean",
            "Clean policy: every CAM on one direct technology, no copies and no RSU beacon; LTE only on an approach farther than XgCellDist from the RSU",
            BooleanValue (false),
            MakeBooleanAccessor (&trafficManagerClient80211p::m_xg_clean),
            MakeBooleanChecker ())
        .AddAttribute ("XgInitMain",
            "Clean policy: initial main technology: 0 = 802.11p, 1 = NR-V2X, 2 = random per vehicle (50/50)",
            UintegerValue (0),
            MakeUintegerAccessor (&trafficManagerClient80211p::m_init_main),
            MakeUintegerChecker<uint32_t> (0, 2))
        .AddAttribute ("XgProbeHz",
            "Clean policy: CAMs per second also sent on the other direct technology to measure it (0 = none)",
            DoubleValue (0),
            MakeDoubleAccessor (&trafficManagerClient80211p::m_probe_hz),
            MakeDoubleChecker<double> (0, 10))
        .AddAttribute ("XgCellDist",
            "Clean policy: distance from the RSU on an approach beyond which the CAM goes also over LTE [m]",
            DoubleValue (150),
            MakeDoubleAccessor (&trafficManagerClient80211p::m_cell_dist),
            MakeDoubleChecker<double> (0))
        .AddAttribute ("XgNbEquip", "Clean policy: choose the technology by the equipment of the neighbours (both only if some neighbours hear only 802.11p and others only NR)",
            BooleanValue (false), MakeBooleanAccessor (&trafficManagerClient80211p::m_nb_equip), MakeBooleanChecker ())
        .AddAttribute ("XgRsuRoute", "Clean policy: on an approach use the technology of the next RSU (known from the map)",
            BooleanValue (false), MakeBooleanAccessor (&trafficManagerClient80211p::m_rsu_route), MakeBooleanChecker ())
        .AddAttribute ("XgDupNear", "Clean policy: copy of the CAM on the other technology closer than this to the next traffic light [m]; 0 = off",
            DoubleValue (0), MakeDoubleAccessor (&trafficManagerClient80211p::m_dup_near), MakeDoubleChecker<double> (0))
        .AddAttribute ("XgOracle", "Reference only, not a realizable policy: the vehicle knows the --zones exactly and "
            "chooses the technology with the smaller loss at its position and at the route points of the next XgPredHorizon s",
            BooleanValue (false), MakeBooleanAccessor (&trafficManagerClient80211p::m_oracle), MakeBooleanChecker ())
        .AddAttribute ("XgGeoRat", "Clean policy: closer than this to the next traffic light (blind corner) the CAM goes over NR [m]; 0 = off",
            DoubleValue (0), MakeDoubleAccessor (&trafficManagerClient80211p::m_geo_rat), MakeDoubleChecker<double> (0));
        return tid;
  }

  trafficManagerClient80211p::trafficManagerClient80211p ()
  {
    NS_LOG_FUNCTION(this);

    m_client = nullptr;
    m_print_summary = true;
    m_already_print = false;
    m_cam_sent = 0;
    m_denm_received = 0;
  }

  trafficManagerClient80211p::~trafficManagerClient80211p ()
  {
    NS_LOG_FUNCTION(this);
  }

  void
  trafficManagerClient80211p::DoDispose (void)
  {
    NS_LOG_FUNCTION(this);
    Application::DoDispose ();
  }

  void
  trafficManagerClient80211p::StartApplication (void)
  {
    NS_LOG_FUNCTION(this);

    /*
     * This application is intended to be installed over a vehicular OBU node,
     * and it is set to generate and broadcast CAM messages on top of BTP and GeoNet.
     */

    m_id = m_client->GetVehicleId (this->GetNode ());

    /* Create the socket for TX and RX */
    TypeId tid = TypeId::LookupByName ("ns3::PacketSocketFactory");

    /* Socket used to send CAMs and receive DENMs */
    m_socket = Socket::CreateSocket (GetNode (), tid);

    /* Bind the socket to local address */
    PacketSocketAddress local;
    local.SetSingleDevice (GetNode ()->GetDevice (0)->GetIfIndex ());
    local.SetPhysicalAddress (GetNode ()->GetDevice (0)->GetAddress ());
    local.SetProtocol (GN_ETHERTYPE);
    if (m_socket->Bind (local) == -1)
    {
      NS_FATAL_ERROR ("Failed to bind client socket");
    }

    /* Set the socket to broadcast */
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
    m_denService.setStationProperties (std::stol(m_id.substr (3)), StationType_passengerCar);
    m_denService.addDENRxCallback (std::bind(&trafficManagerClient80211p::receiveDENM,this,std::placeholders::_1,std::placeholders::_2));
    m_denService.setRealTime (m_real_time);
    m_denService.setSocketRx (m_socket);

    /* Set sockets, callback, station properties and TraCI VDP in CABasicService */
    m_caService.setSocketTx (m_socket);
    m_caService.setSocketRx (m_socket);
    m_caService.addCARxCallback (std::bind(&trafficManagerClient80211p::receiveCAM,this,std::placeholders::_1,std::placeholders::_2));
    m_caService.setStationProperties (std::stol(m_id.substr (3)), StationType_passengerCar);
    m_caService.setRealTime (m_real_time);
    VDP* traci_vdp = new VDPTraCI(m_client,m_id);
    m_caService.setVDP(traci_vdp);
    m_denService.setVDP(traci_vdp);

    // xgregorm: kazdu odoslanu CAM zaznamename (merania, hybrid, kontrola odstupov ETSI)
    m_sid = std::stol (m_id.substr (3));
    m_caService.addCamTxCallback ([this] () {
      libsumo::TraCIPosition p = m_client->TraCIAPI::vehicle.getPosition (m_id);
      // generationDeltaTime tejto CAM (rovnaky cas ako v generateAndEncodeCam) - parovanie pri LTE/NR
      long gdt = compute_timestampIts (m_real_time) % 65536;
      double t = Simulator::Now ().GetSeconds ();
      int sent = xghyb::WIFI;
      if (m_xg_hybrid)
        {
          // cesty, ktorymi CAM naozaj odisla (zvolene, v aute dostupne a nevypadnute)
          sent = xgEffectivePath ();
          m_probe_due = false; // skuska (ak bola) prave odisla
          for (int b : {xghyb::WIFI, xghyb::LTE, xghyb::NR})
            if (sent & b)
              m_hyb_last_tx[xghyb::idx (b)] = t;
          m_hyb_cnt[sent] += xgstats::measuring (); // xgregorm: --warmup
          xghyb::timeline ()[(int) t][sent]++;
        }
      xgstats::onCamTx (m_sid, p.x, p.y, gdt, sent & (xghyb::WIFI | xghyb::NR), sent, GetNode ()->GetId ());
      if (m_last_tx >= 0)
        {
          // kontrola ETSI EN 302 637-2: odstup dvoch CAM 100 ms .. 1000 ms
          double gap = t - m_last_tx;
          m_gap_min = std::min (m_gap_min, gap);
          m_gap_max = std::max (m_gap_max, gap);
          m_gap_low += gap < 0.1 - 1e-6;
          m_gap_high += gap > 1.0 + 1e-6;
        }
      m_last_tx = t;
    });
    // xgregorm: hybrid - CAM z CA sluzby ide cez prepinac: 802.11p (povodny socket), LTE (UDP na server
    // v sieti, ServerAddr) a/alebo NR-V2X (UDP multicast 225.0.0.0:19 cez sidelink). Prijem: 802.11p a NR
    // (od LTE auto nic nedostava). Beacony RSU cez NR chodia na port 20.
    if (m_xg_hybrid)
      {
        Ptr<Socket> lte, nr;
        if (m_hyb_mask & xghyb::LTE)
          {
            lte = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
            if (lte->Bind () == -1)
              NS_FATAL_ERROR ("LTE path needs an IPv4 (LTE) interface on the vehicle");
            lte->Connect (InetSocketAddress (m_server_addr, 9));
          }
        if (m_hyb_mask & xghyb::NR)
          {
            Ptr<NetDevice> nrDev = xghyb::findDevice (GetNode (), "ns3::NrUeNetDevice");
            if (!nrDev)
              NS_FATAL_ERROR ("NR-V2X path needs an NR sidelink device on the vehicle");
            nr = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
            if (nr->Bind (InetSocketAddress (Ipv4Address::GetAny (), 19)) == -1)
              NS_FATAL_ERROR ("Failed to bind the NR-V2X socket");
            nr->BindToNetDevice (nrDev);
            nr->Connect (InetSocketAddress (Ipv4Address ("225.0.0.0"), 19));
            nr->SetRecvCallback (MakeCallback (&GeoNet::receiveGN, m_geoNet));
            if (m_xg_hybrid == 2)
              {
                m_nr_beacon_socket = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
                m_nr_beacon_socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), 20));
                m_nr_beacon_socket->BindToNetDevice (nrDev);
                m_nr_beacon_socket->SetRecvCallback (MakeCallback (&trafficManagerClient80211p::xgReceiveBeacon, this));
              }
          }
        // zaciatok: vsetky dostupne cesty (adaptivny rezim to po prvom ticku zuzi)
        m_hyb_path = m_hyb_base = m_hyb_mask;
        if (m_xg_load || m_xg_qos)
          m_load_rnd = CreateObject<UniformRandomVariable> ();
        // pravidlo zataze: kazde auto ma vlastny prah PRB pre zalohu LTE (rovnomerne XgLteHigh +- 0.2) - pri raste
        // vytazenia bunky zalohu vypina postupne cast aut, nie vsetky naraz
        if (m_xg_load)
          m_lte_thr = m_lte_high + 0.4 * (m_load_rnd->GetValue () - 0.5);
        if (m_xg_clean)
          {
            if (!m_load_rnd)
              m_load_rnd = CreateObject<UniformRandomVariable> ();
            double u0 = m_load_rnd->GetValue ();
            m_main = m_init_main == 1 || (m_init_main == 2 && u0 < 0.5) ? xghyb::NR : xghyb::WIFI;
            if (!(m_hyb_mask & m_main))
              m_main = xgOtherDirect (m_main);
            m_hyb_path = m_hyb_base = m_hyb_mask & m_main;
            if (m_probe_hz > 0) // skusky aut nie su v rovnakom case
              m_probe_t = Simulator::Now ().GetSeconds () - m_load_rnd->GetValue () / m_probe_hz;
          }
        if (lte && xgstats::remPeriod () > 0)
          {
            m_rem_sock = Socket::CreateSocket (GetNode (), UdpSocketFactory::GetTypeId ());
            m_rem_sock->Bind ();
            m_rem_sock->Connect (InetSocketAddress (m_server_addr, 5001));
          }
        Ptr<XgHybridSocket> hs = Create<XgHybridSocket> (m_socket, lte, nr, [this] () { return xgEffectivePath (); });
        if (m_xg_qos || !xgstats::zones ().empty ())
          {
            // pravidlo kvality: CAM nesie v poli Reserved hlavicky GeoNet technologie a poradove cislo, prijem
            // 802.11p aj NR ide najprv cez klienta (precita znacku), potom do GeoNet
            hs->SetMark ([this] () { return xgMark (); });
            m_rx_proxy = Create<XgRxProxySocket> (GetNode ());
            // callback drzi Ptr<GeoNet> ako povodny (MakeCallback(&GeoNet::receiveGN, m_geoNet)) - inak by GeoNet
            // na konci behu zanikol s beziacimi casovacmi LocT (NS_FATAL "Event is still running while destroying")
            m_socket->SetRecvCallback (MakeBoundCallback (&trafficManagerClient80211p::xgRxGnStatic, m_geoNet, this));
            if (nr)
              nr->SetRecvCallback (MakeBoundCallback (&trafficManagerClient80211p::xgRxGnStatic, m_geoNet, this));
          }
        m_caService.setSocketTx (hs);
      }
    // xgregorm: algoritmus relevancie - brana pred kazdou CAM spustenou v CA sluzbe
    // xgregorm: --dcc - T_GenCam_DCC (CAM cez 802.11p nie castejsie nez Toff stavu DCC) ide cez tu istu branu
    if (m_xg_relax || xgdcc::wOn ())
      m_caService.setCamGate (std::bind (&trafficManagerClient80211p::camGate, this, std::placeholders::_1));
    // xgregorm: adaptivny hybrid - beacon RSU (zoznam pocutych CAM) cez 802.11p
    if (m_xg_hybrid == 2 && (m_hyb_mask & xghyb::WIFI))
      {
        m_xg_socket = Socket::CreateSocket (GetNode (), tid);
        PacketSocketAddress xlocal;
        xlocal.SetSingleDevice (GetNode ()->GetDevice (0)->GetIfIndex ());
        xlocal.SetPhysicalAddress (GetNode ()->GetDevice (0)->GetAddress ());
        xlocal.SetProtocol (XG_ETHERTYPE);
        if (m_xg_socket->Bind (xlocal) == -1)
          NS_FATAL_ERROR ("Failed to bind the xgregorm beacon socket");
        m_xg_socket->SetRecvCallback (MakeCallback (&trafficManagerClient80211p::xgReceiveBeacon, this));
      }

    /* Schedule CAM dissemination */
    if(m_send_cam == true)
    {
      // xgregorm: srand(Now) dal autam vzniknutym v rovnakom kroku rovnaky posun -> CAM sa vysielali
      // naraz a kolidovali; oprava rovnaka ako autori urobili v emergencyVehicleAlert.cc
      Ptr<UniformRandomVariable> desync_rvar = CreateObject<UniformRandomVariable> ();
      desync_rvar->SetAttribute ("Min", DoubleValue (0.0));
      desync_rvar->SetAttribute ("Max", DoubleValue (1.0));
      double desync = desync_rvar->GetValue ();
      m_caService.startCamDissemination(desync);
      if (m_xg_relax || m_xg_hybrid == 2)
        m_xg_tick_ev = Simulator::Schedule (Seconds (desync), &trafficManagerClient80211p::xgTick, this);
      if (m_xg_color && m_xg_hybrid)
        m_xg_color_ev = Simulator::ScheduleNow (&trafficManagerClient80211p::xgColorTick, this);
      if (m_xg_hybrid)
        {
          xgstats::pathNow ()[GetNode ()->GetId ()] = [this] () { return xgPathNow (); };
          xgstats::connNow ()[GetNode ()->GetId ()] = [this] () { return xgConnNow (); };
        }
    }
  }

  // ---------------------------------------------------------------- xgregorm: moje algoritmy CAM

  void
  trafficManagerClient80211p::xgCount (uint64_t &counter)
  {
    // jedna potlacena CAM = jedna kontrola CA sluzby (gate sa moze volat viackrat za kontrolu)
    double now = Simulator::Now ().GetSeconds ();
    if (now != m_last_gate_count)
      counter++;
    m_last_gate_count = now;
  }

  bool
  trafficManagerClient80211p::xgRelevant ()
  {
    // auto je relevantne, ak je na trase k semaforu blizsie nez XgRelevance (radic potrebuje plnu frekvenciu
    // CAM len na prijazde); vzdialenost z mapy (v realite digitalna mapa / MAPEM)
    return !m_next_tls.empty () && m_next_dist < m_xg_relevance;
  }

  bool
  trafficManagerClient80211p::camGate (bool dynamic)
  {
    // ETSI EN 302 637-2: odstup CAM je vzdy medzi T_GenCamMin = 100 ms a T_GenCamMax = 1000 ms.
    // Mimo relevantnej oblasti len predlzime minimalny odstup dynamicky spustenych CAM na 1 s (ako DCC:
    // T_GenCam_DCC <= T_GenCamMax), casovo spustena CAM po T_GenCam ide vzdy.
    double gap = Simulator::Now ().GetSeconds () - m_last_tx;
    if (gap < 0.1 - 1e-6)
      return false; // tesne po vynutenej CAM (forceCam), dalsia este nesmie ist
    if (m_xg_relax && !m_relevant && gap < 0.95)
      {
        xgCount (m_xg_relaxed);
        return false;
      }
    // xgregorm: DCC (ETSI EN 302 637-2, T_GenCam_DCC): dynamicky spustena CAM cez 802.11p najskor po Toff stavu DCC
    if (dynamic && xgdcc::wOn () && (xgPathNow () & xghyb::WIFI) && gap < xgdcc::camGap (GetNode ()->GetId ()) - 1e-6)
      {
        if (Simulator::Now ().GetSeconds () != m_last_gate_count)
          xgdcc::cnt ()[7]++;
        m_last_gate_count = Simulator::Now ().GetSeconds ();
        return false;
      }
    return true;
  }

  void
  trafficManagerClient80211p::xgTick ()
  {
    // najblizsi semafor na trase a vzdialenost k nemu (staticka mapa, v realite MAPEM / digitalna mapa)
    std::vector<libsumo::TraCINextTLSData> nt = m_client->TraCIAPI::vehicle.getNextTLS (m_id);
    m_next_tls = nt.empty () ? "" : nt[0].id;
    m_next_dist = nt.empty () ? 1e9 : nt[0].dist;
    if (m_xg_hybrid == 2 && !m_next_tls.empty ())
      {
        // som na prijazde k tomuto semaforu? (cesty, ktorych pruhy semafor riadi = MAPEM / digitalna mapa)
        auto ap = m_tls_approach.find (m_next_tls);
        if (ap == m_tls_approach.end ())
          {
            std::set<std::string> edges;
            for (const std::string &lane : m_client->TraCIAPI::trafficlights.getControlledLanes (m_next_tls))
              edges.insert (lane.substr (0, lane.find_last_of ('_')));
            ap = m_tls_approach.emplace (m_next_tls, edges).first;
          }
        m_on_approach = ap->second.count (m_client->TraCIAPI::vehicle.getRoadID (m_id)) > 0;
      }
    else
      m_on_approach = false;
    if (m_xg_relax)
      {
        bool rel = xgRelevant ();
        if (rel != m_relevant)
          {
            // vstup do oblasti aj odchod z nej oznamime hned, aby radic mal spravny stav
            // (ak CAM odisla pred menej nez 100 ms, radic ma cerstvy stav a dalsia CAM by porusila T_GenCamMin)
            m_relevant = rel;
            if (Simulator::Now ().GetSeconds () - m_last_tx >= 0.1 - 1e-6)
              {
                m_xg_force++;
                m_caService.forceCam ();
              }
          }
      }
    if (m_xg_hybrid == 2)
      xgHybridDecide ();
    m_xg_tick_ev = Simulator::Schedule (MilliSeconds (100), &trafficManagerClient80211p::xgTick, this);
  }

  void
  trafficManagerClient80211p::xgColorTick ()
  {
    // xgregorm: farba auta v SUMO podla hlavnej technologie, ktorou by prave odisla CAM (len vizualizacia, na siet
    // nema vplyv). Pri viacerych naraz ide ta ista CAM ako kopia; hlavna = priama technologia pre susedov (802.11p,
    // inak NR), LTE len ak priama nie je. Skuska 802.11p z NR (raz za 1 s, jedna CAM) sa nezobrazuje, auto by blikalo.
    int p = xgPathNow ();
    int prim = (p & xghyb::WIFI) ? 1 : (p & xghyb::NR) ? 2 : (p & xghyb::LTE) ? 3 : 0;
    if (prim != m_xg_color_last)
      {
        // siva = nic, modra = 802.11p, zelena = NR-V2X, cervena = LTE
        static const unsigned char rgb[4][3] = {{128, 128, 128}, {0, 90, 255}, {0, 200, 0}, {255, 0, 0}};
        libsumo::TraCIColor c;
        c.r = rgb[prim][0];
        c.g = rgb[prim][1];
        c.b = rgb[prim][2];
        c.a = 255;
        m_client->TraCIAPI::vehicle.setColor (m_id, c);
        m_xg_color_last = prim;
      }
    m_xg_color_ev = Simulator::Schedule (MilliSeconds (100), &trafficManagerClient80211p::xgColorTick, this);
  }

  int
  trafficManagerClient80211p::xgConnNow ()
  {
    // priama technologia je "pripojena", ak ju auto ma, nema vypadok a za poslednu 1 s pocul RSU (beacon alebo CAM RSU)
    double now = Simulator::Now ().GetSeconds ();
    int c = 0;
    if ((m_hyb_mask & xghyb::WIFI) && xghyb::up (xghyb::WIFI) && now - m_rsu_heard[0] < 1.0)
      c |= xghyb::WIFI;
    if ((m_hyb_mask & xghyb::NR) && xghyb::up (xghyb::NR) && now - m_rsu_heard[2] < 1.0)
      c |= xghyb::NR;
    return c;
  }

  int
  trafficManagerClient80211p::xgPathNow ()
  {
    // cesty, ktorymi by prave odisla CAM, bez skusky 802.11p z NR (raz za 1 s jedna CAM) - pre farbu a priebezny vypis
    if (m_xg_clean)
      return m_hyb_base;
    int p = xgEffectivePath ();
    if (m_xg_hybrid == 2 && (p & xghyb::NR) && !(m_hyb_base & xghyb::WIFI))
      p &= ~xghyb::WIFI;
    return p;
  }

  int
  trafficManagerClient80211p::xgEffectivePath ()
  {
    // cesta pre CAM odosielanu prave teraz: zvolena (m_hyb_path), bez vypadnutych technologii; ak vypadli
    // vsetky zvolene (a nejaka zvolena bola - 0 = rozhodnutie neposielat), hned nahradna (802.11p, NR, LTE) -
    // modem o vypadku aj obnoveni vie a necaka na dalsie rozhodnutie
    int avail = 0;
    for (int b : {xghyb::WIFI, xghyb::LTE, xghyb::NR})
      if ((m_hyb_mask & b) && xghyb::up (b))
        avail |= b;
    int path = m_hyb_path & avail;
    if (!path && m_hyb_path)
      for (int b : {xghyb::WIFI, xghyb::NR, xghyb::LTE})
        if (avail & b)
          {
            path = b;
            break;
          }
    // a naopak: priama technologia sa prave obnovila, ale rozhodnutie ju este nema (islo len LTE) -> hned ju pridame,
    // inak by susedia do dalsieho rozhodnutia (najviac 100 ms) CAM nedostali
    int direct = avail & (xghyb::WIFI | xghyb::NR);
    if (direct && !(path & (xghyb::WIFI | xghyb::NR)))
      path |= (direct & xghyb::WIFI) ? xghyb::WIFI : xghyb::NR;
    if (m_xg_clean && m_probe_due)
      path |= direct; // skuska: tato CAM ide oboma priamymi technologiami
    // napad 1: 802.11p by CAM drzalo na brane DCC dlhsie nez rozpocet -> tato CAM ide cez NR. Rozhodnutie plati pre
    // cely okamih odoslania (po odoslani je brana zatvorena, vypis ciest musi vidiet to iste)
    if (m_xg_clean && xgdcc::offload () > 0 && (path & xghyb::WIFI) && !(path & xghyb::NR) && (avail & xghyb::NR))
      {
        double now = Simulator::Now ().GetSeconds ();
        if (now != m_dcc_off_t)
          {
            m_dcc_off_t = now;
            m_dcc_off_v = xgdcc::gateWait (GetNode ()->GetId ()) > xgdcc::offload ();
            xgdcc::offloadN () += m_dcc_off_v && xgstats::measuring ();
          }
        if (m_dcc_off_v)
          path = (path & ~xghyb::WIFI) | xghyb::NR;
      }
    return path;
  }

  void
  trafficManagerClient80211p::xgHybridDecide ()
  {
    // adaptivny hybrid (rozhoduje auto kazdych 100 ms len z toho, co samo vie):
    //  - mimo oblasti radica (nie na prijazde k semaforu a dalej nez XgHybRange): len priama technologia pre susedov
    //    (na to je CAM) - 802.11p, ak je dostupne, inak NR-V2X
    //  - v oblasti radica podla potvrdeni RSU (beacon za poslednu 1 s a podiel potvrdenych CAM >= XgHybQmin):
    //    1. RSU potvrdzuje 802.11p -> 802.11p
    //    2. RSU potvrdzuje len NR (napr. vacsi dosah NR) -> NR; raz za sekundu jedna CAM aj cez 802.11p
    //       (skuska - RSU, ktora auto cez 802.11p nepocuje, mu cez 802.11p ani nic nepotvrdi)
    //    3. nepotvrdzuje nic -> vsetky priame technologie (tak sa zisti, ktora RSU dosiahne) + LTE ako zaloha
    //       (do siete nezavisle od dosahu); LTE vypne az po XgHybDwell s potvrdenia (hysterezia)
    //  - bez priamej technologie len LTE, a to len ak auto radic potrebuje (susedia CAM cez LTE aj tak nedostanu,
    //    LTE by sa zbytocne zatazovalo - pri vypadku 802.11p v hustej premavke je to jeho kapacitny limit)
    // Vypadnuta technologia (xghyb::up) je pre auto nedostupna - modem o vypadku vie.
    double now = Simulator::Now ().GetSeconds ();
    int avail = 0;
    for (int b : {xghyb::WIFI, xghyb::LTE, xghyb::NR})
      if ((m_hyb_mask & b) && xghyb::up (b))
        avail |= b;
    // radic auto potrebuje na celom prijazde k semaforu (pocita vsetky auta na nom) a do XgHybRange pred nim
    bool need = !m_next_tls.empty () && (m_on_approach || m_next_dist < m_hyb_range);
    auto lk = m_hyb_link.find (m_next_tls);
    auto ok = [&] (int b) {
      if (!(avail & b) || lk == m_hyb_link.end ())
        return false;
      const HybLink &l = lk->second[xghyb::idx (b)];
      return now - l.last < 1.0 && l.q >= m_hyb_qmin;
    };
    int direct = avail & (xghyb::WIFI | xghyb::NR);
    // hlavna priama technologia: bez pravidiel zataze a kvality 802.11p (ak je), inak podla nich (xgLoadUpdate)
    int prim = (direct & xghyb::WIFI) ? xghyb::WIFI : xghyb::NR;
    if (m_xg_load || m_xg_qos)
      {
        xgLoadUpdate (avail);
        if (direct & m_main)
          prim = m_main;
      }
    if (m_xg_clean && (direct & m_main))
      prim = m_main;
    int other = direct & xgOtherDirect (prim);
    int path;
    int probe = 0;
    bool confirmed = true;
    if (need)
      m_hyb_need_t = now;
    if (m_xg_clean)
      {
        // cista politika: jedna priama technologia; LTE (V2N) len tam, kam RSU podla mapy priamo nedosiahne
        // napad 2: vybava susedov (v realite schopnost v CAM): komu auto dosiahne
        if (m_nb_equip && (direct & xghyb::WIFI) && (direct & xghyb::NR))
          {
            int only[2] = {0, 0};
            for (const auto &e : m_qst)
              if (now - e.second.t <= 1.5)
                {
                  int q = xgstats::eqOf (e.first);
                  only[0] += q == xghyb::WIFI;
                  only[1] += q == xghyb::NR;
                }
            prim = only[0] > only[1] ? xghyb::WIFI : only[1] > only[0] ? xghyb::NR : prim;
            other = direct & xgOtherDirect (prim);
          }
        // napad 3: ku ktorej RSU idem a akou technologiou ju dosiahnem (staticka mapa)
        if (m_rsu_route && need)
          {
            auto rq = xgstats::rsuEqTls ().find (m_next_tls);
            if (rq != xgstats::rsuEqTls ().end () && !(rq->second & prim) && (rq->second & direct))
              {
                prim = rq->second & direct;
                other = direct & xgOtherDirect (prim);
              }
          }
        // napad 5: pri slepej krizovatke NR
        if (m_geo_rat > 0 && need && m_next_dist < m_geo_rat && (direct & xghyb::NR))
          {
            prim = xghyb::NR;
            other = direct & xghyb::WIFI;
          }
        int clean = direct ? prim : now - m_hyb_need_t < m_hyb_dwell ? avail & xghyb::LTE : 0;
        // napad 2: niektori susedia pocuju len jednu a ini len druhu technologiu: kopia
        if (m_nb_equip && other)
          {
            int only[2] = {0, 0};
            for (const auto &e : m_qst)
              if (now - e.second.t <= 1.5)
                {
                  int q = xgstats::eqOf (e.first);
                  only[0] += q == xghyb::WIFI;
                  only[1] += q == xghyb::NR;
                }
            if (only[0] > 0 && only[1] > 0)
              clean |= other;
          }
        // napad 4: selektivna kopia pri krizovatke
        if (m_dup_near > 0 && other && need && m_next_dist < m_dup_near)
          clean |= other;
        if (direct && need && m_next_dist >= m_cell_dist)
          clean |= avail & xghyb::LTE;
        if (m_probe_hz > 0 && other && now - m_probe_t >= 1.0 / m_probe_hz)
          {
            m_probe_due = true;
            m_probe_t = now;
            m_hyb_probes += xgstats::measuring ();
          }
        if (clean != m_hyb_base)
          m_hyb_switch += xgstats::measuring ();
        m_hyb_base = m_hyb_path = clean;
        return;
      }
    if (!direct)
      // CAM cez LTE dostanu len radice semaforov - kto nie je potrebny, neposiela; po opusteni prijazdu este
      // XgHybDwell s, aby radic videl, ze auto odislo (inak by ho dalej pocital na prijazde)
      path = now - m_hyb_need_t < m_hyb_dwell ? avail & xghyb::LTE : 0;
    else if (!need)
      path = prim;
    else if (ok (prim))
      path = prim;
    else if (other && ok (other))
      {
        path = other;
        if (prim == xghyb::WIFI && now - m_hyb_probe_t >= 1.0)
          {
            probe = xghyb::WIFI; // skuska 802.11p (jeden tick = najviac jedna CAM)
            m_hyb_probe_t = now;
            m_hyb_probes += xgstats::measuring ();
          }
      }
    else if (!m_xg_load)
      {
        path = direct | (avail & xghyb::LTE);
        confirmed = false;
      }
    else
      {
        // pravidlo zataze: RSU nepotvrdzuje ziadnu priamu technologiu. Druhu priamu pridame len ak nie je zahltena
        // (kopia do zahlteneho kanala by stratu este zhorsila), LTE len ak bunka nie je pretazena - inak by zaloha
        // zahltila jedinu bunku (Dennik 2026-10-20: 54 % CAM so zalohou LTE, PRB 0.48)
        path = prim;
        if (other && m_load_avg[xghyb::idx (other)] < m_cbr_high)
          path |= other;
        if ((avail & xghyb::LTE) && m_load_avg[1] < m_lte_thr)
          path |= xghyb::LTE;
        confirmed = false;
      }
    if (!confirmed)
      m_hyb_ok_since = -1;
    else if (m_hyb_ok_since < 0)
      m_hyb_ok_since = now;
    if (need && direct && confirmed && (m_hyb_path & xghyb::LTE) && now - m_hyb_ok_since < m_hyb_dwell)
      path |= avail & xghyb::LTE; // hysterezia: LTE drzime este XgHybDwell s po potvrdeni
    // prepnutie = zmena zvolenych technologii (skuska 802.11p sa za prepnutie nepocita)
    if (path != m_hyb_base)
      m_hyb_switch += xgstats::measuring ();
    m_hyb_base = path;
    m_hyb_path = path | probe;
  }

  void
  trafficManagerClient80211p::xgLoadUpdate (int avail)
  {
    // xgregorm: vyber hlavnej priamej technologie podla zataze (kazde auto samo, z toho, co realne vie):
    //  - CBR 802.11p meria kazda stanica sama (ETSI TS 102 687), SL-CBR NR-V2X tiez (3GPP TS 38.215; v ns-3 odhad),
    //    vytazenie bunky LTE oznamuje siet (3GPP Rel-16 predikcia QoS pre V2X, TS 23.287) - priemer za ~1 s
    //  - raz za XgLoadPeriod s: na 802.11p pri CBR >= XgCbrHigh (ETSI DCC active) a menej zatazenom NR prejde na NR
    //    s pravdepodobnostou (CBR - ciel) / CBR, ciel = (XgCbrHigh + XgCbrLow) / 2 - presunie sa len cast aut, ktora
    //    kanal vrati k cielu (keby presli vsetky naraz, zahltili by NR a vratili sa = kmitanie);
    //    na NR sa vrati, ked CBR 802.11p klesne pod XgCbrLow (s pravdepodobnostou (XgCbrLow - CBR) / XgCbrLow),
    //    alebo ked sa zahlti NR a 802.11p je na tom lepsie
    //  - auto ma oba prijimace (prijima 802.11p aj NR), susedia ho teda pocuju aj po presune
    double a = 0.1; // klzavy priemer vzoriek po 100 ms, casova konstanta ~1 s
    uint32_t node = GetNode ()->GetId ();
    for (int k = 0; k < 3; k++)
      if (xgstats::loadFn ()[k])
        {
          double v = xgstats::loadFn ()[k] (node);
          m_load_avg[k] = m_load_init ? (1 - a) * m_load_avg[k] + a * v : v;
        }
    m_load_init = true;
    double now = Simulator::Now ().GetSeconds ();
    if (m_xg_pred || xgstats::featLog ())
      {
        // mapa kvality: vzorka zataze kanala 802.11p a NR (100 ms) v bunke, kde auto prave je
        auto me = xgstats::vehPos ().find (m_sid);
        if (me != xgstats::vehPos ().end ())
          {
            for (int k = 0; k < 2; k++)
              if (xgstats::loadFn ()[k ? 2 : 0])
                xgRemAdd (me->second.first, me->second.second, k, 0, 0, xgstats::loadFn ()[k ? 2 : 0] (node), 1);
          }
        xgRemFlush (now);
        // vyhodnotenie predikcii: spolahlivost namerana autom za horizont vs. predikcia a vs. "zostane ako teraz"
        while (!m_pred_q.empty () && now - m_pred_q.front ().t >= m_pred_h - 1e-9)
          {
            const PRec &p = m_pred_q.front ();
            for (int k = 0; k < 2; k++)
              {
                double e = m_qc_exp[k] - p.e0[k];
                if (e < 10 || p.t < xgstats::warmup ())
                  continue;
                double real = (m_qc_ok[k] - p.o0[k]) / e;
                xgstats::predErr ()[k][0] += std::abs (p.pred[k] - real);
                xgstats::predErr ()[k][1] += std::abs (p.now[k] - real);
                xgstats::predBias ()[k][0] += p.pred[k] - real;
                xgstats::predBias ()[k][1] += p.now[k] - real;
                xgstats::predN ()[k]++;
                xgstats::predCover ()[p.map[k] ? 0 : 1]++;
              }
            m_pred_q.pop_front ();
          }
      }
    // pravidlo kvality: klzave sucty dorucenych / ocakavanych CAM zabudaju s casovou konstantou 2 s
    double fq = std::exp (-0.1 / 2.0);
    for (int k = 0; k < 2; k++)
      {
        m_q_exp[k] *= fq;
        m_q_ok[k] *= fq;
      }
    bool both = (avail & xghyb::WIFI) && (avail & xghyb::NR);
    if (!both)
      {
        // len jedna priama technologia (vypadok / v aute nie je) - nie je kam presuvat
        int d = avail & (xghyb::WIFI | xghyb::NR);
        if (d && d != m_main)
          m_main = d;
        return;
      }
    if (now - m_load_eval_t < m_load_period)
      return;
    m_load_eval_t = now;
    if (m_oracle)
      {
        // referencia pre predikciu: dokonala znalost zon, bez merania a bez postupneho presunu aut
        double pq[2], pc[2], loss[2] = {0, 0};
        int nq[2], nc[2];
        std::vector<std::pair<double, double>> pts;
        auto me = xgstats::vehPos ().find (m_sid);
        if (me != xgstats::vehPos ().end ())
          pts.push_back (me->second);
        xgPredict (pq, nq, pc, nc, &pts);
        for (const auto &pt : pts)
          for (const xgstats::Zone &z : xgstats::zones ())
            if (now >= z.t0 && now < z.t1 && std::hypot (pt.first - z.x, pt.second - z.y) <= z.r)
              loss[z.k] += z.p;
        int best = loss[0] < loss[1] ? xghyb::WIFI : loss[1] < loss[0] ? xghyb::NR : m_main;
        if (best != m_main)
          {
            m_main = best;
            m_pred_sw += xgstats::measuring ();
          }
        return;
      }
    double cw = m_load_avg[0], cn = m_load_avg[2];
    double target = (m_cbr_high + m_cbr_low) / 2;
    double u = m_load_rnd->GetValue ();
    int prev = m_main;
    // pravidlo kvality (XgQosRat, ma prednost): spolahlivost r = CAM susedov dorucene nacas / ocakavane, kazda
    // technologia zvlast; prejde na druhu, ak je spolahlivejsia aspon o XgQosMargin a nie je zahltena, s
    // pravdepodobnostou zisk / 0.4 (pri zisku 0.2 polovica aut za 1 s - aby sa nepresunuli vsetky naraz)
    int im = m_main == xghyb::WIFI ? 0 : 1, io = 1 - im;
    const double kQmin = m_xg_clean ? 10 : 20; // najmenej ocakavanych CAM v klzavom okne, inak je kvalita nezname
    bool known = m_xg_qos && !m_nb_equip && m_q_exp[0] >= kQmin && m_q_exp[1] >= kQmin;
    double r[2] = {known ? m_q_ok[0] / m_q_exp[0] : 0, known ? m_q_ok[1] / m_q_exp[1] : 0};
    double lo = io == 0 ? cw : cn;
    // datova sada pre predikcny model a vstupy nauceneho modelu
    xgstats::PredIn in[2];
    bool useModel = m_xg_pred && !xgstats::predCoef ()[0].empty ();
    if (xgstats::featLog () || useModel)
      {
        double pq[2], pc[2], nb, nbk[2], nbf;
        int nq[2], nc[2];
        xgPredict (pq, nq, pc, nc);
        xgNeighbours (&nb, nbk, &nbf, m_pred_h);
        double vv = m_client->TraCIAPI::vehicle.getSpeed (m_id);
        double cc[2] = {cw, cn};
        for (int k = 0; k < 2; k++)
          {
            double rr = m_q_exp[k] > 0 ? m_q_ok[k] / m_q_exp[k] : 0.5;
            in[k] = {rr, m_q_exp[k], cc[k], nq[k] ? pq[k] / nq[k] : 0, (double) nq[k], nc[k] ? pc[k] / nc[k] : cc[k],
                     nb, nbk[k], nbf, vv, m_on_approach ? 1.0 : 0.0, m_next_tls.empty () ? 300.0 : m_next_dist};
            if (xgstats::featLog ())
              {
                auto me = xgstats::vehPos ().find (m_sid);
                *xgstats::featLog () << now << ',' << m_sid << ',' << (m_main == xghyb::NR) << ',' << k << ',' << in[k].r << ','
                                     << in[k].qexp << ',' << in[k].cbr << ',' << in[k].remr << ',' << in[k].remn << ','
                                     << in[k].remc << ',' << nb << ',' << nbk[k] << ',' << nbf << ',' << vv << ',' << in[k].appr
                                     << ',' << in[k].dist << ',' << m_qc_exp[k] << ',' << m_qc_ok[k] << ','
                                     << (me != xgstats::vehPos ().end () ? me->second.first : 0) << ','
                                     << (me != xgstats::vehPos ().end () ? me->second.second : 0) << '\n';
              }
          }
      }
    if (known && m_xg_pred)
      {
        // predikcia (XgPredRat): body trasy o 1, 2, ... XgPredHorizon s; spolahlivost a zataz kazdej technologie =
        // priemer vlastneho sucasneho merania a hodnot mapy v tych bodoch (bunka bez dost dat sa vynecha)
        double pq[2], pc[2];
        int nq[2], nc[2];
        if (useModel)
          nq[0] = nq[1] = nc[0] = nc[1] = 0, pq[0] = pq[1] = pc[0] = pc[1] = 0;
        else
          xgPredict (pq, nq, pc, nc);
        PRec rec {now, {0, 0}, {r[0], r[1]}, {m_qc_exp[0], m_qc_exp[1]}, {m_qc_ok[0], m_qc_ok[1]}, {nq[0] > 0, nq[1] > 0}};
        double cur[2] = {cw, cn};
        for (int k = 0; k < 2; k++)
          {
            if (useModel)
              {
                // naučený model: spolahlivost na horizonte; zataz cielovej technologie z mapy (ak ju ma) v priemere so sucasnou
                r[k] = xgstats::predModel (k, in[k]);
                cur[k] = (cur[k] + in[k].remc) / 2;
                rec.map[k] = in[k].remn > 0;
              }
            else
              {
                r[k] = (r[k] + pq[k]) / (1 + nq[k]);
                cur[k] = (cur[k] + pc[k]) / (1 + nc[k]);
              }
            rec.pred[k] = r[k];
          }
        m_pred_q.push_back (rec);
        lo = cur[io];
      }
    double gain = known ? r[io] - r[im] : 0;
    if (known && gain > m_qos_margin && lo < m_cbr_high)
      {
        if (u < std::min (1.0, gain / 0.4))
          {
            m_main = xgOtherDirect (m_main);
            if (m_xg_pred)
              m_pred_sw += xgstats::measuring ();
            else
              m_qos_sw += xgstats::measuring ();
          }
        return;
      }
    if (!m_xg_load)
      return;
    // pravidlo zataze: nikdy na technologiu, ktora je namerane horsia o viac nez XgQosMargin
    bool worse = known && -gain > m_qos_margin;
    if (m_main == xghyb::WIFI)
      {
        if (cw >= m_cbr_high && cn < m_cbr_high && cn < cw && !worse && u < (cw - target) / cw)
          m_main = xghyb::NR;
      }
    else if (cn >= m_cbr_high && cw < cn)
      {
        if (!worse && u < (cn - target) / cn)
          m_main = xghyb::WIFI;
      }
    else if (cw < m_cbr_low && !worse && u < (m_cbr_low - cw) / m_cbr_low)
      m_main = xghyb::WIFI;
    if (m_main != prev)
      m_load_sw += xgstats::measuring ();
  }

  void
  trafficManagerClient80211p::xgPredict (double q[2], int nq[2], double c[2], int nc[2], std::vector<std::pair<double, double>> *pts)
  {
    // xgregorm: body, kde auto bude o 1, 2, ... XgPredHorizon s: pri sucasnej rychlosti pozdlz jeho trasy (ako navigacia;
    // pruhy trasy z mapy SUMO, dalsie cesty po pravom pruhu - bunka 20 m je hrubsia nez sirka cesty). Pre kazdu
    // technologiu sucet hodnot mapy v tych bodoch a pocet bodov, kde mapa ma dost dat (>= 10 CAM, >= 5 vzoriek zataze).
    static std::map<std::string, std::vector<std::pair<double, double>>> shapes; // tvar pruhu (staticka mapa)
    for (int k = 0; k < 2; k++)
      q[k] = c[k] = 0, nq[k] = nc[k] = 0;
    TraCIAPI::VehicleScope &veh = m_client->TraCIAPI::vehicle;
    if (m_route.empty ())
      m_route = veh.getRoute (m_id);
    std::vector<std::string> lanes {veh.getLaneID (m_id)};
    int ri = veh.getRouteIndex (m_id);
    for (int j = ri + 1; j >= 1 && j < (int) m_route.size () && lanes.size () < 8; j++)
      lanes.push_back (m_route[j] + "_0");
    double pos = veh.getLanePosition (m_id), v = veh.getSpeed (m_id);
    auto shape = [&] (const std::string &l) -> const std::vector<std::pair<double, double>> & {
      auto it = shapes.find (l);
      if (it == shapes.end ())
        {
          std::vector<std::pair<double, double>> s;
          for (const libsumo::TraCIPosition &p : m_client->TraCIAPI::lane.getShape (l))
            s.push_back ({p.x, p.y});
          it = shapes.emplace (l, s).first;
        }
      return it->second;
    };
    int H = (int) std::lround (m_pred_h);
    size_t li = 0, si = 0; // aktualny pruh a usek jeho tvaru
    double off = 0;        // vzdialenost od zaciatku useku si
    double x = 0, y = 0;
    for (int i = 1; i <= H; i++)
      {
        // posun o v * 1 s pozdlz trasy (prvy krok navyse z zaciatku pruhu na sucasnu polohu v nom)
        double need = (i == 1 ? pos : 0) + v;
        bool ok = false;
        while (li < lanes.size ())
          {
            const std::vector<std::pair<double, double>> &s = shape (lanes[li]);
            if (s.size () < 2)
              {
                li++, si = 0, off = 0;
                continue;
              }
            bool done = false;
            while (si + 1 < s.size ())
              {
                double dx = s[si + 1].first - s[si].first, dy = s[si + 1].second - s[si].second;
                double len = std::hypot (dx, dy);
                if (off + need <= len)
                  {
                    off += need;
                    need = 0;
                    x = s[si].first + dx * off / std::max (len, 1e-9);
                    y = s[si].second + dy * off / std::max (len, 1e-9);
                    done = true;
                    break;
                  }
                need -= len - off;
                off = 0;
                si++;
              }
            if (done)
              {
                ok = true;
                break;
              }
            li++, si = 0, off = 0;
          }
        if (!ok)
          break; // trasa konci skor nez horizont
        if (pts)
          pts->push_back ({x, y});
        xgstats::RemCell *cell = xgstats::remAt (x, y, false);
        if (!cell)
          continue;
        for (int k = 0; k < 2; k++)
          {
            if (cell->exp[k] >= 10)
              {
                q[k] += cell->ok[k] / cell->exp[k];
                nq[k]++;
              }
            if (cell->cbrN[k] >= 5)
              {
                c[k] += cell->cbr[k] / cell->cbrN[k];
                nc[k]++;
              }
          }
      }
  }

  void
  trafficManagerClient80211p::xgRemAdd (double x, double y, int k, double exp, double ok, double cbr, double cbrN)
  {
    // meranie do mapy kvality: bez hlaseni rovno do zdielanej mapy, s --rem-period do pamate auta (odide hlasenim)
    if (xgstats::remPeriod () <= 0 || !m_rem_sock)
      {
        xgstats::RemCell *c = xgstats::remAt (x, y, true);
        c->exp[k] += exp;
        c->ok[k] += ok;
        c->cbr[k] += cbr;
        c->cbrN[k] += cbrN;
        return;
      }
    xgstats::RemDelta &d = m_rem_buf[{(int) std::floor (x / xgstats::remCell ()), (int) std::floor (y / xgstats::remCell ())}];
    d.exp[k] += exp;
    d.ok[k] += ok;
    d.cbr[k] += cbr;
    d.cbrN[k] += cbrN;
  }

  void
  trafficManagerClient80211p::xgRemFlush (double now)
  {
    // hlasenie cez LTE: hlavicka 20 B + 16 B na bunku (id bunky, 2 technologie: spolahlivost, pocet, CBR) + 44 B BTP/GN a IP/UDP
    // sa nepouzije - ide cez IP; +93 B digest podpisu ako ostatne spravy. Do mapy sa dostane o remDelay neskor.
    if (xgstats::remPeriod () <= 0 || !m_rem_sock || now - m_rem_last < xgstats::remPeriod () || m_rem_buf.empty ())
      return;
    m_rem_last = now;
    uint32_t bytes = 20 + 16 * m_rem_buf.size () + 93;
    m_rem_sock->Send (Create<Packet> (bytes));
    if (xgstats::measuring ())
      {
        xgstats::remTx ()[0]++;
        xgstats::remTx ()[1] += bytes;
      }
    xgstats::RemBuf b;
    b.swap (m_rem_buf);
    Simulator::Schedule (Seconds (xgstats::remDelay ()), [b] () { xgstats::remApply (b); });
  }

  void
  trafficManagerClient80211p::xgNeighbours (double nb[1], double nbk[2], double nbf[1], double h)
  {
    // susedia z toho, co auto vie z ich CAM (poloha, rychlost, smer): teraz do 100 m, po technologiach podla ich znacky,
    // a odhad o h s (poloha + rychlost * h, u mna aj u nich; "lokalna dynamicka mapa")
    double now = Simulator::Now ().GetSeconds ();
    auto me = xgstats::vehPos ().find (m_sid);
    nb[0] = nbk[0] = nbk[1] = nbf[0] = 0;
    if (me == xgstats::vehPos ().end ())
      return;
    double mx = me->second.first, my = me->second.second;
    if (m_my_t >= 0 && now > m_my_t)
      {
        m_my_vx = (mx - m_my_x) / (now - m_my_t);
        m_my_vy = (my - m_my_y) / (now - m_my_t);
      }
    m_my_x = mx, m_my_y = my, m_my_t = now;
    double fx = mx + m_my_vx * h, fy = my + m_my_vy * h;
    for (const auto &e : m_qst)
      {
        const QSt &s = e.second;
        if (now - s.t > 1.5)
          continue;
        if (std::hypot (s.x - mx, s.y - my) <= 100)
          {
            nb[0]++;
            nbk[0] += s.has[0];
            nbk[1] += s.has[1];
          }
        if (std::hypot (s.x + s.vx * h - fx, s.y + s.vy * h - fy) <= 100)
          nbf[0]++;
      }
  }

  int
  trafficManagerClient80211p::xgMark ()
  {
    // znacka odosielanej CAM: priame technologie, ktorymi auto prave posiela (bez skusky 802.11p z NR - ta nie je
    // suvisly prud) + poradove cislo CAM mod 64
    int p = m_xg_clean ? xgEffectivePath () : xgPathNow ();
    int m = ((p & xghyb::WIFI) ? 0x80 : 0) | ((p & xghyb::NR) ? 0x40 : 0) | (m_tx_seq & 63);
    m_tx_seq++;
    return m;
  }

  void
  trafficManagerClient80211p::xgRxGn (Ptr<Socket> socket)
  {
    // prijem GeoNet (802.11p aj NR) pri pravidle kvality: precita znacku z pola Reserved zakladnej hlavicky,
    // paket potom spracuje GeoNet ako obvykle (receiveCAM ju najde v m_rx_mark)
    Ptr<Packet> p;
    Address from;
    while ((p = socket->RecvFrom (from)))
      {
        // xgregorm: --zones - lokalne zhorsenie prijmu
        auto mz = xgstats::vehPos ().find (m_sid);
        if (mz != xgstats::vehPos ().end () && xgstats::zoneDrop (socket == m_socket ? 0 : 1, mz->second.first, mz->second.second))
          continue;
        m_rx_mark = -1;
        if (p->GetSize () >= 6)
          {
            uint8_t h[6];
            p->CopyData (h, 6);
            if ((h[0] & 0x0f) == 1)
              m_rx_mark = h[1];
          }
        m_rx_proxy->Set (p, from);
        m_geoNet->receiveGN (m_rx_proxy);
        m_rx_mark = -1;
      }
  }

  void
  trafficManagerClient80211p::xgQosRx (unsigned long sender, int k, int mark, long gdt)
  {
    // kvalita technologie k (0 = 802.11p, 1 = NR) z pohladu prijimaca. Znacka CAM hovori, ktorymi technologiami
    // ju odosielatel poslal, a jej poradove cislo. Kazda CAM (aj ta, ktora prisla len druhou technologiou) sa po
    // 200 ms vyhodnoti: pre kazdu technologiu zo znacky ocakavana 1, dorucena, ak prisla touto technologiou, nacas,
    // ak oneskorenie (cas vzniku z generationDeltaTime, hodiny GNSS) <= XgLatBudget. CAM, ktore chybaju v poradovych
    // cislach (nedosli ziadnou technologiou), su stratene pre technologie, ktore mali obe susedne CAM v znacke.
    // Predpoklad: kanal je symetricky - co ja zle prijimam od susedov, to oni zle prijimaju odo mna.
    // Obmedzenie: sused, ktoreho nepocujem ziadnou technologiou, v odhade chyba.
    double now = Simulator::Now ().GetSeconds ();
    bool has[2] = {(mark & 0x80) != 0, (mark & 0x40) != 0};
    if (!has[0] && !has[1])
      return;
    int seq = mark & 63;
    long nowms = compute_timestampIts (m_real_time) % 65536;
    double lat = ((nowms - gdt + 65536) % 65536) / 1000.0;
    QSt &st = m_qst[sender];
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
        xgQosFinish (sender, st, st.recs.front ());
        st.recs.pop_front ();
      }
  }

  void
  trafficManagerClient80211p::xgQosFinish (unsigned long sender, QSt &st, const QRec &r)
  {
    int miss = 0;
    if (st.seq >= 0 && r.t0 - st.t < 3.0)
      {
        int g = (r.seq - st.seq) & 63;
        if (g == 0)
          return; // ta ista CAM este raz
        miss = g - 1;
      }
    // susedia do 100 m (ako skutocne PRR); poloha odosielatela je v jeho CAM, v simulacii zo vzorky SUMO (~100 ms)
    auto &vp = xgstats::vehPos ();
    auto me = vp.find (m_sid), he = vp.find (sender);
    if (he != vp.end ())
      {
        // poloha a rychlost suseda (v realite v jeho CAM) pre odhad susedov o chvilu
        if (st.pt >= 0 && r.t0 - st.pt >= 0.05)
          {
            st.vx = (he->second.first - st.x) / (r.t0 - st.pt);
            st.vy = (he->second.second - st.y) / (r.t0 - st.pt);
          }
        if (st.pt < 0 || r.t0 - st.pt >= 0.05)
          {
            st.x = he->second.first;
            st.y = he->second.second;
            st.pt = r.t0;
          }
      }
    bool near = me != vp.end () && he != vp.end ()
                && std::hypot (me->second.first - he->second.first, me->second.second - he->second.second) <= 100;
    // NR sidelink bearer vznika az v case nrFrom (autori: 2 s) - dovtedy sa NR nemeria (artefakt nastavenia)
    if (near && r.t0 >= xgstats::nrFrom ())
      for (int k = 0; k < 2; k++)
        {
          if (!r.has[k])
            continue;
          double e = 1 + (st.has[k] ? miss : 0);
          bool ok = r.got[k] && r.lat[k] <= m_lat_budget;
          m_q_exp[k] += e;
          m_q_ok[k] += ok;
          if (m_xg_pred || xgstats::featLog ())
            {
              // mapa kvality: meranie patri do bunky, kde auto (prijimac) prave je
              m_qc_exp[k] += e;
              m_qc_ok[k] += ok;
              xgRemAdd (me->second.first, me->second.second, k, e, ok, 0, 0);
            }
          if (r.t0 >= xgstats::warmup ())
            {
              m_qt_exp[k] += e;
              m_qt_rx[k] += r.got[k];
              m_qt_ok[k] += ok;
            }
        }
    st.seq = r.seq;
    st.has[0] = r.has[0];
    st.has[1] = r.has[1];
    st.t = r.t0;
  }

  void
  trafficManagerClient80211p::xgReceiveBeacon (Ptr<Socket> socket)
  {
    // adaptivny hybrid: beacon RSU "XG|semafor|zaciatok_okna_ms|id;id;..." = CAM, ktore RSU touto
    // technologiou (802.11p alebo NR, podla socketu) pocula od zaciatku okna
    Ptr<Packet> p;
    while ((p = socket->Recv ()))
      {
        std::vector<uint8_t> buf (p->GetSize ());
        p->CopyData (buf.data (), buf.size ());
        std::string txt (buf.begin (), buf.end ());
        std::vector<std::string> f;
        std::stringstream ss (txt);
        std::string item;
        while (std::getline (ss, item, '|'))
          f.push_back (item);
        if (f.size () < 3 || f[0] != "XG")
          continue;
        if (f.size () == 3)
          f.push_back ("");
        m_xg_beacons++;
        double now = Simulator::Now ().GetSeconds ();
        const std::string &tls = f[1];
        // pripojenie k infrastrukture (priebezny vypis): auto pocul nejaku RSU touto technologiou
        int tc = socket == m_nr_beacon_socket ? xghyb::idx (xghyb::NR) : xghyb::idx (xghyb::WIFI);
        if (xghyb::up (tc == 0 ? xghyb::WIFI : xghyb::NR))
          m_rsu_heard[tc] = now;
        if (tls.empty () || tls != m_next_tls)
          continue; // beacon inej krizovatky, nez ku ktorej idem
        // kvalita priameho spojenia k RSU, ku ktorej idem = podiel mojich CAM poslanych touto technologiou,
        // ktore RSU potvrdila
        int ti = socket == m_nr_beacon_socket ? xghyb::idx (xghyb::NR) : xghyb::idx (xghyb::WIFI);
        if (!xghyb::up (ti == 0 ? xghyb::WIFI : xghyb::NR))
          continue; // radio tejto technologie je vypadnute, beacon by neprisiel
        HybLink &lk = m_hyb_link[tls][ti];
        double my_tx = m_hyb_last_tx[ti];
        if (now - lk.last > 2.0)
          lk.q = 1.0; // novy kontakt s RSU: zaciname dovernou hodnotou, prva nepotvrdena CAM ju zhodi
        lk.last = now;
        double win_start = std::stod (f[2]) / 1000.0;
        if (my_tx >= win_start && my_tx < now - 0.002 && lk.eval != my_tx)
          {
            bool heard = false;
            std::stringstream ids (f[3]);
            while (std::getline (ids, item, ';'))
              if (!item.empty () && std::stoul (item) == m_sid)
                heard = true;
            lk.q = 0.8 * lk.q + 0.2 * heard;
            lk.eval = my_tx;
          }
      }
  }

  void
  trafficManagerClient80211p::StopApplication ()
  {
    NS_LOG_FUNCTION(this);

    uint64_t cam_sent;
    cam_sent = m_caService.terminateDissemination ();
    Simulator::Cancel (m_xg_tick_ev);
    Simulator::Cancel (m_xg_color_ev);
    xgstats::pathNow ().erase (GetNode ()->GetId ());
    xgstats::connNow ().erase (GetNode ()->GetId ());
    m_denService.cleanup();

    if (m_print_summary && !m_already_print)
    {
      std::cout << "INFO-" << m_id
                << ",CAM-SENT:" << cam_sent
                << ",DENM-RECEIVED:" << m_denm_received
                << std::endl;
      if (m_xg_relax || m_xg_hybrid)
        std::cout << "XGREGORM-VEH," << m_id << ",RELAXED:" << m_xg_relaxed << ",FORCED:" << m_xg_force
                  << ",BEACONS_RX:" << m_xg_beacons << std::endl;
      if (m_xg_hybrid)
        {
          // pocet CAM podla kombinacie ciest, ktorymi naozaj odisli: P1 = 802.11p, P2 = LTE, P3 = 802.11p+LTE,
          // P4 = NR, P5 = 802.11p+NR, P6 = LTE+NR, P7 = vsetky tri, P0 = ziadna (vsetky vypadnute, alebo bez priamej technologie a radic auto nepotrebuje)
          std::cout << "XGREGORM-HYB," << m_id;
          for (int m = 0; m < 8; m++)
            std::cout << ",P" << m << ":" << m_hyb_cnt[m];
          std::cout << ",SWITCH:" << m_hyb_switch << ",PROBE:" << m_hyb_probes << ",LOADSW:" << m_load_sw;
          if (m_xg_qos)
            std::cout << ",QOSSW:" << m_qos_sw << ",PREDSW:" << m_pred_sw << ",QW:" << m_qt_exp[0] << "/" << m_qt_rx[0] << "/" << m_qt_ok[0]
                      << ",QN:" << m_qt_exp[1] << "/" << m_qt_rx[1] << "/" << m_qt_ok[1];
          std::cout << std::endl;
        }
      std::cout << "XGREGORM-GAP," << m_id << ",MIN:" << (m_gap_min < 1e8 ? m_gap_min * 1000 : -1)
                << ",MAX:" << m_gap_max * 1000 << ",LT100:" << m_gap_low << ",GT1000:" << m_gap_high << std::endl;
      m_already_print=true;
    }
  }

  void
  trafficManagerClient80211p::StopApplicationNow ()
  {
    NS_LOG_FUNCTION(this);
    StopApplication ();
  }

  void
  trafficManagerClient80211p::receiveDENM (denData denm, Address from)
  {
    /* This is just a sample dummy receiveDENM function. The user can customize it to parse the content of a DENM when it is received. */
    (void) denm; // Contains the data received from the DENM
    (void) from; // Contains the address from which the DENM has been received
    std::cout<<"Received a new DENM."<<std::endl;
  }

  void
  trafficManagerClient80211p::receiveCAM (asn1cpp::Seq<CAM> cam, Address from)
  {
    /* This is just a sample dummy receiveCAM function. The user can customize it to parse the content of a CAM when it is received. */
    (void) from; // Contains the address from which the CAM has been received
    // xgregorm: skutocne PRR medzi autami (len meranie)
    unsigned long sender = asn1cpp::getField (cam->header.stationId, unsigned long);
    if (sender >= 1000000)
      m_rsu_heard[InetSocketAddress::IsMatchingType (from) ? 2 : 0] = Simulator::Now ().GetSeconds (); // CAM RSU
    if (sender < 1000000 && m_xg_qos && m_rx_mark >= 0)
      xgQosRx (sender, InetSocketAddress::IsMatchingType (from) ? 1 : 0, m_rx_mark,
               asn1cpp::getField (cam->cam.generationDeltaTime, long));
    if (sender < 1000000)
      xgstats::onVehCamRx (m_sid, sender, asn1cpp::getField (cam->cam.generationDeltaTime, long),
                           InetSocketAddress::IsMatchingType (from) ? 2 : 0); // UDP = NR-V2X
  }
}
