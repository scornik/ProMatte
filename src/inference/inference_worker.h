#pragma once
#include <atomic>
#include <functional>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "inference/frame_types.h"
#include "inference/segmentation_backend.h"
#include "performance/performance_controller.h"
#include "postprocessing/matte_refine.h"
#include "temporal/temporal_stabilizer.h"

namespace promatte {

enum class WorkerState { Stopped, Idle, Initializing, Running, Error, NoModel };

struct WorkerConfig {
	BackendConfig backend;
	ModelDescriptor model;
	std::string modelPath;
	// Cheaper model used when even the lowest tier of `model` is too slow (Auto only).
	ModelDescriptor fallbackModel;
	std::string fallbackModelPath;
	// Better model tried when the current one has plenty of headroom (Auto only).
	ModelDescriptor upgradeModel;
	std::string upgradeModelPath;
	bool modelAutoSelected = false;
	QualityMode quality = QualityMode::Auto;
	int manualTier = -1; // >= 0 overrides quality mode (developer setting)
	uint32_t sourceWidth = 0;
	uint32_t sourceHeight = 0;
	double sourceFps = 30.0;
	TemporalParams temporal;
	RefineParams refine;
	double minIntervalMs = 0; // throttle: minimum time between inferences
};

struct WorkerStatus {
	WorkerState state = WorkerState::Stopped;
	std::string error;
	BackendCapabilities backend;
	std::string modelId;
	std::string modelName;
	uint32_t aiWidth = 0;
	uint32_t aiHeight = 0;
	int tier = 0;
	std::string tierLabel;
	float downsampleRatio = 1.f;
	bool recurrent = false;
	bool providesForeground = false;
	bool usingFallbackModel = false;
};

// Dedicated inference thread with a single-slot "latest frame wins" mailbox.
//
//   render thread ---submitFrame()---> [slot] ---> worker: preprocess, model,
//   refine, temporal ---> [matte slot] ---takeMatte()---> render thread
//
// The render thread never blocks: submitting overwrites an unread frame (counted
// as a drop), taking a matte only swaps buffers. Backend initialisation, model
// loading and resolution changes all happen on the worker thread.
class InferenceWorker {
public:
	InferenceWorker();
	~InferenceWorker();

	InferenceWorker(const InferenceWorker &) = delete;
	InferenceWorker &operator=(const InferenceWorker &) = delete;

	void start();
	void stop(); // joins the thread; safe to call multiple times

	// Full (re)configuration; applied asynchronously by the worker thread.
	void setConfig(const WorkerConfig &config);
	// Hot parameters, applied on the next frame without re-initialisation.
	void setTuning(const TemporalParams &temporal, const RefineParams &refine);
	void setSourceInfo(uint32_t width, uint32_t height, double fps);
	void setQualityMode(QualityMode mode, int manualTier);
	// When disabled the worker drops incoming frames and idles (no AI cost).
	void setEnabled(bool enabled);
	// Release the backend/session (e.g. filter hidden for a long time).
	void releaseBackend();

	// --- render-thread side -------------------------------------------------
	// AI resolution the render thread should downscale to right now.
	void desiredResolution(uint32_t &w, uint32_t &h) const;
	// True when the worker has consumed the previous frame and is ready for a new
	// one. The render thread uses this to avoid a GPU readback (a sync point that
	// can queue behind long inference dispatches) for frames nobody will process.
	bool wantsFrame() const;
	// Hands a frame to the worker; `frame` receives the previous slot buffer for reuse.
	void submitFrame(FrameBuffer &frame);
	// Swaps in the newest matte if it is newer than `out.seq`. Returns true if updated.
	bool takeMatte(MatteBuffer &out);
	bool hasMatte() const { return matteAvailable_.load(); }

	WorkerStatus status() const;
	PerfStats stats() const;
	PerformanceController &performance() { return perf_; }
	void resetTemporal();

	// Called (from the worker thread) when a model's cost has been measured, so the
	// caller can persist it: (modelId, deviceKey, costMs, budgetMs, tier).
	using CostObserver = std::function<void(const std::string &, const std::string &, double, double, int)>;
	void setCostObserver(CostObserver observer);
	// Called when the worker switched models by itself; the caller may re-read status().
	using ModelChangeObserver = std::function<void(const std::string &)>;
	void setModelChangeObserver(ModelChangeObserver observer);
	// Called (worker thread) when GPU inference keeps starving OBS rendering even at
	// the cheapest configuration; the owner should reconfigure with the CPU backend.
	using BackendFallbackObserver = std::function<void(const std::string &deviceKey)>;
	void setBackendFallbackObserver(BackendFallbackObserver observer);

private:
	void threadMain();
	bool applyConfigLocked(WorkerConfig cfg);
	bool initializeBackend(WorkerConfig &cfg, std::string &error);
	void updateAiResolution();
	void processOne(FrameBuffer &frame);
	void reportCost(bool force);
	void setState(WorkerState s, const std::string &error = {});

	// thread + control
	std::thread thread_;
	std::atomic<bool> running_{false};
	std::atomic<bool> enabled_{true};
	mutable std::mutex ctrlMutex_;
	std::condition_variable cv_;
	bool configPending_ = false;
	bool releasePending_ = false;
	WorkerConfig pendingConfig_;
	bool tuningPending_ = false;
	TemporalParams pendingTemporal_;
	RefineParams pendingRefine_;
	bool sourcePending_ = false;
	uint32_t pendingSrcW_ = 0, pendingSrcH_ = 0;
	double pendingSrcFps_ = 30;
	bool qualityPending_ = false;
	QualityMode pendingQuality_ = QualityMode::Auto;
	int pendingManualTier_ = -1;
	bool resetTemporalPending_ = false;

	// frame mailbox
	mutable std::mutex frameMutex_;
	FrameBuffer frameSlot_;
	bool frameAvailable_ = false;
	uint64_t nextSeq_ = 1;

	// matte mailbox
	mutable std::mutex matteMutex_;
	MatteBuffer matteSlot_;
	std::atomic<bool> matteAvailable_{false};

	// worker-owned state
	WorkerConfig config_;
	std::unique_ptr<SegmentationBackend> backend_;
	std::vector<float> tensor_;
	std::vector<uint8_t> luma_;
	std::vector<uint8_t> resized_;
	FrameBuffer work_;
	InferenceOutput output_;
	MatteBuffer matteWork_;
	MatteRefiner refiner_;
	TemporalStabilizer temporal_;
	PerformanceController perf_;
	double lastProcessedMs_ = -1;
	int tier_ = 0;
	std::atomic<uint32_t> aiW_{0}, aiH_{0};
	std::atomic<float> ratio_{1.f};

	// status
	mutable std::mutex statusMutex_;
	WorkerStatus status_;
	mutable std::mutex observerMutex_;
	CostObserver costObserver_;
	ModelChangeObserver modelChangeObserver_;
	BackendFallbackObserver backendFallbackObserver_;
	double lastCostReportMs_ = -1;
};

const char *workerStateName(WorkerState s);

} // namespace promatte
