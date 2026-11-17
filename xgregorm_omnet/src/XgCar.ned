package xgregorm;

import org.car2x.veins.base.modules.IBaseApplLayer;
import org.car2x.veins.modules.nic.INic80211p;
import org.car2x.veins.base.modules.IMobility;

// xgregorm 2026-11-17: auto s dvoma priamymi radiami pre porovnanie vyberu technologie s ns-3. nic = technologia 0
// (802.11p, connectionManager), nic2 = technologia 1 (druhe priame radio na samostatnom kanali, connectionManager2).
// Veins nema NR-V2X sidelink: druha technologia je preto abstrakcia (vlastny kanal bez vzajomneho rusenia, rovnaky model
// sirenia); porovnava sa vyber technologie (jadro xgregorm-qos-core.h), nie fyzika NR.
module XgCar
{
    parameters:
        string applType;
        string nicType = default("xgregorm.XgNic80211p");
        string veinsmobilityType = default("org.car2x.veins.modules.mobility.traci.TraCIMobility");
    gates:
        input veinsradioIn @directIn;
        input veinsradioIn2 @directIn;
    submodules:
        appl: <applType> like IBaseApplLayer {
            @display("p=60,50");
        }
        nic: <nicType> like INic80211p {
            @display("p=60,166");
        }
        nic2: <nicType> like INic80211p {
            connectionManagerName = "connectionManager2";
            @display("p=160,166");
        }
        veinsmobility: <veinsmobilityType> like IMobility {
            @display("p=130,250;i=block/cogwheel");
        }
    connections allowunconnected:
        nic.upperLayerOut --> appl.lowerLayerIn;
        nic.upperLayerIn <-- appl.lowerLayerOut;
        nic.upperControlOut --> appl.lowerControlIn;
        nic.upperControlIn <-- appl.lowerControlOut;
        veinsradioIn --> nic.radioIn;
        nic2.upperLayerOut --> appl.lowerLayerIn2;
        nic2.upperLayerIn <-- appl.lowerLayerOut2;
        nic2.upperControlOut --> appl.lowerControlIn2;
        veinsradioIn2 --> nic2.radioIn;
}
