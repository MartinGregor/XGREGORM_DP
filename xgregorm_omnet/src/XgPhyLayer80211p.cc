#include "XgPhyLayer80211p.h"
#include "XgV2vUrban.h"

namespace veins {
Define_Module(veins::XgPhyLayer80211p);

std::unique_ptr<AnalogueModel> XgPhyLayer80211p::getAnalogueModelFromName(std::string name, ParameterMap& params)
{
    if (name == "XgV2vUrban") {
        std::string file = params.count("buildingsFile") ? params["buildingsFile"].stringValue() : "";
        double f = params.count("frequencyGHz") ? params["frequencyGHz"].doubleValue() : 5.9;
        bool fad = params.count("fading") ? params["fading"].boolValue() : true;
        return std::make_unique<XgV2vUrban>(this, file, f, fad);
    }
    return PhyLayer80211p::getAnalogueModelFromName(name, params);
}
} // namespace veins
