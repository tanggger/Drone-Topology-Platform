// ns-3 mobility setup code
// Generated automatically from RTK data

#include "ns3/waypoint-mobility-model.h"
#include "ns3/mobility-helper.h"

void SetupMobility(NodeContainer& nodes) {
    // 确保节点数量匹配
    NS_ASSERT(nodes.GetN() == 20);
    
    // 为每个节点设置WaypointMobilityModel
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::WaypointMobilityModel");
    mobility.Install(nodes);
    
    // 加载waypoint数据

    // UAV 0
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(0)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_0.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 1
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(1)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_1.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 2
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(2)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_2.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 3
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(3)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_3.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 4
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(4)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_4.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 5
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(5)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_5.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 6
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(6)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_6.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 7
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(7)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_7.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 8
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(8)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_8.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 9
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(9)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_9.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 10
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(10)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_10.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 11
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(11)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_11.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 12
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(12)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_12.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 13
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(13)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_13.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 14
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(14)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_14.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 15
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(15)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_15.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 16
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(16)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_16.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 17
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(17)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_17.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 18
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(18)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_18.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
    // UAV 19
    {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(19)->GetObject<WaypointMobilityModel>();
        
        std::ifstream file("rtk/ns3_traces/uav_19.wp");
        std::string line;
        std::getline(file, line); // skip header
        std::getline(file, line); // skip header
        
        double time, x, y, z;
        while (file >> time >> x >> y >> z) {
            waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
        }
        file.close();
    }
}

// 仿真参数
const double SIMULATION_TIME = 99.8; // 秒
const uint32_t NUM_NODES = 20;
