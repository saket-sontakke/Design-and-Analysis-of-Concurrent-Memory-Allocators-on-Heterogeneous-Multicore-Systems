#include "core_topology.h"
#include <iostream>
#include <fstream>
#include <sstream>

int main() {
    try {
        std::ostringstream oss;
        oss << "Detecting detailed system topology...\n\n";
        
        SystemTopology topo = detectTopology();
        
        printTopology(topo, oss);
        
        oss << "\n================ EXECUTION PINNING VERIFICATION ================\n";
        
        if (pinThreadToCoreType(topo, CoreType::P_CORE)) {
            oss << "Successfully pinned thread to P-Cores. Currently executing on: ";
            CoreType current = getCurrentCoreType(topo);
            if (current == CoreType::P_CORE) oss << "P-Core\n";
            else if (current == CoreType::E_CORE) oss << "E-Core\n";
            else oss << "LP E-Core\n";
        } else {
            oss << "Failed to pin to P-Cores.\n";
        }

        if (pinThreadToCoreType(topo, CoreType::E_CORE)) {
            oss << "Successfully pinned thread to E-Cores. Currently executing on: ";
            CoreType current = getCurrentCoreType(topo);
            if (current == CoreType::P_CORE) oss << "P-Core\n";
            else if (current == CoreType::E_CORE) oss << "E-Core\n";
            else oss << "LP E-Core\n";
        } else {
            oss << "Failed to pin to E-Cores.\n";
        }

        if (pinThreadToCoreType(topo, CoreType::LP_E_CORE)) {
            oss << "Successfully pinned thread to LP E-Cores. Currently executing on: ";
            CoreType current = getCurrentCoreType(topo);
            if (current == CoreType::P_CORE) oss << "P-Core\n";
            else if (current == CoreType::E_CORE) oss << "E-Core\n";
            else oss << "LP E-Core\n";
        } else {
            oss << "Failed to pin to LP E-Cores.\n";
        }

        std::cout << oss.str();
        
        std::ofstream outFile("topology_output.txt");
        if (outFile.is_open()) {
            outFile << oss.str();
            outFile.close();
            std::cout << "\n[SUCCESS] Output successfully saved to 'topology_output.txt'\n";
        } else {
            std::cerr << "\n[ERROR] Failed to save output to file.\n";
        }
        
        std::cout << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n\n";
    }
    return 0;
}