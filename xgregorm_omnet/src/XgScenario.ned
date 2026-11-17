package xgregorm;

import org.car2x.veins.nodes.RSU;
import org.car2x.veins.nodes.Scenario;
import org.car2x.veins.base.connectionManager.ConnectionManager;

// xgregorm: scenar = Veins Scenario (SUMO cez TraCI, auta ako org.car2x.veins.nodes.Car) + RSU z stations.xml
network XgScenario extends Scenario
{
    parameters:
        int numRsu = default(0);
    submodules:
        connectionManager2: ConnectionManager { // xgregorm: kanal druhej priamej technologie (XgCar.nic2)
            @display("p=150,30;i=abstract/multicast");
        }
        rsu[numRsu]: RSU {
            @display("i=veins/sign/yellowdiamond;is=vs");
        }
}
