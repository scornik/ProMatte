#include "utils/system_info.h"

#include <algorithm>
#include <chrono>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#include <dxgi1_4.h>
#include <psapi.h>
#include <wrl/client.h>
#else
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include <unistd.h>
#endif

#ifdef __linux__
#include <filesystem>
#endif

#ifdef __APPLE__
#include <mach/mach.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#endif

namespace promatte::sysinfo {

#ifndef _WIN32
namespace {

// Reads a whole small file (sysfs entries, /proc). Returns false if unreadable.
bool readFile(const std::string &path, std::string &out)
{
	std::ifstream in(path);
	if (!in)
		return false;
	std::ostringstream ss;
	ss << in.rdbuf();
	out = ss.str();
	return true;
}

std::string trim(std::string s)
{
	const char *ws = " \t\r\n";
	size_t b = s.find_first_not_of(ws);
	if (b == std::string::npos)
		return {};
	size_t e = s.find_last_not_of(ws);
	return s.substr(b, e - b + 1);
}

#ifdef __APPLE__
std::string sysctlString(const char *name)
{
	size_t len = 0;
	if (sysctlbyname(name, nullptr, &len, nullptr, 0) != 0 || len == 0)
		return {};
	std::string out(len, '\0');
	if (sysctlbyname(name, out.data(), &len, nullptr, 0) != 0)
		return {};
	while (!out.empty() && out.back() == '\0')
		out.pop_back();
	return out;
}

uint64_t sysctlU64(const char *name)
{
	uint64_t v = 0;
	size_t len = sizeof(v);
	if (sysctlbyname(name, &v, &len, nullptr, 0) != 0)
		return 0;
	return v;
}
#endif

} // namespace
#endif // !_WIN32

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
	case 0x106B:
		return "Apple";
	default:
		return "Unknown";
	}
}

std::vector<GpuInfo> enumerateGpus()
{
	std::vector<GpuInfo> out;
#if defined(_WIN32)
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
#elif defined(__linux__)
	// DRM cards in /sys expose the PCI vendor/device ids. There is no portable
	// way to get a marketing name without a PCI id database, so the name is the
	// vendor plus the device id; amdgpu is the only driver that reports VRAM here.
	std::error_code ec;
	std::vector<std::string> cards;
	for (auto &e : std::filesystem::directory_iterator("/sys/class/drm", ec)) {
		const std::string name = e.path().filename().string();
		// "card0", but not the connector entries like "card0-HDMI-A-1"
		if (name.rfind("card", 0) == 0 && name.find('-') == std::string::npos)
			cards.push_back(e.path().string());
	}
	std::sort(cards.begin(), cards.end());
	int index = 0;
	for (const auto &card : cards) {
		std::string vendorStr, deviceStr;
		if (!readFile(card + "/device/vendor", vendorStr))
			continue;
		readFile(card + "/device/device", deviceStr);
		GpuInfo g;
		g.vendorId = uint32_t(std::strtoul(trim(vendorStr).c_str(), nullptr, 16));
		const std::string dev = trim(deviceStr);
		g.name = std::string(vendorName(g.vendorId)) + " GPU" + (dev.empty() ? "" : " [" + dev + "]");
		std::string vram;
		if (readFile(card + "/device/mem_info_vram_total", vram))
			g.dedicatedVideoMemory = std::strtoull(trim(vram).c_str(), nullptr, 10);
		g.isSoftware = false;
		g.adapterIndex = index++;
		out.push_back(g);
	}
#elif defined(__APPLE__)
	// Apple Silicon has one integrated GPU sharing system memory; Intel Macs are
	// reported the same way. The exact model comes from hw.model (e.g. "Mac14,2").
	GpuInfo g;
	const std::string model = sysctlString("hw.model");
	g.name = model.empty() ? "Apple GPU" : model + " GPU";
	g.vendorId = 0x106B;
	g.sharedSystemMemory = sysctlU64("hw.memsize");
	g.isSoftware = false;
	g.adapterIndex = 0;
	out.push_back(g);
#endif
	return out;
}

std::string cpuName()
{
#if defined(_WIN32)
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
#elif defined(__linux__)
	std::string cpuinfo;
	if (readFile("/proc/cpuinfo", cpuinfo)) {
		std::istringstream in(cpuinfo);
		std::string line;
		while (std::getline(in, line)) {
			// x86 uses "model name", arm64 uses "Model" or "CPU implementer"
			for (const char *key : {"model name", "Model name", "Hardware", "Model"}) {
				if (line.rfind(key, 0) == 0) {
					size_t colon = line.find(':');
					if (colon != std::string::npos)
						return trim(line.substr(colon + 1));
				}
			}
		}
	}
#elif defined(__APPLE__)
	std::string s = sysctlString("machdep.cpu.brand_string");
	if (!s.empty())
		return s;
#endif
	return "Unknown CPU";
}

unsigned logicalCores()
{
#if defined(_WIN32)
	SYSTEM_INFO si{};
	GetSystemInfo(&si);
	return si.dwNumberOfProcessors;
#else
	unsigned n = std::thread::hardware_concurrency();
	if (n == 0) {
		long sc = sysconf(_SC_NPROCESSORS_ONLN);
		n = sc > 0 ? unsigned(sc) : 1u;
	}
	return n;
#endif
}

uint64_t totalPhysicalMemory()
{
#if defined(_WIN32)
	MEMORYSTATUSEX ms{};
	ms.dwLength = sizeof(ms);
	if (GlobalMemoryStatusEx(&ms))
		return ms.ullTotalPhys;
#elif defined(__linux__)
	const long pages = sysconf(_SC_PHYS_PAGES);
	const long pageSize = sysconf(_SC_PAGE_SIZE);
	if (pages > 0 && pageSize > 0)
		return uint64_t(pages) * uint64_t(pageSize);
#elif defined(__APPLE__)
	return sysctlU64("hw.memsize");
#endif
	return 0;
}

uint64_t processWorkingSetBytes()
{
#if defined(_WIN32)
	PROCESS_MEMORY_COUNTERS_EX pmc{};
	pmc.cb = sizeof(pmc);
	if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc),
				 sizeof(pmc)))
		return pmc.WorkingSetSize;
#elif defined(__linux__)
	// /proc/self/statm: size resident shared text lib data dt (in pages)
	std::string statm;
	if (readFile("/proc/self/statm", statm)) {
		unsigned long long size = 0, resident = 0;
		if (std::sscanf(statm.c_str(), "%llu %llu", &size, &resident) == 2) {
			const long pageSize = sysconf(_SC_PAGE_SIZE);
			if (pageSize > 0)
				return uint64_t(resident) * uint64_t(pageSize);
		}
	}
#elif defined(__APPLE__)
	mach_task_basic_info info{};
	mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
	if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) ==
	    KERN_SUCCESS)
		return info.resident_size;
#endif
	return 0;
}

uint64_t processVideoMemoryBytes()
{
	uint64_t total = 0;
#if defined(_WIN32)
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
	// Linux and macOS expose no per-process VRAM figure that works across drivers,
	// so this stays 0 there and the overlay simply omits it.
	return total;
}

double processCpuPercent()
{
#if defined(_WIN32)
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
#elif defined(__linux__)
	// Process jiffies from /proc/self/stat against total jiffies from /proc/stat,
	// which gives the same "percent of the whole machine" figure as Windows.
	static std::mutex m;
	static unsigned long long lastProc = 0, lastTotal = 0;
	std::lock_guard<std::mutex> lock(m);
	std::string stat, procStat;
	if (!readFile("/proc/self/stat", stat) || !readFile("/proc/stat", procStat))
		return 0;
	// utime and stime are fields 14 and 15, after the comm field which may contain spaces.
	const size_t close = stat.rfind(')');
	if (close == std::string::npos)
		return 0;
	unsigned long long utime = 0, stime = 0;
	{
		std::istringstream in(stat.substr(close + 1));
		std::string field;
		for (int i = 0; i < 12 && in >> field; ++i) {
			if (i == 11)
				utime = std::strtoull(field.c_str(), nullptr, 10);
		}
		if (in >> field)
			stime = std::strtoull(field.c_str(), nullptr, 10);
	}
	unsigned long long total = 0;
	{
		std::istringstream in(procStat);
		std::string cpu;
		in >> cpu; // "cpu"
		unsigned long long v = 0;
		while (in >> v)
			total += v;
	}
	const unsigned long long proc = utime + stime;
	double pct = 0;
	if (lastTotal != 0 && total > lastTotal)
		pct = 100.0 * double(proc - lastProc) / double(total - lastTotal);
	lastProc = proc;
	lastTotal = total;
	return pct;
#elif defined(__APPLE__)
	// getrusage against wall-clock time, normalised by core count so the result
	// is a percentage of the whole machine as on the other platforms.
	static std::mutex m;
	static double lastCpuSeconds = 0;
	static std::chrono::steady_clock::time_point lastAt{};
	std::lock_guard<std::mutex> lock(m);
	rusage ru{};
	if (getrusage(RUSAGE_SELF, &ru) != 0)
		return 0;
	const double cpuSeconds = double(ru.ru_utime.tv_sec) + double(ru.ru_utime.tv_usec) / 1e6 +
				  double(ru.ru_stime.tv_sec) + double(ru.ru_stime.tv_usec) / 1e6;
	const auto now = std::chrono::steady_clock::now();
	double pct = 0;
	if (lastAt.time_since_epoch().count() != 0) {
		const double wall = std::chrono::duration<double>(now - lastAt).count();
		const double cores = double(std::max(1u, logicalCores()));
		if (wall > 0)
			pct = 100.0 * (cpuSeconds - lastCpuSeconds) / (wall * cores);
	}
	lastCpuSeconds = cpuSeconds;
	lastAt = now;
	return pct;
#else
	return 0;
#endif
}

std::string osVersion()
{
#if defined(_WIN32)
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
#elif defined(__linux__)
	std::string release;
	if (readFile("/etc/os-release", release)) {
		std::istringstream in(release);
		std::string line;
		while (std::getline(in, line)) {
			if (line.rfind("PRETTY_NAME=", 0) == 0) {
				std::string v = line.substr(12);
				if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
					v = v.substr(1, v.size() - 2);
				return v;
			}
		}
	}
	return "Linux";
#elif defined(__APPLE__)
	const std::string product = sysctlString("kern.osproductversion");
	const std::string kernel = sysctlString("kern.osrelease");
	if (!product.empty())
		return "macOS " + product + (kernel.empty() ? "" : " (Darwin " + kernel + ")");
	return "macOS";
#endif
	return "Unknown OS";
}

} // namespace promatte::sysinfo
