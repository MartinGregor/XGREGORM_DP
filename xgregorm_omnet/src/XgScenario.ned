package xgregorm;

import org.car2x.veins.nodes.RSU;
import org.car2x.veins.nodes.Scenario;

// xgregorm: scenar = Veins Scenario (SUMO cez TraCI, auta ako org.car2x.veins.nodes.Car) + RSU z stations.xml
network XgScenario extends Scenario
{
    parameters:
        int numRsu = default(0);
    submodules:
        rsu[numRsu]: RSU {
            @display("i=veins/sign/yellowdiamond;is=vs");
        }
}
