/* xgregorm: hybridny scenar trafficManager - kazde auto (a RSU) moze mat 802.11p, LTE aj NR-V2X a pre kazdu
 * CAM sa rozhodne, ktorou technologiou (alebo ktorymi) ide. Postavene na v2i-trafficManager-80211p.cc,
 * v2i-trafficManager-LTE.cc (EPC, server v sieti) a v2v-emergencyVehicleAlert-nrv2x.cc (sidelink) autorov VaN3Twin.
 *
 * Topologia:
 *   auto --802.11p (broadcast)--------------------------------> RSU (radic semafora svojej krizovatky), susedne auta
 *   auto --NR-V2X sidelink (multicast 225.0.0.0)--------------> RSU, susedne auta
 *   auto --LTE--> eNB --EPC (SGW/PGW)--> server v sieti --kabel (CSMA)--> vsetky RSU
 *
 * Technologie: --use-80211p / --use-lte / --use-nr (vypnuta = nepouziva sa, LTE a NR sa ani neinstaluju;
 *   802.11p zariadenie zostava, lebo GeoNet berie adresu zo zariadenia 0, ale nic nevysiela).
 * Politika: --policy all = kazda CAM na vsetkych zapnutych technologiach; adaptive = viz
 *   trafficManagerClient80211p::xgHybridDecide (priama technologia pre susedov + zaloha pre radic, ked
 *   ho RSU nepotvrdzuje). Jedna technologia v --policy all = cista referencia tej technologie.
 * Vypadok pocas behu: --fail-80211p / --fail-lte / --fail-nr "od:do" [s] (napr. "60:90", "60" = do konca).
 */
#include "ns3/carla-module.h"
#include "ns3/trafficManagerClient80211p-helper.h"
#include "ns3/trafficManagerServer80211p-helper.h"
#include "ns3/trafficManagerClient80211p.h"
#include "ns3/trafficManagerServer80211p.h"
#include "ns3/xgregorm-hybrid-socket.h"
#include "ns3/traci-module.h"
#include "ns3/internet-module.h"
#include "ns3/csma-module.h"
#include "ns3/lte-module.h"
#include "ns3/nr-module.h"
#include "ns3/antenna-module.h"
#include "ns3/buildings-module.h"
#include "ns3/propagation-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/wave-module.h"
#include "ns3/mobility-module.h"
#include "ns3/sumo_xml_parser.h"
#include "ns3/vehicle-visualizer-module.h"
#include <unistd.h>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("xgregorm-hybrid");

namespace {
// server v sieti: CAM prijate cez LTE (UDP 9) preposle po kabli vsetkym RSU (broadcast 10.10.0.255:9);
// kazda RSU si z nej vezme, len ak je auto na prijazde k jej semaforu (ako v realite geo-filter brokera)
Ptr<Socket> g_relay_out;
uint64_t g_relay_n = 0;
uint64_t g_ho[4] = {0, 0, 0, 0}; // xgregorm: LTE odovzdavanie medzi bunkami: zaciatok, OK, chyba, strata spojenia
const Ipv4Address kBackhaulNet ("10.10.0.0");
const Ipv4Mask kBackhaulMask ("255.255.255.0");

void
XgRelay (Ptr<Socket> s)
{
  Ptr<Packet> p;
  Address from;
  while ((p = s->RecvFrom (from)))
    {
      // vlastny broadcast z kabla (ak by sa vratil) neposielame znova
      if (InetSocketAddress::IsMatchingType (from) &&
          kBackhaulMask.IsMatch (InetSocketAddress::ConvertFrom (from).GetIpv4 (), kBackhaulNet))
        continue;
      g_relay_out->Send (p);
      g_relay_n++;
    }
}
} // namespace

int
main (int argc, char *argv[])
{
  std::vector<float> rate_admitted_values{3,4.5,6,9,12,18,24,27};
  std::string datarate_config;

  std::string sumo_folder = "src/automotive/examples/sumo_files_v2i_TM_map/";
  std::string mob_trace = "cars.rou.xml";
  std::string rsu_file = "stations.xml";
  std::string sumo_config ="src/automotive/examples/sumo_files_v2i_TM_map/map.sumo.cfg";

  bool verbose = true;
  bool realtime = false;
  bool sumo_gui = true;
  double sumo_updates = 0.01;
  bool print_summary = false;
  int txPower=26;
  float datarate=12;
  bool vehicle_vis = false;
  std::string sumo_netstate_file_name;
  bool send_cam = true;
  double m_baseline_prr = 150.0;
  bool m_metric_sup = true;
  double simTime = 200;
  int phase_time = 20;
  double threshold = 70;
  double tls_radius = 50;
  int sumo_seed = 10;
  bool xg_relax = false;
  double xg_relevance = 250;
  uint32_t beacon_ms = 200;
  // hybrid
  bool use_80211p = true, use_lte = true, use_nr = true;
  std::string policy = "adaptive";
  std::string fail_80211p, fail_lte, fail_nr;
  double hyb_range = 250, hyb_qmin = 0.9, hyb_dwell = 1.0;
  bool density = true;
  double core_delay_ms = 5; // PGW -> server, ako autori v v2i-trafficManager-LTE
  // NR-V2X (hodnoty z v2v-emergencyVehicleAlert-nrv2x autorov)
  double nr_freq = 5.89e9;
  uint16_t nr_bandwidth = 400;
  double nr_tx_power = 23;
  uint16_t nr_numerology = 2;
  uint8_t nr_mcs = 14;
  bool nr_sensing = false; // autori: false (nahodny vyber zdrojov bez sledovania obsadenosti)
  Time nr_bearer_time = Seconds (2.0);

  int numberOfNodes;
  uint32_t nodeCounter = 0;
  int numberOfRSUs;

  CommandLine cmd;
  xmlDocPtr rou_xml_file;

  cmd.AddValue ("realtime", "Use the realtime scheduler or not", realtime);
  cmd.AddValue ("sumo-gui", "Use SUMO gui or not", sumo_gui);
  cmd.AddValue ("sumo-updates", "SUMO granularity", sumo_updates);
  cmd.AddValue ("sumo-folder","Position of sumo config files",sumo_folder);
  cmd.AddValue ("mob-trace", "Name of the mobility trace file", mob_trace);
  cmd.AddValue ("sumo-config", "Location and name of SUMO configuration file", sumo_config);
  cmd.AddValue ("summary", "Print a summary for each vehicle at the end of the simulation", print_summary);
  cmd.AddValue ("vehicle-visualizer", "Activate the web-based vehicle visualizer for ms-van3t", vehicle_vis);
  cmd.AddValue ("send-cam", "Turn on or off the transmission of CAMs",send_cam);
  cmd.AddValue ("netstate-dump-file", "Name of the SUMO netstate-dump file", sumo_netstate_file_name);
  cmd.AddValue ("baseline", "Baseline for PRR calculation (unused, MetricSupervisor is off in hybrid)", m_baseline_prr);
  cmd.AddValue ("met-sup","Ignored: MetricSupervisor is off in the hybrid scenario",m_metric_sup);
  cmd.AddValue ("phase-time", "Duration of traffic lights phases", phase_time);
  cmd.AddValue ("threshold","Threshold multiplier for traffic light phase preemption",threshold);
  cmd.AddValue ("tls-radius","Each RSU controls only traffic lights within this radius [m]",tls_radius);
  cmd.AddValue ("sumo-seed","Random seed for SUMO",sumo_seed);
  cmd.AddValue ("xg-relax","Relevance-based CAM rate: full ETSI rate only near the next traffic light, 1 Hz elsewhere",xg_relax);
  cmd.AddValue ("xg-relevance","Relevance-based CAM rate: distance to the traffic light with full-rate CAMs [m]",xg_relevance);
  cmd.AddValue ("beacon-ms","Adaptive policy: period of the RSU feedback beacon [ms]",beacon_ms);
  cmd.AddValue ("use-80211p","Technology 802.11p available",use_80211p);
  cmd.AddValue ("use-lte","Technology LTE (eNB + core network + server) available",use_lte);
  cmd.AddValue ("use-nr","Technology NR-V2X sidelink available",use_nr);
  cmd.AddValue ("policy","CAM path policy: all (every CAM on all available technologies) or adaptive",policy);
  cmd.AddValue ("fail-80211p","802.11p outage during the run, \"from:to\" [s] (\"60\" = until the end)",fail_80211p);
  cmd.AddValue ("fail-lte","LTE outage during the run, \"from:to\" [s]",fail_lte);
  cmd.AddValue ("fail-nr","NR-V2X outage during the run, \"from:to\" [s]",fail_nr);
  cmd.AddValue ("hyb-range","Adaptive: distance to the next traffic light where its controller needs the vehicle [m]",hyb_range);
  cmd.AddValue ("hyb-qmin","Adaptive: lowest share of CAMs confirmed by the RSU to rely on one direct technology",hyb_qmin);
  cmd.AddValue ("density","Adaptive: density rule P1 - a vehicle without 802.11p neighbours sends also on NR-V2X",density);
  cmd.AddValue ("hyb-dwell","Adaptive: the direct link must be good this long before the backup is switched off [s]",hyb_dwell);
  cmd.AddValue ("core-delay","One-way delay PGW -> server [ms]",core_delay_ms);
  cmd.AddValue ("nr-tx-power","NR-V2X tx power [dBm]",nr_tx_power);
  cmd.AddValue ("nr-mcs","NR-V2X sidelink MCS",nr_mcs);
  cmd.AddValue ("nr-sensing","NR-V2X mode 2: sensing-based resource selection (authors: false)",nr_sensing);
  cmd.AddValue ("tx-power", "802.11p transmission power [dBm]", txPower);
  cmd.AddValue ("datarate", "802.11p channel data rate [Mbit/s]", datarate);
  cmd.AddValue ("sim-time", "Total duration of the simulation [s]", simTime);
  bool range_stats = false;
  std::string channel_model = "authors";
  uint32_t n_enb = 1;
  double enb_isd = 500;
  double lte_ue_power = 10;
  cmd.AddValue ("lte-ue-power","LTE UE max tx power [dBm] (ns-3 default 10; real UE power class 3 = 23)",lte_ue_power);
  cmd.AddValue ("enb","LTE macro cells: 1 = one cell in the middle of the RSUs (attach at start); k*k > 1 = square grid "
                "with inter-site distance --enb-isd, X2 handover (A3 RSRP), a vehicle attaches when it enters the map",n_enb);
  cmd.AddValue ("enb-isd","Inter-site distance of the LTE cells [m] (3GPP urban macro: 500 m)",enb_isd);
  cmd.AddValue ("channel","Radio channel of the direct technologies: authors = 802.11p LogDistance (authors' 802.11p examples) "
                "and NR 3GPP TR 37.885 V2V highway (authors' NR example); 3gpp = 802.11p on the same 3GPP V2V highway model as NR",
                channel_model);
  cmd.AddValue ("range-stats","Measure delivery vs. distance (50 m bins up to 1000 m), car-car and car-RSU",range_stats);
  cmd.Parse (argc, argv);
  xgstats::rangeOn () = range_stats;

  uint32_t paths = (use_80211p ? xghyb::WIFI : 0) | (use_lte ? xghyb::LTE : 0) | (use_nr ? xghyb::NR : 0);
  if (paths == 0)
    NS_FATAL_ERROR ("At least one technology must be available (--use-80211p / --use-lte / --use-nr)");
  if (policy != "all" && policy != "adaptive")
    NS_FATAL_ERROR ("--policy must be all or adaptive");
  bool adaptive = policy == "adaptive";
  xghyb::setOutage (xghyb::WIFI, fail_80211p);
  xghyb::setOutage (xghyb::LTE, fail_lte);
  xghyb::setOutage (xghyb::NR, fail_nr);

  if(std::find(rate_admitted_values.begin(), rate_admitted_values.end(), datarate) == rate_admitted_values.end())
    NS_FATAL_ERROR("Fatal error: invalid 802.11p data rate" << datarate << "Mbit/s.");
  datarate_config = datarate == 4.5 ? "OfdmRate4_5MbpsBW10MHz" : "OfdmRate" + std::to_string((int)datarate) + "MbpsBW10MHz";

  if (verbose)
    {
      LogComponentEnable ("CABasicService", LOG_LEVEL_INFO);
      LogComponentEnable ("DENBasicService", LOG_LEVEL_INFO);
      LogComponentEnable ("trafficManagerServer80211p", LOG_LEVEL_INFO);
      LogComponentEnable ("trafficManagerClient80211p", LOG_LEVEL_INFO);
    }

  if(realtime)
      GlobalValue::Bind ("SimulatorImplementationType", StringValue ("ns3::RealtimeSimulatorImpl"));

  // LTE: bunka s viac nez 40 UE potrebuje dlhsiu periodu SRS (inak "too many UEs")
  Config::SetDefault ("ns3::LteEnbRrc::SrsPeriodicity", UintegerValue (320));
  // NR-V2X ako u autorov
  Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue (999999999));

  std::string path = sumo_folder + mob_trace;
  xmlInitParser();
  rou_xml_file = xmlParseFile(path.c_str ());
  if (rou_xml_file == NULL)
    NS_FATAL_ERROR("Error: unable to parse the specified XML file: "<<path);
  numberOfNodes = XML_rou_count_vehicles(rou_xml_file);
  std::string rsu_path = sumo_folder + rsu_file;
  std::ifstream rsu_file_stream (rsu_path.c_str());
  std::vector<std::tuple<std::string, float, float>> rsuData = XML_poli_count_stations(rsu_file_stream);
  numberOfRSUs = rsuData.size();
  xmlFreeDoc(rou_xml_file);
  xmlCleanupParser();
  if(numberOfNodes==-1)
    NS_FATAL_ERROR("Fatal error: cannot gather the number of vehicles from the specified XML file: "<<path);

  ns3::Time simulationTime (ns3::Seconds(simTime));

  /*** 1. Uzly ***/
  NodeContainer obuNodes;
  obuNodes.Create (numberOfNodes);
  NodeContainer rsuNodes;
  rsuNodes.Create (numberOfRSUs);
  NodeContainer serverNodes;
  serverNodes.Create (1);
  Ptr<Node> server = serverNodes.Get (0);

  MobilityHelper mobility;
  mobility.Install (obuNodes);
  mobility.Install (rsuNodes);

  /*** 2. 802.11p - zariadenie 0 na autach aj RSU (aplikacie a GeoNet), instaluje sa vzdy ***/
  YansWifiPhyHelper wifiPhy;
  wifiPhy.Set ("TxPowerStart", DoubleValue (txPower));
  wifiPhy.Set ("TxPowerEnd", DoubleValue (txPower));
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default ();
  Ptr<YansWifiChannel> channel = wifiChannel.Create ();
  // xgregorm: --channel 3gpp - 802.11p na rovnakom modeli sirenia ako NR-V2X (3GPP TR 37.885 V2V highway: LOS/NLOSv
  // podla vzdialenosti, bez tienenia, stav kanala pre dvojicu sa neaktualizuje - presne ako NR u autorov), aby rozdiel
  // dosahu urcovala technologia (vykon, citlivost, modulacia), nie rozny model
  if (channel_model == "3gpp")
    {
      Ptr<ThreeGppV2vHighwayChannelConditionModel> ccm = CreateObject<ThreeGppV2vHighwayChannelConditionModel> ();
      ccm->SetAttribute ("UpdatePeriod", TimeValue (MilliSeconds (0)));
      ccm->AssignStreams (2000);
      Ptr<ThreeGppV2vHighwayPropagationLossModel> loss = CreateObject<ThreeGppV2vHighwayPropagationLossModel> ();
      loss->SetAttribute ("Frequency", DoubleValue (5.9e9));
      loss->SetAttribute ("ShadowingEnabled", BooleanValue (false));
      loss->SetChannelConditionModel (ccm);
      loss->AssignStreams (2010);
      channel = CreateObject<YansWifiChannel> ();
      channel->SetPropagationDelayModel (CreateObject<ConstantSpeedPropagationDelayModel> ());
      channel->SetPropagationLossModel (loss);
    }
  else if (channel_model != "authors")
    NS_FATAL_ERROR ("--channel must be authors or 3gpp");
  wifiPhy.SetChannel (channel);
  wifiPhy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11);
  NqosWaveMacHelper wifi80211pMac = NqosWaveMacHelper::Default ();
  Wifi80211pHelper wifi80211p = Wifi80211pHelper::Default ();
  wifi80211p.SetRemoteStationManager ("ns3::ConstantRateWifiManager", "DataMode", StringValue (datarate_config), "ControlMode", StringValue (datarate_config));
  wifi80211p.Install (wifiPhy, wifi80211pMac, obuNodes);
  wifi80211p.Install (wifiPhy, wifi80211pMac, rsuNodes);

  /*** 3. IP (InternetStackHelper pridava aj PacketSocketFactory pre 802.11p) ***/
  InternetStackHelper internet;
  internet.Install (obuNodes);
  internet.Install (rsuNodes);
  internet.Install (serverNodes);
  Ipv4StaticRoutingHelper ipv4RoutingHelper;

  /*** 4. LTE + EPC: auta = UE, jedna makro bunka v strede mesta, server za PGW ***/
  Ipv4Address serverAddr ("1.0.0.2");
  // helpery musia zit az do konca simulacie (planuju udalosti s ukazovatelom na seba)
  Ptr<LteHelper> lteHelper;
  Ptr<PointToPointEpcHelper> epcHelper;
  Ptr<NrPointToPointEpcHelper> nrEpcHelper;
  Ptr<NrHelper> nrHelper;
  Ptr<NrSlHelper> nrSlHelper;
  NetDeviceContainer lteUeDevs;
  if (use_lte)
    {
      Config::SetDefault ("ns3::LteUePhy::TxPower", DoubleValue (lte_ue_power));
      lteHelper = CreateObject<LteHelper> ();
      epcHelper = CreateObject<PointToPointEpcHelper> ();
      lteHelper->SetEpcHelper (epcHelper);
      Ptr<Node> pgw = epcHelper->GetPgwNode ();

      PointToPointHelper p2ph;
      p2ph.SetDeviceAttribute ("DataRate", DataRateValue (DataRate ("10Gb/s")));
      p2ph.SetDeviceAttribute ("Mtu", UintegerValue (1500));
      p2ph.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (core_delay_ms)));
      NetDeviceContainer coreDevs = p2ph.Install (pgw, server);
      Ipv4AddressHelper coreIp;
      coreIp.SetBase ("1.0.0.0", "255.0.0.0");
      Ipv4InterfaceContainer coreIfaces = coreIp.Assign (coreDevs);
      serverAddr = coreIfaces.GetAddress (1);
      Ptr<Ipv4StaticRouting> serverRouting = ipv4RoutingHelper.GetStaticRouting (server->GetObject<Ipv4> ());
      serverRouting->AddNetworkRouteTo (Ipv4Address ("7.0.0.0"), Ipv4Mask ("255.0.0.0"),
                                        server->GetObject<Ipv4> ()->GetInterfaceForDevice (coreDevs.Get (1)));

      // eNB v strede RSU (mesto), vyska stoziaru 30 m
      double cx = 0, cy = 0;
      for (auto &r : rsuData)
        {
          cx += std::get<1> (r) / rsuData.size ();
          cy += std::get<2> (r) / rsuData.size ();
        }
      // viac buniek (--enb k*k): stvorcova siet stoziarov okolo stredu, sused po enb_isd, odovzdavanie cez X2
      uint32_t k = (uint32_t) std::lround (std::sqrt ((double) n_enb));
      if (k < 1 || k * k != n_enb)
        NS_FATAL_ERROR ("--enb must be a square number (1, 4, 9, ...)");
      if (n_enb > 1)
        {
          lteHelper->SetHandoverAlgorithmType ("ns3::A3RsrpHandoverAlgorithm");
          lteHelper->SetHandoverAlgorithmAttribute ("Hysteresis", DoubleValue (3.0));
          lteHelper->SetHandoverAlgorithmAttribute ("TimeToTrigger", TimeValue (MilliSeconds (256)));
        }
      NodeContainer enbNodes;
      enbNodes.Create (n_enb);
      MobilityHelper enbMobility;
      Ptr<ListPositionAllocator> enbPos = CreateObject<ListPositionAllocator> ();
      for (uint32_t a = 0; a < k; a++)
        for (uint32_t b = 0; b < k; b++)
          enbPos->Add (Vector (cx + (a - (k - 1) / 2.0) * enb_isd, cy + (b - (k - 1) / 2.0) * enb_isd, 30));
      enbMobility.SetPositionAllocator (enbPos);
      enbMobility.Install (enbNodes);

      NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice (enbNodes);
      NetDeviceContainer ueDevs = lteHelper->InstallUeDevice (obuNodes);
      lteUeDevs = ueDevs;
      epcHelper->AssignUeIpv4Address (ueDevs);
      for (uint32_t u = 0; u < obuNodes.GetN (); ++u)
        {
          // LTE je jedina cesta do siete (802.11p ide cez PacketSocket/GeoNet, NR len multicast 225.0.0.0)
          Ptr<Ipv4> ipv4 = obuNodes.Get (u)->GetObject<Ipv4> ();
          ipv4RoutingHelper.GetStaticRouting (ipv4)->SetDefaultRoute (epcHelper->GetUeDefaultGatewayAddress (),
                                                                       ipv4->GetInterfaceForDevice (ueDevs.Get (u)));
        }
      if (n_enb == 1)
        lteHelper->Attach (ueDevs, enbDevs.Get (0));
      else
        {
          lteHelper->AddX2Interface (enbNodes); // UE sa pripoja az pri vstupe do mapy (vyber bunky podla polohy)
          Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/LteUeRrc/HandoverStart",
                                         MakeCallback (+[] (uint64_t, uint16_t, uint16_t, uint16_t) { g_ho[0]++; }));
          Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndOk",
                                         MakeCallback (+[] (uint64_t, uint16_t, uint16_t) { g_ho[1]++; }));
          Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndError",
                                         MakeCallback (+[] (uint64_t, uint16_t, uint16_t) { g_ho[2]++; }));
          Config::ConnectWithoutContext ("/NodeList/*/DeviceList/*/LteUeRrc/RadioLinkFailure",
                                         MakeCallback (+[] (uint64_t, uint16_t, uint16_t) { g_ho[3]++; }));
        }
    }

  /*** 5. NR-V2X sidelink (mode 2, bez gNB): auta aj RSU; nastavenie ako v2v-emergencyVehicleAlert-nrv2x ***/
  if (use_nr)
    {
      NodeContainer slNodes;
      slNodes.Add (obuNodes);
      slNodes.Add (rsuNodes);

      // NR sidelink potrebuje vlastny NrPointToPointEpcHelper (LTE UE neprijme) a oba EPC helpery maju
      // natvrdo rovnake interne adresy (PGW 7.0.0.1, S5/S11 ...). Su to dve oddelene siete (NR jadro sa
      // pre sidelink nepouziva), preto vypneme len globalnu kontrolu kolizii adries.
      if (use_lte)
        Ipv4AddressGenerator::TestMode ();
      nrEpcHelper = CreateObject<NrPointToPointEpcHelper> ();
      nrHelper = CreateObject<NrHelper> ();
      nrHelper->SetEpcHelper (nrEpcHelper);

      CcBwpCreator ccBwpCreator;
      CcBwpCreator::SimpleOperationBandConf bandConfSl (nr_freq, nr_bandwidth, 1, BandwidthPartInfo::V2V_Highway);
      OperationBandInfo bandSl = ccBwpCreator.CreateOperationBandContiguousCc (bandConfSl);
      Config::SetDefault ("ns3::ThreeGppChannelModel::UpdatePeriod", TimeValue (MilliSeconds (0)));
      nrHelper->SetChannelConditionModelAttribute ("UpdatePeriod", TimeValue (MilliSeconds (0)));
      nrHelper->SetPathlossAttribute ("ShadowingEnabled", BooleanValue (false));
      nrHelper->InitializeOperationBand (&bandSl);
      BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps ({bandSl});

      nrHelper->SetUeAntennaAttribute ("NumRows", UintegerValue (1));
      nrHelper->SetUeAntennaAttribute ("NumColumns", UintegerValue (2));
      nrHelper->SetUeAntennaAttribute ("AntennaElement", PointerValue (CreateObject<IsotropicAntennaModel> ()));
      nrHelper->SetUePhyAttribute ("TxPower", DoubleValue (nr_tx_power));
      nrHelper->SetUeMacAttribute ("EnableSensing", BooleanValue (nr_sensing));
      nrHelper->SetUeMacAttribute ("T1", UintegerValue (2));
      nrHelper->SetUeMacAttribute ("T2", UintegerValue (81));
      nrHelper->SetUeMacAttribute ("ActivePoolId", UintegerValue (0));
      nrHelper->SetUeMacAttribute ("ReservationPeriod", TimeValue (MilliSeconds (20)));
      nrHelper->SetUeMacAttribute ("NumSidelinkProcess", UintegerValue (4));
      nrHelper->SetUeMacAttribute ("EnableBlindReTx", BooleanValue (true));
      nrHelper->SetUeMacAttribute ("SlThresPsschRsrp", IntegerValue (-128));
      uint8_t bwpId = 0;
      nrHelper->SetBwpManagerTypeId (TypeId::LookupByName ("ns3::NrSlBwpManagerUe"));
      nrHelper->SetUeBwpManagerAlgorithmAttribute ("GBR_MC_PUSH_TO_TALK", UintegerValue (bwpId));
      std::set<uint8_t> bwpIds = {bwpId};

      NetDeviceContainer slDevs = nrHelper->InstallUeDevice (slNodes, allBwps);
      for (auto it = slDevs.Begin (); it != slDevs.End (); ++it)
        DynamicCast<NrUeNetDevice> (*it)->UpdateConfig ();

      nrSlHelper = CreateObject<NrSlHelper> ();
      nrSlHelper->SetEpcHelper (nrEpcHelper);
      nrSlHelper->SetSlErrorModel ("ns3::NrLteMiErrorModel");
      nrSlHelper->SetUeSlAmcAttribute ("AmcModel", EnumValue (NrAmc::ErrorModel));
      nrSlHelper->SetNrSlSchedulerTypeId (NrSlUeMacSchedulerSimple::GetTypeId ());
      nrSlHelper->SetUeSlSchedulerAttribute ("FixNrSlMcs", BooleanValue (true));
      nrSlHelper->SetUeSlSchedulerAttribute ("InitialNrSlMcs", UintegerValue (nr_mcs));
      nrSlHelper->PrepareUeForSidelink (slDevs, bwpIds);

      Ptr<NrSlCommPreconfigResourcePoolFactory> ptrFactory = Create<NrSlCommPreconfigResourcePoolFactory> ();
      std::vector<std::bitset<1>> slBitMap (10, std::bitset<1> (1));
      ptrFactory->SetSlTimeResources (slBitMap);
      ptrFactory->SetSlSensingWindow (100);
      ptrFactory->SetSlSelectionWindow (5);
      ptrFactory->SetSlFreqResourcePscch (10);
      ptrFactory->SetSlSubchannelSize (10);
      ptrFactory->SetSlMaxNumPerReserve (3);
      LteRrcSap::SlResourcePoolConfigNr poolConfig;
      poolConfig.haveSlResourcePoolConfigNr = true;
      LteRrcSap::SlResourcePoolIdNr poolId;
      poolId.id = 0;
      poolConfig.slResourcePoolId = poolId;
      poolConfig.slResourcePool = ptrFactory->CreatePool ();
      LteRrcSap::SlBwpPoolConfigCommonNr bwpPoolConfig;
      bwpPoolConfig.slTxPoolSelectedNormal[poolId.id] = poolConfig;
      LteRrcSap::Bwp bwp;
      bwp.numerology = nr_numerology;
      bwp.symbolsPerSlots = 14;
      bwp.rbPerRbg = 1;
      bwp.bandwidth = nr_bandwidth;
      LteRrcSap::SlBwpGeneric bwpGeneric;
      bwpGeneric.bwp = bwp;
      bwpGeneric.slLengthSymbols = LteRrcSap::GetSlLengthSymbolsEnum (14);
      bwpGeneric.slStartSymbol = LteRrcSap::GetSlStartSymbolEnum (0);
      LteRrcSap::SlBwpConfigCommonNr bwpConfig;
      bwpConfig.haveSlBwpGeneric = true;
      bwpConfig.slBwpGeneric = bwpGeneric;
      bwpConfig.haveSlBwpPoolConfigCommonNr = true;
      bwpConfig.slBwpPoolConfigCommonNr = bwpPoolConfig;
      LteRrcSap::SlFreqConfigCommonNr freqConfig;
      for (uint8_t id : bwpIds)
        freqConfig.slBwpList[id] = bwpConfig;
      LteRrcSap::TddUlDlConfigCommon tdd;
      tdd.tddPattern = "UL|UL|UL|UL|UL|UL|UL|UL|UL|UL|";
      LteRrcSap::SlPreconfigGeneralNr general;
      general.slTddConfig = tdd;
      LteRrcSap::SlUeSelectedConfig ueSelected;
      ueSelected.slProbResourceKeep = 0.0;
      LteRrcSap::SlPsschTxParameters psschParams;
      psschParams.slMaxTxTransNumPssch = 5;
      LteRrcSap::SlPsschTxConfigList psschList;
      psschList.slPsschTxParameters[0] = psschParams;
      ueSelected.slPsschTxConfigList = psschList;
      LteRrcSap::SidelinkPreconfigNr preconfig;
      preconfig.slPreconfigGeneral = general;
      preconfig.slUeSelectedPreConfig = ueSelected;
      preconfig.slPreconfigFreqInfoList[0] = freqConfig;
      nrSlHelper->InstallNrSlPreConfiguration (slDevs, preconfig);

      int64_t stream = 1000;
      stream += nrHelper->AssignStreams (slDevs, stream);
      stream += nrSlHelper->AssignStreams (slDevs, stream);

      // IP na NR rozhrani vlastna podsiet (LTE EPC uz pouziva 7.0.0.0/8); multicast 225.0.0.0 cez NR
      Ipv4AddressHelper nrIp;
      nrIp.SetBase ("8.0.0.0", "255.0.0.0");
      nrIp.Assign (slDevs);
      for (uint32_t n = 0; n < slNodes.GetN (); ++n)
        {
          Ptr<Ipv4> ipv4 = slNodes.Get (n)->GetObject<Ipv4> ();
          ipv4RoutingHelper.GetStaticRouting (ipv4)->AddNetworkRouteTo (Ipv4Address ("225.0.0.0"), Ipv4Mask ("255.0.0.0"),
                                                                         ipv4->GetInterfaceForDevice (slDevs.Get (n)));
        }
      Ipv4Address group ("225.0.0.0");
      uint32_t dstL2Id = 255;
      nrSlHelper->ActivateNrSlBearer (nr_bearer_time, slDevs,
                                      Create<LteSlTft> (LteSlTft::Direction::TRANSMIT, LteSlTft::CommType::GroupCast, group, dstL2Id));
      nrSlHelper->ActivateNrSlBearer (nr_bearer_time, slDevs,
                                      Create<LteSlTft> (LteSlTft::Direction::RECEIVE, LteSlTft::CommType::GroupCast, group, dstL2Id));
    }

  /*** 6. Kabel: server + vsetky RSU na jednom segmente (mestska siet riadenia dopravy) ***/
  NodeContainer backhaulNodes;
  backhaulNodes.Add (server);
  backhaulNodes.Add (rsuNodes);
  CsmaHelper csma;
  csma.SetChannelAttribute ("DataRate", StringValue ("100Mbps"));
  csma.SetChannelAttribute ("Delay", TimeValue (MilliSeconds (1)));
  NetDeviceContainer backhaulDevs = csma.Install (backhaulNodes);
  Ipv4AddressHelper backhaulIp;
  backhaulIp.SetBase ("10.10.0.0", "255.255.255.0");
  backhaulIp.Assign (backhaulDevs);

  // server: prijem CAM z LTE a preposlanie RSU
  Ptr<Socket> relayIn = Socket::CreateSocket (server, UdpSocketFactory::GetTypeId ());
  relayIn->Bind (InetSocketAddress (Ipv4Address::GetAny (), 9));
  relayIn->SetRecvCallback (MakeCallback (&XgRelay));
  g_relay_out = Socket::CreateSocket (server, UdpSocketFactory::GetTypeId ());
  g_relay_out->Bind ();
  g_relay_out->SetAllowBroadcast (true);
  g_relay_out->Connect (InetSocketAddress (Ipv4Address ("10.10.0.255"), 9));

  /*** 7. SUMO ***/
  Ptr<TraciClient> sumoClient = CreateObject<TraciClient> ();
  sumoClient->SetAttribute ("SumoConfigPath", StringValue (sumo_config));
  sumoClient->SetAttribute ("SumoBinaryPath", StringValue (""));
  sumoClient->SetAttribute ("SynchInterval", TimeValue (Seconds (sumo_updates)));
  sumoClient->SetAttribute ("StartTime", TimeValue (Seconds (0.0)));
  sumoClient->SetAttribute ("SumoGUI", (BooleanValue) sumo_gui);
  sumoClient->SetAttribute ("SumoPort", UintegerValue (3400));
  sumoClient->SetAttribute ("PenetrationRate", DoubleValue (1.0));
  sumoClient->SetAttribute ("SumoLogFile", BooleanValue (false));
  sumoClient->SetAttribute ("SumoStepLog", BooleanValue (false));
  sumoClient->SetAttribute ("SumoSeed", IntegerValue (sumo_seed));
  std::string sumo_additional_options = "--verbose true";
  if(sumo_netstate_file_name!="")
    sumo_additional_options += " --netstate-dump " + sumo_netstate_file_name;
  sumo_additional_options += " --collision.action warn --collision.check-junctions --error-log=sumo-errors-or-collisions.xml";
  sumoClient->SetAttribute ("SumoWaitForSocket", TimeValue (Seconds (1.0)));
  sumoClient->SetAttribute ("SumoAdditionalCmdOptions", StringValue (sumo_additional_options));

  vehicleVisualizer vehicleVisObj;
  Ptr<vehicleVisualizer> vehicleVis = &vehicleVisObj;
  if (vehicle_vis)
  {
      vehicleVis->startServer();
      vehicleVis->connectToServer ();
      sumoClient->SetAttribute ("VehicleVisualizer", PointerValue (vehicleVis));
  }

  // MetricSupervisor meria len GeoNet: CAM poslanu inou technologiou nez 802.11p povazuje za nepocuty
  // broadcast a CAM poslanu viacerymi cestami za viac prijmov toho isteho paketu (PRR > 1 -> NS_FATAL_ERROR).
  // V hybride sa preto nepouziva; meria sa XGREGORM-V2V / -AW / -HYB (xgregorm-stats.h).
  Ptr<MetricSupervisor> metSup = NULL;

  /*** 8. RSU = radic semafora svojej krizovatky ***/
  trafficManagerServer80211pHelper serverHelper;
  serverHelper.SetAttribute ("Client", (PointerValue) sumoClient);
  serverHelper.SetAttribute ("RealTime", BooleanValue(realtime));
  serverHelper.SetAttribute ("PhaseTime", UintegerValue(phase_time));
  serverHelper.SetAttribute ("Threshold", DoubleValue(threshold));
  serverHelper.SetAttribute ("TlsRadius", DoubleValue(tls_radius));
  serverHelper.SetAttribute ("MetricSupervisor", PointerValue (metSup));
  serverHelper.SetAttribute ("SendCAM", BooleanValue (use_80211p)); // CAM samotnej RSU idu len cez 802.11p
  serverHelper.SetAttribute ("XgBeaconMs", UintegerValue (beacon_ms));
  // beacon so spatnou vazbou len pre adaptivnu politiku (inak by len zatazoval kanal)
  serverHelper.SetAttribute ("XgHybrid", UintegerValue (adaptive ? 2 : 1));
  serverHelper.SetAttribute ("XgNr", BooleanValue (use_nr));
  int i = 0;
  for (auto rsu : rsuData)
    {
      Ptr<Node> rsuNode = rsuNodes.Get (i++);
      sumoClient->AddStation (std::get<0> (rsu), std::get<1> (rsu), std::get<2> (rsu), 0.0, rsuNode);
      ApplicationContainer app = serverHelper.Install (rsuNode);
      app.Start (Seconds (0.0));
      app.Stop (simulationTime - Seconds (0.1));
    }

  /*** 9. Auta ***/
  trafficManagerClient80211pHelper clientHelper;
  clientHelper.SetAttribute ("ServerAddr", Ipv4AddressValue (serverAddr));
  clientHelper.SetAttribute ("Client", (PointerValue) sumoClient);
  clientHelper.SetAttribute ("PrintSummary", BooleanValue(print_summary));
  clientHelper.SetAttribute ("RealTime", BooleanValue(realtime));
  clientHelper.SetAttribute ("SendCAM", BooleanValue (send_cam));
  clientHelper.SetAttribute ("MetricSupervisor", PointerValue (metSup));
  clientHelper.SetAttribute ("XgRelax", BooleanValue (xg_relax));
  clientHelper.SetAttribute ("XgRelevance", DoubleValue (xg_relevance));
  clientHelper.SetAttribute ("XgHybrid", UintegerValue (adaptive ? 2 : 1));
  clientHelper.SetAttribute ("XgPaths", UintegerValue (paths));
  clientHelper.SetAttribute ("XgHybRange", DoubleValue (hyb_range));
  clientHelper.SetAttribute ("XgHybQmin", DoubleValue (hyb_qmin));
  clientHelper.SetAttribute ("XgHybDwell", DoubleValue (hyb_dwell));
  clientHelper.SetAttribute ("XgDensity", BooleanValue (density));

  STARTUP_FCN setupNewNode = [&] (std::string vehicleID,TraciClient::StationTypeTraCI_t stationType) -> Ptr<Node>
    {
      if (nodeCounter >= obuNodes.GetN())
        NS_FATAL_ERROR("Node Pool empty!: " << nodeCounter << " nodes created.");
      Ptr<Node> includedNode = obuNodes.Get(nodeCounter);
      // xgregorm: viac LTE buniek - UE sa pripoji, az ked ma auto polohu (vyber bunky v idle mode)
      if (use_lte && n_enb > 1)
        {
          Ptr<NetDevice> ue = lteUeDevs.Get (nodeCounter);
          Simulator::Schedule (MilliSeconds (10), [lteHelper, ue] () { lteHelper->Attach (ue); });
        }
      ++nodeCounter;
      ApplicationContainer ClientApp = clientHelper.Install (includedNode);
      ClientApp.Start (Seconds (0.0));
      ClientApp.Stop (simulationTime - Simulator::Now () - Seconds (0.1));
      return includedNode;
    };

  SHUTDOWN_FCN shutdownNode = [] (Ptr<Node> exNode,std::string vehicleID)
    {
      Ptr<trafficManagerClient80211p> app = exNode->GetApplication(0)->GetObject<trafficManagerClient80211p>();
      if(app)
        app->StopApplicationNow ();
      Ptr<ConstantPositionMobilityModel> mob = exNode->GetObject<ConstantPositionMobilityModel>();
      mob->SetPosition(Vector(-1000.0+(rand()%25),320.0+(rand()%25),250.0));
    };

  sumoClient->SumoSetup (setupNewNode, shutdownNode);

  Simulator::Stop (simulationTime);
  Simulator::Run ();
  Simulator::Destroy ();

  std::cout << "XGREGORM-RELAY,CAM:" << g_relay_n << std::endl;
  std::cout << "XGREGORM-LTEHO,START:" << g_ho[0] << ",OK:" << g_ho[1] << ",ERR:" << g_ho[2] << ",RLF:" << g_ho[3] << std::endl;
  // casova os po 1 s: T<sekunda>:pocet CAM pre masku 0..7 (oddelene /)
  std::cout << "XGREGORM-HYBT";
  for (auto &e : xghyb::timeline ())
    {
      std::cout << ",T" << e.first << ":";
      for (int m = 0; m < 8; m++)
        std::cout << (m ? "/" : "") << e.second[m];
    }
  std::cout << std::endl;
  // casova os dorucenia po 1 s (xgstats::timeline): T<s>:ul_exp/ul_rx/v2v_exp/v2v_rx/vzorky/nevie/vek_ms/chyba>10m
  std::cout << "XGREGORM-LOSST";
  for (auto &e : xgstats::timeline ())
    {
      std::cout << ",T" << e.first << ":";
      for (int k = 0; k < 8; k++)
        std::cout << (k ? "/" : "") << e.second[k];
    }
  std::cout << std::endl;
  // oneskorenie auto -> auto: W = 802.11p, NR = NR-V2X (prva kopia kazdej CAM)
  std::cout << "XGREGORM-V2VLAT,W_SUM:" << xgstats::v2vLatSum ()[0] << ",W_N:" << xgstats::v2vLatN ()[0]
            << ",NR_SUM:" << xgstats::v2vLatSum ()[2] << ",NR_N:" << xgstats::v2vLatN ()[2] << std::endl;
  // hustota po 1 s: T<s>:vzorky/sum_cbr_e4/max_cbr_e4/sum_nb_w/sum_nb_nr/izolovane/cbr>0.3/cbr>0.6
  std::cout << "XGREGORM-DENS";
  for (auto &e : xgstats::densT ())
    {
      std::cout << ",T" << e.first << ":";
      for (int k = 0; k < 8; k++)
        std::cout << (k ? "/" : "") << e.second[k];
    }
  std::cout << std::endl;
  // dorucenie podla vzdialenosti (po 50 m): B<index>:v2v_exp/v2v_rx/ul_exp/ul_rx
  if (range_stats)
    {
      std::cout << "XGREGORM-RANGE";
      for (int b = 0; b < xgstats::kBins; b++)
        std::cout << ",B" << b << ":" << xgstats::rangeBins ()[0][b] << "/" << xgstats::rangeBins ()[1][b] << "/"
                  << xgstats::rangeBins ()[2][b] << "/" << xgstats::rangeBins ()[3][b];
      std::cout << std::endl;
    }
  return 0;
}
