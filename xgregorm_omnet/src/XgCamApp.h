// xgregorm: aplikacia CAM pre Veins (porovnanie s ns-3), popis v XgCamApp.ned
#pragma once

#include "veins/modules/application/ieee80211p/DemoBaseApplLayer.h"
#include "veins/base/modules/BaseMobility.h"
#include "XgCam_m.h"
#include "xgregorm-qos-core.h" // spolocne jadro pravidla kvality (to iste ako v ns-3)

#include <map>

namespace veins {

class XgCamApp : public DemoBaseApplLayer {
public:
    void initialize(int stage) override;
    void finish() override;

protected:
    void onWSM(BaseFrame1609_4* wsm) override;
    void handleSelfMsg(cMessage* msg) override;
    void handlePositionUpdate(cObject* obj) override;

    void sendCam();
    double headingDeg() const;

    bool isRsu = false;
    int camBytes = 160, certBytes = 242, digestBytes = 93;
    int idx = -1; // index v XgStats
    int seq = 0;
    BaseMobility* mob = nullptr;
    cMessage* checkEvt = nullptr;
    // stav poslednej CAM (ETSI)
    simtime_t lastCam = -1, lastCert = -100, tGenCam = 1;
    int nGenCam = 0;
    // kvalita podla spolocneho jadra (len 802.11p = technologia 0): susedia, klzave sucty ocakavanych / nacas
    std::map<int, xgcore::QNeighbor> qn;
    double qExp[2] = {0, 0}, qOk[2] = {0, 0};
    simtime_t lastDecay = 0;
    Coord lastPos;
    double lastSpeed = 0, lastHeading = 0;
};

} // namespace veins
