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
    // xgregorm 2026-11-17: dve priame technologie a vyber (twoRat)
    void handleMessage(cMessage* msg) override;
    void decide();
    bool predict(double fut[2], bool has[2], int cnt[2]);
    void remFlush();
    bool zoneDrop(int k);
    bool twoRat = false;
    std::string policy = "Q";
    int mainK = 0, curK = 0;
    int gOut2 = -1, gIn2 = -1, gCtl2 = -1;
    simtime_t lastProbe = 0, nextDecide = 0, nextRem = 0;
    bool probeDue = false;
    long long sw = 0, pxN[2] = {0, 0};
    struct RemRep { double x, y; int k; double e, ok; };
    std::vector<RemRep> remBuf;

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
