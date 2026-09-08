#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace promatte::sysinfo {

struct GpuInfo {
	std::string name;
	uint32_t vendorId = 0; // 0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel
	uint64_t dedicatedVideoMemory = 0;
	uint64_t sharedSystemMemory = 0;
	bool isSoftware = false;
	int adapterIndex = -1;
};

std::vector<GpuInfo> enumerateGpus();
std::string cpuName();
unsigned logicalCores();
uint64_t totalPhysicalMemory();
uint64_t processWorkingSetBytes();
// Local (dedicated) video memory used by this process across all adapters; 0 if unavailable.
uint64_t processVideoMemoryBytes();
// Percentage of total CPU capacity used by this process since the previous call.
double processCpuPercent();
std::string osVersion();

const char *vendorName(uint32_t vendorId);

} // namespace promatte::sysinfo
