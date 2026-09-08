#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include <obs-module.h>

#include "inference/frame_types.h"
#include "inference/inference_worker.h"
#include "obs/background_source.h"
#include "obs/settings.h"
#include "rendering/debug_overlay.h"
#include "performance/capability_store.h"
#include "rendering/gpu_pipeline.h"

namespace promatte {

class ModelManager;

// Per-filter-instance state. Created in filter_create, destroyed in filter_destroy.
struct FilterContext {
	obs_source_t *source = nullptr;
	FilterSettings settings;
	std::mutex settingsMutex;

	std::unique_ptr<InferenceWorker> worker;
	GpuPipeline gpu;
	BackgroundSource background;
	DebugOverlay overlay;
	bool gpuReady = false;
	std::string gpuError;

	FrameBuffer frame; // reusable buffer handed to the worker
	MatteBuffer matte; // latest matte taken from the worker
	uint32_t lastW = 0, lastH = 0;
	double lastFps = 0;
	uint64_t lastRenderNs = 0;
	uint64_t lastFrameTime = 0; // obs_get_video_frame_time of the last processed frame
	uint64_t lastOverlayNs = 0;
	std::atomic<bool> workerEnabled{true};
	bool bypassLogged = false;
	std::string resolvedModelId; // model actually configured on the worker
	std::string configuredBackend;
	std::string renderAdapterName; // GPU OBS renders on (gs_get_device_name)
	std::string deviceKey;         // capability-store key for the active backend
	std::atomic<bool> modelChangedByWorker{false};
	std::atomic<bool> forceCpuBackend{false}; // set when GPU inference starved OBS rendering

	std::string statusText(bool multiline) const;
	void configureWorker();
	void applyTuning();
};

void registerFilter();
ModelManager &modelManager();
CapabilityStore &capabilityStore();
const char *moduleDataPath(const char *file, std::string &storage);
bool developerModeEnv();

} // namespace promatte
