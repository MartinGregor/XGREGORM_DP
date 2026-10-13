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
            MakeDoubleChecker<double> (0));
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
          for (int b : {xghyb::WIFI, xghyb::LTE, xghyb::NR})
            if (sent & b)
              m_hyb_last_tx[xghyb::idx (b)] = t;
          m_hyb_cnt[sent]++;
          xghyb::timeline ()[(int) t][sent]++;
        }
      xgstats::onCamTx (m_sid, p.x, p.y, gdt, sent & (xghyb::WIFI | xghyb::NR));
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
        m_caService.setSocketTx (Create<XgHybridSocket> (m_socket, lte, nr, [this] () { return xgEffectivePath (); }));
      }
    // xgregorm: algoritmus relevancie - brana pred kazdou CAM spustenou v CA sluzbe
    if (m_xg_relax)
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
    if (!m_relevant && gap < 0.95)
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
    int path;
    int probe = 0;
    bool confirmed = true;
    if (need)
      m_hyb_need_t = now;
    if (!direct)
      // CAM cez LTE dostanu len radice semaforov - kto nie je potrebny, neposiela; po opusteni prijazdu este
      // XgHybDwell s, aby radic videl, ze auto odislo (inak by ho dalej pocital na prijazde)
      path = now - m_hyb_need_t < m_hyb_dwell ? avail & xghyb::LTE : 0;
    else if (!need)
      path = (direct & xghyb::WIFI) ? xghyb::WIFI : xghyb::NR;
    else if (ok (xghyb::WIFI))
      path = xghyb::WIFI;
    else if (ok (xghyb::NR))
      {
        path = xghyb::NR;
        if ((direct & xghyb::WIFI) && now - m_hyb_probe_t >= 1.0)
          {
            probe = xghyb::WIFI; // skuska 802.11p (jeden tick = najviac jedna CAM)
            m_hyb_probe_t = now;
            m_hyb_probes++;
          }
      }
    else
      {
        path = direct | (avail & xghyb::LTE);
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
      m_hyb_switch++;
    m_hyb_base = path;
    m_hyb_path = path | probe;
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
          std::cout << ",SWITCH:" << m_hyb_switch << ",PROBE:" << m_hyb_probes << std::endl;
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
    if (sender < 1000000)
      xgstats::onVehCamRx (m_sid, sender, asn1cpp::getField (cam->cam.generationDeltaTime, long),
                           InetSocketAddress::IsMatchingType (from) ? 2 : 0); // UDP = NR-V2X
  }
}
