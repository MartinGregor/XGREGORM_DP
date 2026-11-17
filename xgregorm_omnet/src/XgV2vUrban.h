// xgregorm: mestsky kanal 3GPP TR 37.885 V2V Urban pre Veins, rovnaky ako --channel urban v xgregorm-hybrid (ns-3):
//  - budova medzi stanicami (obdlzniky z xgregorm_buildings.txt) = NLOS, inak LOS s pravdepodobnostou
//    min(1, 1.05 exp(-0.0114 d)), inak NLOSv (auta v ceste); stav dvojice sa prepocita najskor po 100 ms,
//  - utlm: LOS 38.77 + 16.7 log d + 18.2 log f, NLOS 36.85 + 30 log d + 18.9 log f (f v GHz, d 3D v m),
//    NLOSv = LOS + max(0, lognormal(9 + max(0, 15 log d - 41), 4.5)) dB pre kazdy ramec,
//  - tienenie: LOS a NLOSv 3 dB, NLOS 4 dB, korelacia exp(-posun / 10 m alebo 13 m) ako ThreeGppPropagationLossModel,
//  - rychle uniky Nakagami-m (m = 3 do 50 m, 1.5 do 150 m, 1 dalej) pre kazdy ramec, ako --fading 1.
// Nahodne cisla su z OMNeT++, nie z ns-3: zhoda je statisticka, nie ramec po ramci.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "veins/base/phyLayer/AnalogueModel.h"

namespace veins {

class XgV2vUrban : public AnalogueModel {
public:
    XgV2vUrban(cComponent* owner, std::string buildingsFile, double freqGHz, bool fading);
    void filterSignal(Signal* signal) override;

protected:
    struct Box {
        double x0, x1, y0, y1;
    };
    struct Cond {
        int c = -1;              // 0 LOS, 1 NLOSv, 2 NLOS
        simtime_t t = -1;        // cas vyhodnotenia
    };
    struct Shadow {
        int c = -1;
        double dx = 0, dy = 0, s = 0;
    };
    bool blocked(double ax, double ay, double bx, double by) const;
    cComponent* comp;
    double fGHz;
    bool fading;
    double percType3 = 0; // podiel aut typu 3 (3 m) [%], ns-3 ThreeGppV2vUrbanPropagationLossModel::PercType3Vehicles
    static std::vector<Box>& boxes();
    static std::map<std::pair<uintptr_t, uintptr_t>, Cond>& conds();
    static std::map<std::pair<uintptr_t, uintptr_t>, Shadow>& shadows();
};

} // namespace veins
