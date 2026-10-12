#include "ns3/random-variable-stream.h" // xgregorm
#include "trafficManagerClient80211p.h"

#include "ns3/CAM.h"
#include "ns3/DENM.h"
#include "ns3/vdpTraci.h"
#include "ns3/socket.h"
#include "ns3/network-module.h"
#include "ns3/gn-utils.h" // Needed for GN_ETHERTYPE
#include "ns3/xgregorm-stats.h" // xgregorm
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
        // xgregorm: moje algoritmy CAM
        .AddAttribute ("XgSpat",
            "Algorithm 1: hold CAMs while standing at a red light announced by the RSU (SPaT)",
            BooleanValue (false),
            MakeBooleanAccessor (&trafficManagerClient80211p::m_xg_spat),
            MakeBooleanChecker ())
        .AddAttribute ("XgAck",
            "Algorithm 2: retransmit a CAM that the RSU's beacon does not list as heard",
            BooleanValue (false),
            MakeBooleanAccessor (&trafficManagerClient80211p::m_xg_ack),
            MakeBooleanChecker ())
        .AddAttribute ("XgRelax",
            "Algorithm 3: outside the area announced by an RSU send only the 1 Hz time-triggered CAMs",
            BooleanValue (false),
            MakeBooleanAccessor (&trafficManagerClient80211p::m_xg_relax),
            MakeBooleanChecker ())
        .AddAttribute ("XgHoldMax",
            "Algorithm 1: longest time [s] without any CAM while holding at a red light (ETSI T_GenCamMax = 1 s)",
            DoubleValue (1),
            MakeDoubleAccessor (&trafficManagerClient80211p::m_xg_hold_max),
            MakeDoubleChecker<double> (0.1, 1)); // xgregorm: ETSI EN 302 637-2 nedovoli dlhsie nez 1 s
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

    // xgregorm: kazdu odoslanu CAM zaznamename (merania + cakanie na potvrdenie od RSU)
    m_sid = std::stol (m_id.substr (3));
    m_caService.addCamTxCallback ([this] () {
      libsumo::TraCIPosition p = m_client->TraCIAPI::vehicle.getPosition (m_id);
      m_x = p.x;
      m_y = p.y;
      xgstats::onCamTx (m_sid, p.x, p.y);
      double t = Simulator::Now ().GetSeconds ();
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
      m_ack_wait = true;
      m_last_was_retx = m_retx_now;
    });
    if (m_xg_spat || m_xg_ack || m_xg_relax)
      {
        m_caService.setCamGate (std::bind (&trafficManagerClient80211p::camGate, this, std::placeholders::_1));
        m_xg_socket = Socket::CreateSocket (GetNode (), tid);
        PacketSocketAddress xlocal;
        xlocal.SetSingleDevice (GetNode ()->GetDevice (0)->GetIfIndex ());
        xlocal.SetPhysicalAddress (GetNode ()->GetDevice (0)->GetAddress ());
        xlocal.SetProtocol (XG_ETHERTYPE);
        if (m_xg_socket->Bind (xlocal) == -1)
          NS_FATAL_ERROR ("Failed to bind the xgregorm beacon socket");
        m_xg_socket->SetRecvCallback (MakeCallback (&trafficManagerClient80211p::xgReceiveBeacon, this));
        if (m_xg_ack)
          {
            m_xg_jitter = CreateObject<UniformRandomVariable> ();
            m_xg_jitter->SetAttribute ("Min", DoubleValue (0.0));
            m_xg_jitter->SetAttribute ("Max", DoubleValue (0.02));
          }
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
      if (m_xg_socket)
        m_xg_tick_ev = Simulator::Schedule (Seconds (desync), &trafficManagerClient80211p::xgTick, this);
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
    // algoritmus 3: auto je relevantne, ak je na trase k semaforu, ktoreho RSU nedavno vysielala beacon,
    // a je k nemu blizsie nez oznamena vzdialenost (RSU potrebuje plnu frekvenciu CAM len na prijazde)
    auto it = m_rel_heard.find (m_next_tls);
    return it != m_rel_heard.end () && Simulator::Now ().GetSeconds () - it->second.first < 2.5 &&
           m_next_dist < it->second.second;
  }

  bool
  trafficManagerClient80211p::camGate (bool dynamic)
  {
    // ETSI EN 302 637-2: odstup CAM je vzdy medzi T_GenCamMin = 100 ms a T_GenCamMax = 1000 ms.
    // Algoritmy 1 a 3 len predlzia minimalny odstup dynamicky spustenych CAM (ako DCC: T_GenCam_DCC
    // <= T_GenCamMax), casovo spustena CAM po T_GenCam ide vzdy.
    double gap = Simulator::Now ().GetSeconds () - m_last_tx;
    if (gap < 0.1 - 1e-6)
      return false; // tesne po vynutenej CAM (forceCam), dalsia este nesmie ist
    // algoritmus 1: stojim pri cervenej (podla SPaT z RSU) -> RSU o mne vie a kym je cervena, nepohnem sa
    if (m_xg_spat && m_red && m_red_tls == m_next_tls && Simulator::Now ().GetSeconds () - m_red_time < 2.5 &&
        m_next_dist < 100 && gap < m_xg_hold_max - 0.05 && m_client->TraCIAPI::vehicle.getSpeed (m_id) < 0.5)
      {
        xgCount (m_xg_hold);
        return false;
      }
    // algoritmus 3: mimo relevantnej oblasti najviac 1 CAM za sekundu
    if (m_xg_relax && !m_relevant && gap < 0.95)
      {
        xgCount (m_xg_relaxed);
        return false;
      }
    return true;
  }

  void
  trafficManagerClient80211p::xgTick ()
  {
    // najblizsi semafor na trase a vzdialenost k nemu (staticka mapa, v realite MAPEM / digitalna mapa)
    std::vector<libsumo::TraCINextTLSData> nt = m_client->TraCIAPI::vehicle.getNextTLS (m_id);
    if (nt.empty ())
      {
        m_next_tls = "";
        m_next_idx = -1;
        m_next_dist = 1e9;
      }
    else
      {
        m_next_tls = nt[0].id;
        m_next_idx = nt[0].tlIndex;
        m_next_dist = nt[0].dist;
      }
    if (m_xg_ack)
      {
        libsumo::TraCIPosition p = m_client->TraCIAPI::vehicle.getPosition (m_id);
        m_x = p.x;
        m_y = p.y;
      }
    if (m_xg_relax)
      {
        bool rel = xgRelevant ();
        if (rel != m_relevant)
          {
            // vstup do oblasti RSU aj odchod z nej oznamime hned, aby RSU mala spravny stav
            // (ak CAM odisla pred menej nez 100 ms, RSU ma cerstvy stav a dalsia CAM by porusila T_GenCamMin)
            m_relevant = rel;
            if (Simulator::Now ().GetSeconds () - m_last_tx >= 0.1 - 1e-6)
              {
                m_xg_force++;
                m_caService.forceCam ();
              }
          }
      }
    m_xg_tick_ev = Simulator::Schedule (MilliSeconds (100), &trafficManagerClient80211p::xgTick, this);
  }

  void
  trafficManagerClient80211p::xgReceiveBeacon (Ptr<Socket> socket)
  {
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
        if (f.size () < 8 || f[0] != "XG")
          continue;
        if (f.size () == 8)
          f.push_back ("");
        m_xg_beacons++;
        double now = Simulator::Now ().GetSeconds ();
        double rx = std::stod (f[1]), ry = std::stod (f[2]);
        std::string tls = f[3];
        if (tls.empty ())
          continue;
        // algoritmus 3: RSU oznamuje, kde potrebuje plnu frekvenciu CAM
        if (std::stod (f[5]) > 0)
          m_rel_heard[tls] = {now, std::stod (f[5])};
        if (tls != m_next_tls)
          continue; // beacon inej krizovatky, nez ku ktorej idem
        // algoritmus 1: farba pre moj signal (index z mapy)
        if (f[4] != "-" && m_next_idx >= 0 && m_next_idx < (int) f[4].size ())
          {
            char c = f[4][m_next_idx];
            m_red = (c == 'r' || c == 'R' || c == 'y' || c == 'Y');
            m_red_tls = tls;
            m_red_time = now;
          }
        // algoritmus 2: potvrdenie mojej poslednej CAM od RSU krizovatky, ku ktorej idem
        if (f[7] == "1" && m_ack_wait && std::hypot (m_x - rx, m_y - ry) < 150)
          {
            double win_start = std::stod (f[6]) / 1000.0;
            if (m_last_tx >= win_start && m_last_tx < now - 0.002)
              {
                bool heard = false;
                std::stringstream ids (f[8]);
                while (std::getline (ids, item, ';'))
                  if (!item.empty () && std::stoul (item) == m_sid)
                    heard = true;
                m_ack_wait = false;
                if (!heard && !m_last_was_retx)
                  {
                    // najskor T_GenCamMin = 100 ms po nepotvrdenej CAM (ETSI EN 302 637-2)
                    double delay = std::max (m_xg_jitter->GetValue (), m_last_tx + 0.1 - now);
                    m_retx_for = m_last_tx;
                    Simulator::Schedule (Seconds (delay), &trafficManagerClient80211p::xgRetransmit, this);
                  }
              }
          }
      }
  }

  void
  trafficManagerClient80211p::xgRetransmit ()
  {
    // algoritmus 2: RSU moju CAM nepocula -> poslem aktualnu CAM (najviac raz za povodnu CAM),
    // ak medzitym neodisla nova CAM
    if (m_last_tx == m_retx_for)
      {
        m_retx_now = true;
        m_xg_retx++;
        m_caService.forceCam ();
        m_retx_now = false;
      }
  }

  void
  trafficManagerClient80211p::StopApplication ()
  {
    NS_LOG_FUNCTION(this);

    uint64_t cam_sent;
    cam_sent = m_caService.terminateDissemination ();
    Simulator::Cancel (m_xg_tick_ev);
    m_denService.cleanup();

    if (m_print_summary && !m_already_print)
    {
      std::cout << "INFO-" << m_id
                << ",CAM-SENT:" << cam_sent
                << ",DENM-RECEIVED:" << m_denm_received
                << std::endl;
      if (m_xg_socket)
        std::cout << "XGREGORM-VEH," << m_id << ",HOLD:" << m_xg_hold << ",RELAXED:" << m_xg_relaxed
                  << ",RETX:" << m_xg_retx << ",FORCED:" << m_xg_force << ",BEACONS_RX:" << m_xg_beacons << std::endl;
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
    if (sender < 1000000)
      xgstats::onVehCamRx (m_sid, sender);
  }
}
