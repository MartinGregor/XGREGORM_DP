// xgregorm: aplikacia CAM pre Veins a meranie dorucenia v rovnakom formate ako xgregorm-hybrid (ns-3).
//
// Meranie (rovnaka definicia ako --range-stats a XGREGORM-V2V v ns-3):
//  - pri kazdej CAM auta (od casu warmup) sa zapamata, ktore stanice su do maxRange a v ktorom kosi vzdialenosti
//    (po 50 m, poloha v okamihu vyslania); auto-auto = v2v, auto-RSU = ul,
//  - prijem: prva kopia CAM u danej stanice sa zapocita do jej kosa,
//  - XGREGORM-V2V = auto-auto do 100 m (kose 0 a 1), XGREGORM-V2VLAT = sucet oneskoreni [s] tychto prijmov.

#include "XgCamApp.h"

#include <cmath>
#include <list>
#include <sstream>
#include <string>
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
    // xgregorm 2026-11-17: dve technologie - RSU podla indexu (zony), mapa kvality, pocty
    std::map<int, BaseMobility*> rsuPos;
    struct Zone { int rsu; double r, t0, t1, p; int k; };
    std::vector<Zone> zones;
    struct Cell { double t = 0, e[2] = {0, 0}, ok[2] = {0, 0}; };
    std::map<std::pair<int, int>, Cell> rem;
    long long txK[2] = {0, 0}, camBoth = 0, sw = 0, pxN[2] = {0, 0}, zoneDrops = 0, mainN[2] = {0, 0};
    double qE[2] = {0, 0}, qO[2] = {0, 0};
    std::string policy;
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
        twoRat = par("twoRat").boolValue() && !isRsu;
        policy = par("policy").stdstringValue();
        gOut2 = findGate("lowerLayerOut2");
        gIn2 = findGate("lowerLayerIn2");
        gCtl2 = findGate("lowerControlIn2");
        if (isRsu)
            S().rsuPos[getParentModule()->getIndex()] = mob;
        // zony (rovnaky zapis ako --zones v ns-3), raz pre celu simulaciu
        std::string zs = par("zones").stdstringValue();
        if (!zs.empty() && S().zones.empty()) {
            std::stringstream ss(zs);
            std::string it;
            while (std::getline(ss, it, '+')) {
                Stats::Zone z;
                char w, c;
                std::stringstream one(it);
                if (!(one >> z.rsu >> c >> z.r >> c >> z.t0 >> c >> z.t1 >> c >> w >> c >> z.p))
                    throw cRuntimeError("zones: expected rsu:radius:t0:t1:W|N:loss, got %s", it.c_str());
                z.k = w == 'N' ? 1 : 0;
                S().zones.push_back(z);
            }
        }
    }
    else if (stage == 1) {
        // nahodny posun ako desynchronizacia v ns-3 (auta: kontrola kazdych 100 ms, RSU: CAM kazdu 1 s)
        scheduleAt(simTime() + uniform(0, isRsu ? 1.0 : 0.1), checkEvt);
        if (twoRat) {
            // hlavna technologia na zaciatku ako --init-main 2 (nahodne), FW = len 802.11p
            mainK = policy == "FW" ? 0 : (uniform(0, 1) < 0.5 ? 1 : 0);
            lastProbe = simTime() - uniform(0, 1.0 / par("probeHz").doubleValue());
            nextDecide = simTime() + uniform(0, 1);
            nextRem = simTime() + uniform(0, par("remPeriod").doubleValue());
            S().policy = policy;
        }
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
    if (!s.policy.empty())
        std::cout << "XGREGORM-OMNET-RAT,POLICY:" << s.policy << ",TX0:" << s.txK[0] << ",TX1:" << s.txK[1] << ",BOTH:" << s.camBoth
                  << ",SW:" << s.sw << ",PX:" << s.pxN[0] << "/" << s.pxN[1] << ",ZONEDROP:" << s.zoneDrops << ",MAIN:" << s.mainN[0]
                  << "/" << s.mainN[1] << ",Q0:" << s.qE[0] << "/" << s.qO[0] << ",Q1:" << s.qE[1] << "/" << s.qO[1] << std::endl;
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
    if (twoRat) {
        if (policy != "FW" && policy != "MIX" && now - lastProbe >= 1.0 / par("probeHz").doubleValue()) {
            probeDue = true;
            lastProbe = now;
        }
        if (now >= nextDecide) {
            decide();
            nextDecide = now + 1;
        }
        if (now >= nextRem) {
            remFlush();
            nextRem = now + par("remPeriod").doubleValue();
        }
        if (now >= par("warmup").doubleValue())
            S().mainN[mainK]++;
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
    // technologie tejto CAM: hlavna, pri skuske obe (ako --clean 1 v ns-3)
    bool use[2] = {!twoRat || mainK == 0, twoRat && (mainK == 1 || probeDue)};
    if (twoRat && probeDue)
        use[0] = true;
    probeDue = false;
    m->setMark(xgcore::qosMark(use[0], use[1], seq));
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
    if (twoRat) {
        if (now >= par("warmup").doubleValue()) {
            s.txK[0] += use[0];
            s.txK[1] += use[1];
            s.camBoth += use[0] && use[1];
        }
        if (use[1]) {
            if (use[0])
                send(m->dup(), gOut2);
            else {
                send(m, gOut2);
                return;
            }
        }
    }
    sendDown(m);
}

void XgCamApp::handleMessage(cMessage* msg)
{
    if (msg->getArrivalGateId() == gIn2) {
        curK = 1;
        if (auto* w = dynamic_cast<BaseFrame1609_4*>(msg))
            onWSM(w);
        curK = 0;
        delete msg;
        return;
    }
    if (msg->getArrivalGateId() == gCtl2) {
        delete msg;
        return;
    }
    DemoBaseApplLayer::handleMessage(msg);
}

bool XgCamApp::zoneDrop(int k)
{
    Stats& s = S();
    if (s.zones.empty())
        return false;
    double now = simTime().dbl();
    Coord p = mob->getPositionAt(simTime());
    for (const auto& z : s.zones) {
        auto rp = s.rsuPos.find(z.rsu);
        if (z.k == k && now >= z.t0 && now < z.t1 && rp != s.rsuPos.end() && p.distance(rp->second->getPositionAt(simTime())) <= z.r)
            return uniform(0, 1) < z.p;
    }
    return false;
}

void XgCamApp::remFlush()
{
    // hlasenia do mapy kvality raz za remPeriod (ako --rem-period 1 v ns-3); bunka zabuda s casovou konstantou remTau
    Stats& s = S();
    double now = simTime().dbl(), cell = par("remCell").doubleValue(), tau = par("remTau").doubleValue();
    for (const RemRep& r : remBuf) {
        auto& c = s.rem[{(int) std::floor(r.x / cell), (int) std::floor(r.y / cell)}];
        double f = std::exp(-(now - c.t) / tau);
        for (int k = 0; k < 2; k++)
            c.e[k] *= f, c.ok[k] *= f;
        c.t = now;
        c.e[r.k] += r.e;
        c.ok[r.k] += r.ok;
    }
    remBuf.clear();
}

bool XgCamApp::predict(double fut[2], bool has[2], int cnt[2])
{
    // body trasy o 1..H s pri sucasnej rychlosti (ako xgPredict v ns-3), priemer mapy v nich (bunky s >= 10 ocakavanymi)
    auto tm = dynamic_cast<TraCIMobility*>(mob);
    double sum[2] = {0, 0};
    int n[2] = {0, 0};
    has[0] = has[1] = false;
    fut[0] = fut[1] = 0;
    if (!tm)
        return false;
    Stats& s = S();
    double now = simTime().dbl(), cell = par("remCell").doubleValue(), tau = par("remTau").doubleValue();
    auto* veh = tm->getVehicleCommandInterface();
    auto* ci = tm->getCommandInterface();
    std::list<std::string> roads = veh->getPlannedRoadIds();
    std::string lane = veh->getLaneId();
    std::vector<std::string> lanes{lane};
    std::string road = tm->getRoadId();
    bool after = false;
    for (const auto& r : roads) {
        if (after && lanes.size() < 8)
            lanes.push_back(r + "_0");
        if (r == road)
            after = true;
    }
    double need = veh->getLanePosition(), v = tm->getSpeed();
    int H = (int) std::lround(par("predHorizon").doubleValue());
    size_t li = 0;
    std::vector<Coord> shp;
    size_t si = 0;
    double off = 0;
    auto load = [&](size_t i) {
        shp.clear();
        if (i < lanes.size() && lanes[i].size() && lanes[i][0] != ':')
            for (const Coord& c : ci->lane(lanes[i]).getShape())
                shp.push_back(c);
        si = 0;
        off = 0;
    };
    load(0);
    for (int i = 1; i <= H; i++) {
        need += v;
        bool ok = false;
        Coord pt;
        while (li < lanes.size()) {
            bool done = false;
            while (si + 1 < shp.size()) {
                double len = shp[si].distance(shp[si + 1]);
                if (off + need <= len) {
                    off += need;
                    need = 0;
                    pt = shp[si] + (shp[si + 1] - shp[si]) * (off / std::max(len, 1e-9));
                    done = true;
                    break;
                }
                need -= len - off;
                off = 0;
                si++;
            }
            if (done) {
                ok = true;
                break;
            }
            load(++li);
        }
        if (!ok)
            break;
        auto it = s.rem.find({(int) std::floor(pt.x / cell), (int) std::floor(pt.y / cell)});
        if (it == s.rem.end())
            continue;
        double f = std::exp(-(now - it->second.t) / tau);
        for (int k = 0; k < 2; k++)
            if (it->second.e[k] * f >= 10) {
                sum[k] += it->second.ok[k] / it->second.e[k];
                n[k]++;
            }
    }
    for (int k = 0; k < 2; k++) {
        cnt[k] = n[k];
        if (n[k]) {
            fut[k] = sum[k] / n[k];
            has[k] = true;
        }
    }
    return true;
}

void XgCamApp::decide()
{
    // raz za 1 s: pravidlo kvality spolocnym jadrom (xgcore::qosDecide), pri PM / PX s mapou kvality
    if (policy == "FW" || policy == "MIX")
        return;
    Stats& s = S();
    int im = mainK, io = 1 - mainK;
    bool known = xgcore::qosKnown(qExp, 10);
    double r[2] = {known ? qOk[0] / qExp[0] : 0, known ? qOk[1] / qExp[1] : 0};
    double gain = r[io] - r[im];
    if (known && (policy == "PM" || policy == "PX")) {
        double fut[2];
        bool has[2];
        int cnt[2] = {0, 0};
        predict(fut, has, cnt);
        if (policy == "PM") {
            // povodna predikcia (ako ns-3): (sucasne meranie + sucet mapy v bodoch trasy) / (1 + pocet bodov)
            double rr[2];
            for (int k = 0; k < 2; k++)
                rr[k] = (r[k] + fut[k] * cnt[k]) / (1 + cnt[k]);
            gain = rr[io] - rr[im];
        }
        else {
            double cell = par("remCell").doubleValue(), mNow[2] = {0, 0};
            bool hNow[2] = {false, false}, fresh = false;
            Coord p = mob->getPositionAt(simTime());
            auto it = s.rem.find({(int) std::floor(p.x / cell), (int) std::floor(p.y / cell)});
            if (it != s.rem.end()) {
                double f = std::exp(-(simTime().dbl() - it->second.t) / par("remTau").doubleValue());
                for (int k = 0; k < 2; k++)
                    if (it->second.e[k] * f >= 10)
                        mNow[k] = it->second.ok[k] / it->second.e[k], hNow[k] = true;
            }
            gain = xgcore::pxGain(r, mNow, hNow, fut, has, im, par("qosMargin").doubleValue(), par("pxDev").doubleValue(), &fresh);
            if (simTime() >= par("warmup").doubleValue())
                s.pxN[fresh]++;
        }
    }
    auto d = xgcore::qosDecide(known, gain, par("qosMargin").doubleValue(), 0, 1, uniform(0, 1));
    if (d == xgcore::QDecision::Switch) {
        mainK = io;
        if (simTime() >= par("warmup").doubleValue())
            s.sw++;
    }
}

void XgCamApp::onWSM(BaseFrame1609_4* wsm)
{
    XgCam* m = dynamic_cast<XgCam*>(wsm);
    if (!m)
        return;
    Stats& s = S();
    int k = curK;
    // zony: lokalne zhorsenie prijmu technologie k podla polohy prijimaca (ako --zones v ns-3)
    if (!isRsu && zoneDrop(k)) {
        if (simTime() >= par("warmup").doubleValue())
            s.zoneDrops++;
        return;
    }
    // odhad kvality spolocnym jadrom (ako --qos-rat v ns-3): susedia do 100 m, rozpocet 100 ms
    if (!isRsu && !m->isRsu()) {
        double now = simTime().dbl();
        int snd = m->getSender();
        xgcore::QNeighbor& st = qn[snd];
        xgcore::qosRx(st, k, m->getMark(), (simTime() - m->getGenTime()).dbl(), now, [&](const xgcore::QRec& r, int miss) {
            Coord me = mob->getPositionAt(simTime());
            bool near = me.distance(s.st[snd].mob->getPositionAt(simTime())) <= 100;
            if (!near)
                return;
            for (int kk = 0; kk < (twoRat ? 2 : 1); kk++) {
                if (!r.has[kk])
                    continue;
                double e = xgcore::qosExpected(st, miss, kk);
                bool ok = xgcore::qosOnTime(r, kk, 0.1);
                qExp[kk] += e;
                qOk[kk] += ok;
                if (twoRat)
                    remBuf.push_back({me.x, me.y, kk, e, (double) ok});
                if (r.t0 >= par("warmup").doubleValue()) {
                    if (kk == 0) {
                        s.qExp += e;
                        s.qRx += r.got[0];
                        s.qOk += ok;
                    }
                    s.qE[kk] += e;
                    s.qO[kk] += ok;
                }
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
