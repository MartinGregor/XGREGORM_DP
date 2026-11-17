// xgregorm: PhyLayer80211p s modelom kanala XgV2vUrban (v config.xml: AnalogueModel type="XgV2vUrban")
#pragma once
#include "veins/modules/phy/PhyLayer80211p.h"

namespace veins {
class XgPhyLayer80211p : public PhyLayer80211p {
protected:
    std::unique_ptr<AnalogueModel> getAnalogueModelFromName(std::string name, ParameterMap& params) override;
};
} // namespace veins
