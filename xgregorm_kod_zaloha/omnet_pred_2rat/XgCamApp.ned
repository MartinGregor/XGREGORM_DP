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
}
