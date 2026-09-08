#include "utils/system_info.h"

#include <mutex>

#ifdef _WIN32
#include <windows.h>
#include <dxgi1_4.h>
#include <psapi.h>
#include <wrl/client.h>
#endif

namespace promatte::sysinfo {

const char *vendorName(uint32_t vendorId)
{
	switch (vendorId) {
	case 0x10DE:
		return "NVIDIA";
	case 0x1002:
		return "AMD";
	case 0x8086:
		return "Intel";
	case 0x1414:
		return "Microsoft";
	default:
		return "Unknown";
	}
}

std::vector<GpuInfo> enumerateGpus()
{
	std::vector<GpuInfo> out;
#ifdef _WIN32
	using Microsoft::WRL::ComPtr;
	ComPtr<IDXGIFactory1> factory;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
		return out;
	for (UINT i = 0;; ++i) {
		ComPtr<IDXGIAdapter1> adapter;
		if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND)
			break;
		DXGI_ADAPTER_DESC1 desc{};
		if (FAILED(adapter->GetDesc1(&desc)))
			continue;
		GpuInfo g;
		char name[256]{};
		WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
		g.name = name;
		g.vendorId = desc.VendorId;
		g.dedicatedVideoMemory = desc.DedicatedVideoMemory;
		g.sharedSystemMemory = desc.SharedSystemMemory;
		g.isSoftware = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
		g.adapterIndex = int(i);
		out.push_back(g);
	}
#endif
	return out;
}

std::string cpuName()
{
#ifdef _WIN32
	HKEY key;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ,
			  &key) == ERROR_SUCCESS) {
		char buf[256]{};
		DWORD size = sizeof(buf);
		DWORD type = 0;
		LONG r = RegQueryValueExA(key, "ProcessorNameString", nullptr, &type, reinterpret_cast<LPBYTE>(buf),
					  &size);
		RegCloseKey(key);
		if (r == ERROR_SUCCESS) {
			std::string s(buf);
			while (!s.empty() && s.back() == ' ')
				s.pop_back();
			return s;
		}
	}
#endif
	return "Unknown CPU";
}

unsigned logicalCores()
{
#ifdef _WIN32
	SYSTEM_INFO si{};
	GetSystemInfo(&si);
	return si.dwNumberOfProcessors;
#else
	return 1;
#endif
}

uint64_t totalPhysicalMemory()
{
#ifdef _WIN32
	MEMORYSTATUSEX ms{};
	ms.dwLength = sizeof(ms);
	if (GlobalMemoryStatusEx(&ms))
		return ms.ullTotalPhys;
#endif
	return 0;
}

uint64_t processWorkingSetBytes()
{
#ifdef _WIN32
	PROCESS_MEMORY_COUNTERS_EX pmc{};
	pmc.cb = sizeof(pmc);
	if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc),
				 sizeof(pmc)))
		return pmc.WorkingSetSize;
#endif
	return 0;
}

uint64_t processVideoMemoryBytes()
{
	uint64_t total = 0;
#ifdef _WIN32
	using Microsoft::WRL::ComPtr;
	ComPtr<IDXGIFactory1> factory;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
		return 0;
	for (UINT i = 0;; ++i) {
		ComPtr<IDXGIAdapter1> adapter;
		if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND)
			break;
		ComPtr<IDXGIAdapter3> adapter3;
		if (FAILED(adapter.As(&adapter3)))
			continue;
		DXGI_QUERY_VIDEO_MEMORY_INFO info{};
		if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
			total += info.CurrentUsage;
	}
#endif
	return total;
}

double processCpuPercent()
{
#ifdef _WIN32
	static std::mutex m;
	static ULONGLONG lastSys = 0, lastProc = 0;
	std::lock_guard<std::mutex> lock(m);
	FILETIME idle, kernel, user, cKernel, cUser, cCreate, cExit;
	if (!GetSystemTimes(&idle, &kernel, &user))
		return 0;
	if (!GetProcessTimes(GetCurrentProcess(), &cCreate, &cExit, &cKernel, &cUser))
		return 0;
	auto toU64 = [](const FILETIME &f) { return (ULONGLONG(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
	ULONGLONG sys = toU64(kernel) + toU64(user);
	ULONGLONG proc = toU64(cKernel) + toU64(cUser);
	double pct = 0;
	if (lastSys != 0 && sys > lastSys)
		pct = 100.0 * double(proc - lastProc) / double(sys - lastSys);
	lastSys = sys;
	lastProc = proc;
	return pct;
#else
	return 0;
#endif
}

std::string osVersion()
{
#ifdef _WIN32
	HKEY key;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key) ==
	    ERROR_SUCCESS) {
		char product[128]{}, build[64]{}, display[64]{};
		DWORD s1 = sizeof(product), s2 = sizeof(build), s3 = sizeof(display);
		RegQueryValueExA(key, "ProductName", nullptr, nullptr, reinterpret_cast<LPBYTE>(product), &s1);
		RegQueryValueExA(key, "CurrentBuildNumber", nullptr, nullptr, reinterpret_cast<LPBYTE>(build), &s2);
		RegQueryValueExA(key, "DisplayVersion", nullptr, nullptr, reinterpret_cast<LPBYTE>(display), &s3);
		RegCloseKey(key);
		return std::string(product) + " " + display + " (build " + build + ")";
	}
#endif
	return "Unknown OS";
}

} // namespace promatte::sysinfo
