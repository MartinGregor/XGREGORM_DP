package xgregorm;

import org.car2x.veins.modules.application.ieee80211p.DemoBaseApplLayer;

// xgregorm: CAM podla ETSI EN 302 637-2 (kontrola kazdych 100 ms: zmena polohy > 4 m, smeru > 4 stupne,
// rychlosti > 0.5 m/s, alebo uplynul T_GenCam; T_GenCam = odstup poslednej dynamickej CAM pre dalsie 3 CAM,
// inak 1 s; RSU 1 CAM za sekundu). Velkost ramca = camBytes + podpis (certifikat 1x za s, inak digest), ako
// --security 1 v ns-3. Meria dorucenie podla vzdialenosti (auto-auto, auto-RSU) v rovnakom formate ako ns-3.
simple XgCamApp extends DemoBaseApplLayer
{
    parameters:
        @class(veins::XgCamApp);
        bool isRsu = default(false);
        int camBytes = default(160);       // ramec bez podpisu [B] (MAC + LLC + GeoNet/BTP + CAM)
        int certBytes = default(242);      // podpis s certifikatom (1x za sekundu)
        int digestBytes = default(93);     // podpis s digestom
        double warmup @unit(s) = default(0s);   // statistiky az od tohto casu (ako --warmup v ns-3)
        double maxRange @unit(m) = default(1000m);
        // xgregorm 2026-11-17: dve priame technologie (XgCar) a vyber technologie ako v ns-3 (--clean 1)
        bool twoRat = default(false);
        string policy = default("Q");      // FW = len technologia 0, MIX = nahodna pevna, Q = kvalita, PM = mapa, PX = vylepsena
        double probeHz = default(0.5);     // skusky (CAM oboma) pri Q, PM, PX
        double qosMargin = default(0.05);
        double predHorizon @unit(s) = default(3s);
        double remCell @unit(m) = default(20m);
        double remTau @unit(s) = default(30s);
        double remPeriod @unit(s) = default(1s);
        double pxDev = default(0.15);
        string zones = default("");        // ako --zones v ns-3: rsu:polomer:t0:t1:W|N:strata+...
    gates:
        input lowerLayerIn2 @loose;
        output lowerLayerOut2 @loose;
        input lowerControlIn2 @loose;
}
