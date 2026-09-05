#pragma once

#include <windows.h>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <iomanip>
#include <string>
#include <sstream>
#include <cstdint>
#include <algorithm>

#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

enum class CoreType { P_CORE, E_CORE, LP_E_CORE };

struct CpuVersionInfo {
    int family;
    int model;
    int stepping;
    bool isHybrid;
};

struct NumaNodeInfo {
    DWORD nodeNumber;
    DWORD_PTR mask;
    std::vector<int> logicalProcIds;
};

struct CacheInstanceInfo {
    int level;
    std::string type;
    int sizeKB;
    int lineSizeBytes;
    int associativity;
    DWORD_PTR mask;
    std::vector<int> logicalProcIds;
};

struct CoreInfo {
    int coreIndex;
    CoreType type;
    BYTE efficiencyClass;
    DWORD_PTR affinityMask;
    std::vector<int> logicalProcIds;
    bool hasSMT;
    int l1dSizeKB;
    int l1dAssoc;
    int l1iSizeKB;
    int l1iAssoc;
    int l2SizeKB;
    int l2Assoc;
    bool hasL3;
    int l3SizeKB;
    int l3Assoc;
    int lineSizeBytes;
};

struct SystemTopology {
    std::string cpuBrand;
    std::string cpuVendor;
    CpuVersionInfo cpuVersion;
    
    int socketCount;
    int numaNodeCount;
    int totalLogicalProcessors;
    int totalPhysicalCores;
    
    std::vector<NumaNodeInfo> numaNodes;
    std::vector<CacheInstanceInfo> cacheInstances;
    std::vector<CoreInfo> cores;
    
    std::vector<int> pCoreIndices;
    std::vector<int> eCoreIndices;
    std::vector<int> lpECoreIndices;
    
    DWORD_PTR pCoreMask;
    DWORD_PTR eCoreMask;
    DWORD_PTR lpECoreMask;
    
    int totalL1dKB;
    int totalL1iKB;
    int totalL2KB;
    int totalL3KB;
};

inline std::vector<int> maskToLogicalProcIds(DWORD_PTR mask) {
    std::vector<int> ids;
    for (int i = 0; i < static_cast<int>(sizeof(DWORD_PTR) * 8); ++i) {
        if (mask & (static_cast<DWORD_PTR>(1) << i)) {
            ids.push_back(i);
        }
    }
    return ids;
}

inline std::string formatProcIds(const std::vector<int>& ids) {
    std::ostringstream oss;
    for (size_t i = 0; i < ids.size(); ++i) {
        oss << ids[i];
        if (i + 1 < ids.size()) oss << ", ";
    }
    return oss.str();
}

inline std::string formatAssociativity(int assoc) {
    if (assoc == 0xFF || assoc == 65535) return "Fully Assoc";
    if (assoc == 0) return "Unknown";
    return std::to_string(assoc) + "-way";
}

inline std::string getCpuBrandString() {
    int cpuInfo[4] = {0};
    char brand[49] = {0};
    __cpuid(cpuInfo, 0x80000000);
    unsigned int nExIds = cpuInfo[0];
    if (nExIds >= 0x80000004) {
        __cpuid(reinterpret_cast<int*>(brand), 0x80000002);
        __cpuid(reinterpret_cast<int*>(brand + 16), 0x80000003);
        __cpuid(reinterpret_cast<int*>(brand + 32), 0x80000004);
        return std::string(brand);
    }
    return "Unknown CPU";
}

inline std::string getCpuVendor() {
    int cpuInfo[4] = {0};
    char vendor[13] = {0};
    __cpuid(cpuInfo, 0);
    *reinterpret_cast<int*>(vendor) = cpuInfo[1];
    *reinterpret_cast<int*>(vendor + 4) = cpuInfo[3];
    *reinterpret_cast<int*>(vendor + 8) = cpuInfo[2];
    return std::string(vendor);
}

inline CpuVersionInfo getCpuVersionInfo() {
    CpuVersionInfo info = {};
    int cpuInfo[4] = {0};
    
    __cpuid(cpuInfo, 1);
    int stepping = cpuInfo[0] & 0xF;
    int model = (cpuInfo[0] >> 4) & 0xF;
    int family = (cpuInfo[0] >> 8) & 0xF;
    int extModel = (cpuInfo[0] >> 16) & 0xF;
    int extFamily = (cpuInfo[0] >> 20) & 0xFF;
    
    info.family = (family == 15) ? (family + extFamily) : family;
    info.model = (family == 6 || family == 15) ? ((extModel << 4) | model) : model;
    info.stepping = stepping;

    int leaf7Info[4] = {0};
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
    __cpuidex(leaf7Info, 7, 0);
#else
    __cpuid_count(7, 0, leaf7Info[0], leaf7Info[1], leaf7Info[2], leaf7Info[3]);
#endif
    info.isHybrid = (leaf7Info[3] & (1 << 15)) != 0;

    return info;
}

inline SystemTopology detectTopology() {
    SystemTopology topo = {};
    topo.cpuBrand = getCpuBrandString();
    topo.cpuVendor = getCpuVendor();
    topo.cpuVersion = getCpuVersionInfo();
    topo.pCoreMask = 0;
    topo.eCoreMask = 0;
    topo.lpECoreMask = 0;
    topo.totalL1dKB = 0;
    topo.totalL1iKB = 0;
    topo.totalL2KB = 0;
    topo.totalL3KB = 0;
    topo.socketCount = 0;
    topo.numaNodeCount = 0;
    topo.totalLogicalProcessors = 0;

    DWORD returnLength = 0;
    GetLogicalProcessorInformationEx(RelationAll, nullptr, &returnLength);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        throw std::runtime_error("Failed to query processor information size.");
    }

    std::vector<uint8_t> buffer(returnLength);
    if (!GetLogicalProcessorInformationEx(RelationAll, reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()), &returnLength)) {
        throw std::runtime_error("Failed to query processor information.");
    }

    DWORD_PTR l3Mask = 0;
    uint8_t* ptr = buffer.data();

    while (ptr < buffer.data() + returnLength) {
        auto info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(ptr);
        
        if (info->Relationship == RelationProcessorPackage) {
            topo.socketCount++;
        }
        else if (info->Relationship == RelationNumaNode) {
            topo.numaNodeCount++;
            NumaNodeInfo node = {};
            node.nodeNumber = info->NumaNode.NodeNumber;
            node.mask = info->NumaNode.GroupMask.Mask;
            node.logicalProcIds = maskToLogicalProcIds(node.mask);
            topo.numaNodes.push_back(node);
        }
        else if (info->Relationship == RelationCache) {
            auto& cache = info->Cache;
            CacheInstanceInfo cInfo = {};
            cInfo.level = cache.Level;
            cInfo.sizeKB = cache.CacheSize / 1024;
            cInfo.lineSizeBytes = cache.LineSize;
            cInfo.associativity = cache.Associativity;
            cInfo.mask = cache.GroupMask.Mask;
            cInfo.logicalProcIds = maskToLogicalProcIds(cInfo.mask);

            switch (cache.Type) {
                case CacheUnified:     cInfo.type = "Unified"; break;
                case CacheInstruction: cInfo.type = "Instruction"; break;
                case CacheData:        cInfo.type = "Data"; break;
                case CacheTrace:       cInfo.type = "Trace"; break;
            }

            topo.cacheInstances.push_back(cInfo);

            if (cache.Level == 3) {
                l3Mask |= cInfo.mask;
            }
        }
        ptr += info->Size;
    }

    ptr = buffer.data();
    int physicalCoreIndex = 0;
    while (ptr < buffer.data() + returnLength) {
        auto info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(ptr);
        
        if (info->Relationship == RelationProcessorCore) {
            CoreInfo core = {};
            core.coreIndex = physicalCoreIndex++;
            core.affinityMask = info->Processor.GroupMask[0].Mask;
            core.efficiencyClass = info->Processor.EfficiencyClass;
            core.logicalProcIds = maskToLogicalProcIds(core.affinityMask);
            topo.totalLogicalProcessors += static_cast<int>(core.logicalProcIds.size());
            core.hasSMT = (core.logicalProcIds.size() > 1);
            
            if (core.efficiencyClass >= 1) {
                core.type = CoreType::P_CORE;
            } else {
                if ((core.affinityMask & l3Mask) != 0) {
                    core.type = CoreType::E_CORE;
                } else {
                    core.type = CoreType::LP_E_CORE;
                }
            }
            
            for (const auto& c : topo.cacheInstances) {
                if (c.mask & core.affinityMask) {
                    if (c.level == 1 && c.type == "Data") {
                        core.l1dSizeKB = c.sizeKB;
                        core.l1dAssoc = c.associativity;
                        core.lineSizeBytes = c.lineSizeBytes;
                    }
                    if (c.level == 1 && c.type == "Instruction") {
                        core.l1iSizeKB = c.sizeKB;
                        core.l1iAssoc = c.associativity;
                    }
                    if (c.level == 2) {
                        core.l2SizeKB = c.sizeKB;
                        core.l2Assoc = c.associativity;
                    }
                    if (c.level == 3) {
                        core.l3SizeKB = c.sizeKB;
                        core.l3Assoc = c.associativity;
                    }
                }
            }
            
            core.hasL3 = (core.type != CoreType::LP_E_CORE);
            if (!core.hasL3) core.l3SizeKB = 0;

            topo.cores.push_back(core);
            
            if (core.type == CoreType::P_CORE) {
                topo.pCoreIndices.push_back(core.coreIndex);
                topo.pCoreMask |= core.affinityMask;
            } else if (core.type == CoreType::E_CORE) {
                topo.eCoreIndices.push_back(core.coreIndex);
                topo.eCoreMask |= core.affinityMask;
            } else if (core.type == CoreType::LP_E_CORE) {
                topo.lpECoreIndices.push_back(core.coreIndex);
                topo.lpECoreMask |= core.affinityMask;
            }
        }
        ptr += info->Size;
    }

    topo.totalPhysicalCores = static_cast<int>(topo.cores.size());

    for (const auto& c : topo.cacheInstances) {
        if (c.level == 1 && c.type == "Data") topo.totalL1dKB += c.sizeKB;
        if (c.level == 1 && c.type == "Instruction") topo.totalL1iKB += c.sizeKB;
        if (c.level == 2) topo.totalL2KB += c.sizeKB;
        if (c.level == 3) topo.totalL3KB += c.sizeKB;
    }

    return topo;
}

// NOTE: printTopology now accepts an std::ostream to enable saving to a file
inline void printTopology(const SystemTopology& topo, std::ostream& out) {
    out << "=================== SYSTEM & HARDWARE IDENTIFICATION ===================\n";
    out << "CPU Brand String:       " << topo.cpuBrand << "\n";
    out << "CPU Vendor:             " << topo.cpuVendor << "\n";
    out << "Architecture Signature: Family " << topo.cpuVersion.family 
        << ", Model " << topo.cpuVersion.model 
        << ", Stepping " << topo.cpuVersion.stepping << "\n";
    out << "Hybrid Architecture:    " << (topo.cpuVersion.isHybrid ? "Yes (Intel Hybrid Tech)" : "No") << "\n";
    out << "Physical Sockets:       " << topo.socketCount << "\n";
    out << "NUMA Nodes:             " << topo.numaNodeCount << "\n\n";

    out << "=================== CORE & THREAD TOPOLOGY SUMMARY ===================\n";
    out << std::left << std::setw(26) << "Total Physical Cores:" << topo.totalPhysicalCores << "\n";
    out << std::left << std::setw(26) << "Total Logical Processors:" << topo.totalLogicalProcessors << "\n";
    
    out << std::left << std::setw(16) << "P-Cores:" << std::setw(4) << topo.pCoreIndices.size() 
        << " | Combined Mask: 0x" << std::hex << std::setw(6) << topo.pCoreMask << std::dec 
        << " | Threads: " << formatProcIds(maskToLogicalProcIds(topo.pCoreMask)) << "\n";
        
    out << std::left << std::setw(16) << "E-Cores:" << std::setw(4) << topo.eCoreIndices.size() 
        << " | Combined Mask: 0x" << std::hex << std::setw(6) << topo.eCoreMask << std::dec 
        << " | Threads: " << formatProcIds(maskToLogicalProcIds(topo.eCoreMask)) << "\n";
        
    out << std::left << std::setw(16) << "LP E-Cores:" << std::setw(4) << topo.lpECoreIndices.size() 
        << " | Combined Mask: 0x" << std::hex << std::setw(6) << topo.lpECoreMask << std::dec 
        << " | Threads: " << formatProcIds(maskToLogicalProcIds(topo.lpECoreMask)) << "\n\n";

    out << "================ CACHE HARDWARE AGGREGATION (PHYSICAL TOTALS) ================\n";
    out << "Total L1 Data Cache:        " << topo.totalL1dKB << " KB\n";
    out << "Total L1 Instruction Cache: " << topo.totalL1iKB << " KB\n";
    out << "Total Combined L1 Cache:    " << (topo.totalL1dKB + topo.totalL1iKB) << " KB (" 
        << static_cast<double>(topo.totalL1dKB + topo.totalL1iKB) / 1024.0 << " MB)\n";
    out << "Total L2 Cache:             " << topo.totalL2KB << " KB (" 
        << topo.totalL2KB / 1024.0 << " MB)\n";
    out << "Total L3 Cache (Shared):    " << topo.totalL3KB << " KB (" 
        << topo.totalL3KB / 1024.0 << " MB)\n\n";

    out << "================ PER-CORE DETAILED TOPOLOGY BREAKDOWN ================\n";
    // Fixed alignment by increasing logical thread column width
    out << std::left 
        << std::setw(6)  << "Core#" 
        << std::setw(11) << "Type" 
        << std::setw(8)  << "EffCls"
        << std::setw(6)  << "SMT" 
        << std::setw(20) << "Logical Thread(s)" 
        << std::setw(11) << "L1d (KB)" 
        << std::setw(11) << "L1i (KB)" 
        << std::setw(10) << "L2 (KB)" 
        << std::setw(12) << "L3 Access" 
        << std::setw(12) << "Mask (Hex)" << "\n";
    out << std::string(105, '-') << "\n";
    
    for (const auto& core : topo.cores) {
        std::string typeStr;
        switch (core.type) {
            case CoreType::P_CORE: typeStr = "P-Core"; break;
            case CoreType::E_CORE: typeStr = "E-Core"; break;
            case CoreType::LP_E_CORE: typeStr = "LP E-Core"; break;
        }
        
        out << std::left 
            << std::setw(6)  << core.coreIndex 
            << std::setw(11) << typeStr 
            << std::setw(8)  << static_cast<int>(core.efficiencyClass)
            << std::setw(6)  << (core.hasSMT ? "Yes" : "No") 
            << std::setw(20) << formatProcIds(core.logicalProcIds)
            << std::setw(11) << core.l1dSizeKB 
            << std::setw(11) << core.l1iSizeKB 
            << std::setw(10) << core.l2SizeKB 
            << std::setw(12) << (core.hasL3 ? "Yes" : "No") 
            << "0x" << std::hex << core.affinityMask << std::dec << "\n";
    }

    out << "\n================ PHYSICAL CACHE BLOCK INSTANCES =================\n";
    out << std::left 
        << std::setw(6)  << "Lvl" 
        << std::setw(13) << "Type" 
        << std::setw(10) << "Size" 
        << std::setw(12) << "Line Size" 
        << std::setw(16) << "Associativity" 
        << std::setw(28) << "Shared Logical Threads" << "\n";
    out << std::string(85, '-') << "\n";

    for (const auto& c : topo.cacheInstances) {
        std::string sizeStr = std::to_string(c.sizeKB) + " KB";
        std::string lineStr = std::to_string(c.lineSizeBytes) + " B";
        
        out << std::left 
            << std::setw(6)  << ("L" + std::to_string(c.level))
            << std::setw(13) << c.type 
            << std::setw(10) << sizeStr 
            << std::setw(12) << lineStr 
            << std::setw(16) << formatAssociativity(c.associativity) 
            << formatProcIds(c.logicalProcIds) << "\n";
    }
}

inline bool pinThreadToMask(DWORD_PTR mask) {
    if (mask == 0) return false;
    HANDLE thread = GetCurrentThread();
    DWORD_PTR prevMask = SetThreadAffinityMask(thread, mask);
    return prevMask != 0;
}

inline bool pinThreadToCoreType(const SystemTopology& topo, CoreType type) {
    DWORD_PTR mask = 0;
    switch (type) {
        case CoreType::P_CORE: mask = topo.pCoreMask; break;
        case CoreType::E_CORE: mask = topo.eCoreMask; break;
        case CoreType::LP_E_CORE: mask = topo.lpECoreMask; break;
    }
    return pinThreadToMask(mask);
}

inline CoreType getCurrentCoreType(const SystemTopology& topo) {
    PROCESSOR_NUMBER procNum;
    GetCurrentProcessorNumberEx(&procNum);
    DWORD_PTR currentCoreMask = (DWORD_PTR)1 << procNum.Number;

    if ((currentCoreMask & topo.pCoreMask) != 0) return CoreType::P_CORE;
    if ((currentCoreMask & topo.lpECoreMask) != 0) return CoreType::LP_E_CORE;
    return CoreType::E_CORE;
}