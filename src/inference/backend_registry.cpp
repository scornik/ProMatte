#include "inference/backend_registry.h"

#include <mutex>

#include "backends/onnxruntime_backend.h"
#include "utils/logging.h"
#include "utils/system_info.h"

namespace promatte {

namespace {

std::vector<BackendCapabilities> probe()
{
	std::vector<BackendCapabilities> out;
	auto providers = OnnxRuntimeBackend::availableProviders();
	auto has = [&](const char *name) {
		for (const auto &p : providers)
			if (p == name)
				return true;
		return false;
	};
	auto gpus = sysinfo::enumerateGpus();
	std::string firstGpu;
	bool anyHardwareGpu = false;
	for (const auto &g : gpus) {
		if (!g.isSoftware) {
			anyHardwareGpu = true;
			if (firstGpu.empty())
				firstGpu = g.name;
		}
	}

	auto add = [&](BackendKind kind, const char *provider, bool gpu, const std::string &device) {
		BackendCapabilities c;
		c.kind = kind;
		c.name = backendKindName(kind);
		c.isGpu = gpu;
		c.available = has(provider) && (!gpu || anyHardwareGpu || kind == BackendKind::CoreML);
		c.deviceName = device;
		out.push_back(c);
	};
	add(BackendKind::TensorRT, "TensorrtExecutionProvider", true, firstGpu);
	add(BackendKind::CUDA, "CUDAExecutionProvider", true, firstGpu);
	add(BackendKind::DirectML, "DmlExecutionProvider", true, firstGpu);
	add(BackendKind::CoreML, "CoreMLExecutionProvider", true, "Apple");
	add(BackendKind::OpenVINO, "OpenVINOExecutionProvider", true, firstGpu);
	add(BackendKind::CPU, "CPUExecutionProvider", false, sysinfo::cpuName());

	std::string summary;
	for (const auto &c : out)
		if (c.available)
			summary += (summary.empty() ? "" : ", ") + c.name;
	PM_LOG_INFO("available inference backends: %s", summary.c_str());
	return out;
}

} // namespace

const std::vector<BackendCapabilities> &enumerateBackends()
{
	static std::vector<BackendCapabilities> cache;
	static std::once_flag once;
	std::call_once(once, [] { cache = probe(); });
	return cache;
}

bool backendAvailable(BackendKind kind)
{
	for (const auto &c : enumerateBackends())
		if (c.kind == kind)
			return c.available;
	return false;
}

BackendKind resolveAutoBackend()
{
	// Order = preference. The list from probe() is already in preference order.
	for (const auto &c : enumerateBackends())
		if (c.available)
			return c.kind;
	return BackendKind::CPU;
}

int preferredGpuAdapter(const std::string &preferName)
{
	auto gpus = sysinfo::enumerateGpus();
	if (!preferName.empty()) {
		for (const auto &g : gpus)
			if (!g.isSoftware && g.name == preferName)
				return g.adapterIndex;
	}
	int best = -1;
	uint64_t bestMem = 0;
	for (const auto &g : gpus) {
		if (g.isSoftware)
			continue;
		// Prefer discrete GPUs (dedicated memory); ties broken by enumeration order.
		if (best < 0 || g.dedicatedVideoMemory > bestMem) {
			best = g.adapterIndex;
			bestMem = g.dedicatedVideoMemory;
		}
	}
	return best < 0 ? 0 : best;
}

std::unique_ptr<SegmentationBackend> createBackend(BackendKind kind)
{
	if (kind == BackendKind::Auto)
		kind = resolveAutoBackend();
	return std::make_unique<OnnxRuntimeBackend>(kind);
}

} // namespace promatte
