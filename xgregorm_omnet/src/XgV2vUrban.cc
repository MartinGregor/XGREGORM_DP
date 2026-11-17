// xgregorm: mestsky kanal 3GPP TR 37.885 V2V Urban pre Veins (popis v XgV2vUrban.h)
#include "XgV2vUrban.h"

#include <cmath>
#include <fstream>

#include "veins/base/toolbox/Signal.h"

namespace veins {

std::vector<XgV2vUrban::Box>& XgV2vUrban::boxes()
{
    static std::vector<Box> b;
    return b;
}
std::map<std::pair<uintptr_t, uintptr_t>, XgV2vUrban::Cond>& XgV2vUrban::conds()
{
    static std::map<std::pair<uintptr_t, uintptr_t>, Cond> m;
    return m;
}
std::map<std::pair<uintptr_t, uintptr_t>, XgV2vUrban::Shadow>& XgV2vUrban::shadows()
{
    static std::map<std::pair<uintptr_t, uintptr_t>, Shadow> m;
    return m;
}

XgV2vUrban::XgV2vUrban(cComponent* owner, std::string buildingsFile, double freqGHz, bool fading)
    : AnalogueModel(owner)
    , comp(owner)
    , fGHz(freqGHz)
    , fading(fading)
{
    // budovy v suradniciach OMNeT++ (export_scenario.py ich prepocita): "x0 x1 y0 y1" na riadok
    if (boxes().empty() && !buildingsFile.empty()) {
        std::ifstream f(buildingsFile);
        if (!f)
            throw cRuntimeError("XgV2vUrban: cannot read %s", buildingsFile.c_str());
        Box b;
        while (f >> b.x0 >> b.x1 >> b.y0 >> b.y1)
            boxes().push_back(b);
    }
}

// usecka a-b pretina obdlznik (budovy su vyssie nez anteny 1.5 a 5 m, staci rovina x-y)
bool XgV2vUrban::blocked(double ax, double ay, double bx, double by) const
{
    for (const Box& b : boxes()) {
        double t0 = 0, t1 = 1, dx = bx - ax, dy = by - ay;
        bool out = false;
        const double p[4] = {-dx, dx, -dy, dy};
        const double q[4] = {ax - b.x0, b.x1 - ax, ay - b.y0, b.y1 - ay};
        for (int i = 0; i < 4 && !out; i++) {
            if (p[i] == 0) {
                if (q[i] < 0)
                    out = true;
            }
            else {
                double r = q[i] / p[i];
                if (p[i] < 0)
                    t0 = std::max(t0, r);
                else
                    t1 = std::min(t1, r);
                if (t0 > t1)
                    out = true;
            }
        }
        if (!out)
            return true;
    }
    return false;
}

void XgV2vUrban::filterSignal(Signal* signal)
{
    Coord a = signal->getSenderPoa().pos.getPositionAt();
    Coord r = signal->getReceiverPoa().pos.getPositionAt();
    uintptr_t ia = (uintptr_t) signal->getSenderPoa().antenna.get(), ir = (uintptr_t) signal->getReceiverPoa().antenna.get();
    auto key = std::make_pair(std::min(ia, ir), std::max(ia, ir));
    double d2 = std::hypot(a.x - r.x, a.y - r.y), d3 = std::max(1.0, a.distance(r));
    // stav kanala (symetricky pre dvojicu), najskor po 100 ms
    Cond& c = conds()[key];
    if (c.c < 0 || simTime() - c.t >= 0.1) {
        if (blocked(a.x, a.y, r.x, r.y))
            c.c = 2;
        else
            c.c = comp->uniform(0, 1) <= std::min(1.0, 1.05 * std::exp(-0.0114 * d2)) ? 0 : 1;
        c.t = simTime();
    }
    double pl;
    if (c.c == 2)
        pl = 36.85 + 30 * std::log10(d3) + 18.9 * std::log10(fGHz);
    else {
        pl = 38.77 + 16.7 * std::log10(d3) + 18.2 * std::log10(fGHz);
        if (c.c == 1) {
            // blokovanie autom (TR 37.885 6.2.1, ako ns-3): vyska blokujuceho auta 3 m s pravdepodobnostou percType3
            // (ns-3 predvolene 0 %), inak 1.6 m; obe anteny vyssie = 0 dB; obe nizsie: lognormal(9 + ..., 4.5);
            // inak (napr. RSU 5 m a auto 1.5 m): lognormal(5 + ..., 4)
            double hb = comp->uniform(0, 100) < percType3 ? 3.0 : 1.6;
            double hmin = std::min(a.z, r.z), hmax = std::max(a.z, r.z);
            if (hmin <= hb) {
                bool low = hmax < hb;
                double mu = (low ? 9.0 : 5.0) + std::max(0.0, 15 * std::log10(d3) - 41.0), sg = low ? 4.5 : 4.0;
                double m = std::log(mu * mu / std::sqrt(sg * sg + mu * mu)), s = std::sqrt(std::log(sg * sg / (mu * mu) + 1));
                pl += std::max(0.0, comp->lognormal(m, s));
            }
        }
    }
    // korelovane tienenie
    Shadow& sh = shadows()[key];
    double std = c.c == 2 ? 4.0 : 3.0, dcor = c.c == 0 ? 10.0 : 13.0;
    double dx = a.x - r.x, dy = a.y - r.y;
    if (sh.c != c.c)
        sh.s = comp->normal(0, 1) * std;
    else {
        double R = std::exp(-std::hypot(dx - sh.dx, dy - sh.dy) / dcor);
        sh.s = R * sh.s + std::sqrt(1 - R * R) * comp->normal(0, 1) * std;
    }
    sh.c = c.c;
    sh.dx = dx;
    sh.dy = dy;
    pl += sh.s;
    double g = std::pow(10.0, -pl / 10.0);
    // Nakagami-m: vykon ~ Gamma(m, 1/m)
    if (fading) {
        double m = d3 < 50 ? 3.0 : d3 < 150 ? 1.5 : 1.0;
        g *= comp->gamma_d(m, 1.0 / m);
    }
    *signal *= g;
}

} // namespace veins
