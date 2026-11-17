// xgregorm: aplikacia CAM pre Veins a meranie dorucenia v rovnakom formate ako xgregorm-hybrid (ns-3).
//
// Meranie (rovnaka definicia ako --range-stats a XGREGORM-V2V v ns-3):
//  - pri kazdej CAM auta (od casu warmup) sa zapamata, ktore stanice su do maxRange a v ktorom kosi vzdialenosti
//    (po 50 m, poloha v okamihu vyslania); auto-auto = v2v, auto-RSU = ul,
//  - prijem: prva kopia CAM u danej stanice sa zapocita do jej kosa,
//  - XGREGORM-V2V = auto-auto do 100 m (kose 0 a 1), XGREGORM-V2VLAT = sucet oneskoreni [s] tychto prijmov.

#include "XgCamApp.h"

#include <cmath>
#include <deque>
#include <map>
#include <unordered_map>
#include <vector>

#include "veins/modules/mobility/traci/TraCIMobility.h"

namespace veins {

Define_Module(veins::XgCamApp);

namespace {

constexpr int kBins = 20;     // ako xgstats::kBins v ns-3
constexpr double kBin = 50.0; // [m]

struct Station {
    BaseMobility* mob = nullptr;
    bool rsu = false;
    bool active = false;
};

struct TxRec {
    simtime_t t;
    std::unordered_map<int, int> pend; // prijimac -> kos
};

struct Stats {
    std::vector<Station> st;
    std::map<std::pair<int, int>, TxRec> tx;
    std::deque<std::pair<simtime_t, std::pair<int, int>>> order;
    long long bins[4][kBins] = {};
    double latSum = 0;
    double qExp = 0, qRx = 0, qOk = 0; // odhad kvality v autach (jadro), od warmup
    long long latN = 0, camTx = 0, rsuTx = 0, frames = 0, bytes = 0;
    bool printed = false;
};

Stats& S()
{
    static Stats s;
    return s;
}

} // namespace

void XgCamApp::initialize(int stage)
{
    DemoBaseApplLayer::initialize(stage);
    if (stage == 0) {
        isRsu = par("isRsu").boolValue();
        camBytes = par("camBytes");
        certBytes = par("certBytes");
        digestBytes = par("digestBytes");
        mob = FindModule<BaseMobility*>::findSubModule(getParentModule());
        ASSERT(mob);
        idx = S().st.size();
        S().st.push_back({mob, isRsu, true});
        checkEvt = new cMessage("xg cam check");
    }
    else if (stage == 1) {
        // nahodny posun ako desynchronizacia v ns-3 (auta: kontrola kazdych 100 ms, RSU: CAM kazdu 1 s)
        scheduleAt(simTime() + uniform(0, isRsu ? 1.0 : 0.1), checkEvt);
    }
}

void XgCamApp::finish()
{
    DemoBaseApplLayer::finish();
    if (idx >= 0)
        S().st[idx].active = false;
    cancelAndDelete(checkEvt);
    checkEvt = nullptr;
    Stats& s = S();
    // vypis len pri konci simulacie (finish sa vola aj pri odstraneni auta zo SUMO pocas behu)
    if (s.printed || getSimulation()->getSimulationStage() != CTX_FINISH)
        return;
    s.printed = true;
    long long e = s.bins[0][0] + s.bins[0][1], r = s.bins[1][0] + s.bins[1][1];
    std::cout << "XGREGORM-V2V,EXP:" << e << ",RX:" << r << std::endl;
    std::cout << "XGREGORM-V2VLAT,W_SUM:" << s.latSum << ",W_N:" << s.latN << std::endl;
    std::cout << "XGREGORM-WIFIFRAMES,N:" << s.frames << ",BYTES:" << s.bytes << std::endl;
    std::cout << "XGREGORM-OMNET-CAM,CAR:" << s.camTx << ",RSU:" << s.rsuTx << std::endl;
    // odhad spolahlivosti 802.11p v autach (spolocne jadro xgregorm-qos-core.h): ocakavane / prijate / nacas
    std::cout << "XGREGORM-QW,EXP:" << s.qExp << ",RX:" << s.qRx << ",OK:" << s.qOk << std::endl;
    std::cout << "XGREGORM-RANGE";
    for (int b = 0; b < kBins; b++)
        std::cout << ",B" << b << ":" << s.bins[0][b] << "/" << s.bins[1][b] << "/" << s.bins[2][b] << "/" << s.bins[3][b];
    std::cout << std::endl;
}

double XgCamApp::headingDeg() const
{
    auto tm = dynamic_cast<TraCIMobility*>(mob);
    return tm ? tm->getHeading().getRad() * 180.0 / M_PI : 0;
}

void XgCamApp::handlePositionUpdate(cObject* obj)
{
    DemoBaseApplLayer::handlePositionUpdate(obj);
}

void XgCamApp::handleSelfMsg(cMessage* msg)
{
    if (msg != checkEvt) {
        DemoBaseApplLayer::handleSelfMsg(msg);
        return;
    }
    simtime_t now = simTime();
    // klzave okno kvality: zabudanie kazdych 100 ms s casovou konstantou 2 s (jadro)
    for (int k = 0; k < 2; k++) {
        qExp[k] *= xgcore::qosDecay(0.1, 2.0);
        qOk[k] *= xgcore::qosDecay(0.1, 2.0);
    }
    if (isRsu) {
        sendCam();
        scheduleAt(now + 1, checkEvt);
        return;
    }
    // ETSI EN 302 637-2, 6.1.3: kontrola kazdych T_CheckCamGen = 100 ms
    auto tm = dynamic_cast<TraCIMobility*>(mob);
    Coord p = mob->getPositionAt(now);
    double v = tm ? tm->getSpeed() : 0, h = headingDeg();
    simtime_t el = now - lastCam;
    bool dyn = false;
    if (lastCam >= 0 && el >= 0.1) {
        double dh = std::fabs(std::fmod(h - lastHeading + 540.0, 360.0) - 180.0);
        dyn = dh > 4.0 || p.distance(lastPos) > 4.0 || std::fabs(v - lastSpeed) > 0.5;
    }
    if (lastCam < 0 || dyn || el >= tGenCam) {
        if (dyn) {
            // dynamicka CAM: T_GenCam = odstup, plati pre dalsie N_GenCam = 3 CAM
            tGenCam = el;
            nGenCam = 3;
        }
        else if (nGenCam > 0 && --nGenCam == 0)
            tGenCam = 1;
        sendCam();
        lastPos = p;
        lastSpeed = v;
        lastHeading = h;
    }
    scheduleAt(now + 0.1, checkEvt);
}

void XgCamApp::sendCam()
{
    simtime_t now = simTime();
    lastCam = now;
    int bytes = camBytes;
    if (now - lastCert >= 1) {
        bytes += certBytes;
        lastCert = now;
    }
    else
        bytes += digestBytes;
    XgCam* m = new XgCam("CAM");
    populateWSM(m);
    m->setByteLength(bytes);
    m->setSender(idx);
    m->setSeq(++seq);
    m->setGenTime(now);
    m->setMark(xgcore::qosMark(true, false, seq));
    m->setIsRsu(isRsu);
    Stats& s = S();
    s.frames++;
    s.bytes += bytes;
    if (isRsu)
        s.rsuTx++;
    else
        s.camTx++;
    // ocakavani prijemcovia (len CAM aut, od warmup)
    if (!isRsu && now >= par("warmup").doubleValue()) {
        Coord p = mob->getPositionAt(now);
        double maxR = par("maxRange").doubleValue();
        TxRec r;
        r.t = now;
        for (int k = 0; k < (int) s.st.size(); k++) {
            if (k == idx || !s.st[k].active)
                continue;
            double d = p.distance(s.st[k].mob->getPositionAt(now));
            if (d > maxR)
                continue;
            int b = std::min(kBins - 1, (int) (d / kBin));
            r.pend[k] = b;
            s.bins[s.st[k].rsu ? 2 : 0][b]++;
        }
        auto key = std::make_pair(idx, seq);
        s.tx[key] = std::move(r);
        s.order.push_back({now, key});
    }
    // zabudni CAM starsie nez 2 s (prijem pride do milisekund)
    while (!s.order.empty() && now - s.order.front().first > 2) {
        s.tx.erase(s.order.front().second);
        s.order.pop_front();
    }
    sendDown(m);
}

void XgCamApp::onWSM(BaseFrame1609_4* wsm)
{
    XgCam* m = dynamic_cast<XgCam*>(wsm);
    if (!m)
        return;
    Stats& s = S();
    // odhad kvality spolocnym jadrom (ako --qos-rat v ns-3): susedia do 100 m, rozpocet 100 ms
    if (!isRsu && !m->isRsu()) {
        double now = simTime().dbl();
        int snd = m->getSender();
        xgcore::QNeighbor& st = qn[snd];
        xgcore::qosRx(st, 0, m->getMark(), (simTime() - m->getGenTime()).dbl(), now, [&](const xgcore::QRec& r, int miss) {
            bool near = mob->getPositionAt(simTime()).distance(s.st[snd].mob->getPositionAt(simTime())) <= 100;
            if (!near || !r.has[0])
                return;
            double e = xgcore::qosExpected(st, miss, 0);
            bool ok = xgcore::qosOnTime(r, 0, 0.1);
            qExp[0] += e;
            qOk[0] += ok;
            if (r.t0 >= par("warmup").doubleValue()) {
                s.qExp += e;
                s.qRx += r.got[0];
                s.qOk += ok;
            }
        });
    }
    auto it = s.tx.find(std::make_pair(m->getSender(), m->getSeq()));
    if (it == s.tx.end())
        return;
    auto pe = it->second.pend.find(idx);
    if (pe == it->second.pend.end())
        return;
    int b = pe->second;
    it->second.pend.erase(pe);
    s.bins[isRsu ? 3 : 1][b]++;
    if (!isRsu && b < 2) {
        s.latSum += (simTime() - m->getGenTime()).dbl();
        s.latN++;
    }
}

} // namespace veins
