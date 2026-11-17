//
// xgregorm: sprava o polohe (CAM) pre porovnanie s ns-3. Velkost na vzduchu nastavuje aplikacia
// (setByteLength) podla CAM v ns-3; tieto polia su len pre meranie (v realite ich nesie CAM a hlavicka GeoNet).
//
import veins.modules.messages.BaseFrame1609_4;

namespace veins;

packet XgCam extends BaseFrame1609_4
{
    int sender;        // index stanice v XgStats
    int seq;           // poradove cislo CAM odosielatela
    simtime_t genTime; // cas vzniku (hodiny GNSS, v simulacii presne)
    bool isRsu;        // CAM poslala RSU
    int mark;          // znacka ako v ns-3 (pole Reserved hlavicky GeoNet): bit 7 = 802.11p, bity 0-5 = poradove cislo mod 64
}
