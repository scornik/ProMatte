#include "inference/inference_worker.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

#include "inference/backend_registry.h"
#include "preprocessing/preprocess.h"
#include "utils/logging.h"
#include "utils/timer.h"

namespace promatte {

const char *workerStateName(WorkerState s)
{
	switch (s) {
	case WorkerState::Stopped:
		return "Stopped";
	case WorkerState::Idle:
		return "Idle";
	case WorkerState::Initializing:
		return "Initializing";
	case WorkerState::Running:
		return "Running";
	case WorkerState::Error:
		return "Error";
	case WorkerState::NoModel:
		return "No model";
	}
	return "?";
}

InferenceWorker::InferenceWorker() = default;

InferenceWorker::~InferenceWorker()
{
	stop();
}

void InferenceWorker::start()
{
	if (running_.exchange(true))
		return;
	setState(WorkerState::Idle);
	thread_ = std::thread([this] { threadMain(); });
}

void InferenceWorker::stop()
{
	if (!running_.exchange(false)) {
		if (thread_.joinable())
			thread_.join();
		return;
	}
	if (backend_)
		backend_->cancel(); // abort an in-flight Run() so join is fast
	cv_.notify_all();
	if (thread_.joinable())
		thread_.join();
	backend_.reset();
	setState(WorkerState::Stopped);
}

void InferenceWorker::setState(WorkerState s, const std::string &error)
{
	std::lock_guard<std::mutex> lock(statusMutex_);
	status_.state = s;
	status_.error = error;
}

void InferenceWorker::setConfig(const WorkerConfig &config)
{
	{
		std::lock_guard<std::mutex> lock(ctrlMutex_);
		pendingConfig_ = config;
		configPending_ = true;
		releasePending_ = false;
	}
	cv_.notify_all();
}

void InferenceWorker::setTuning(const TemporalParams &temporal, const RefineParams &refine)
{
	std::lock_guard<std::mutex> lock(ctrlMutex_);
	pendingTemporal_ = temporal;
	pendingRefine_ = refine;
	tuningPending_ = true;
}

void InferenceWorker::setSourceInfo(uint32_t width, uint32_t height, double fps)
{
	std::lock_guard<std::mutex> lock(ctrlMutex_);
	pendingSrcW_ = width;
	pendingSrcH_ = height;
	pendingSrcFps_ = fps;
	sourcePending_ = true;
}

void InferenceWorker::setQualityMode(QualityMode mode, int manualTier)
{
	std::lock_guard<std::mutex> lock(ctrlMutex_);
	pendingQuality_ = mode;
	pendingManualTier_ = manualTier;
	qualityPending_ = true;
}

void InferenceWorker::setEnabled(bool enabled)
{
	enabled_ = enabled;
	if (enabled)
		cv_.notify_all();
}

void InferenceWorker::releaseBackend()
{
	{
		std::lock_guard<std::mutex> lock(ctrlMutex_);
		releasePending_ = true;
	}
	cv_.notify_all();
}

void InferenceWorker::setCostObserver(CostObserver observer)
{
	std::lock_guard<std::mutex> lock(observerMutex_);
	costObserver_ = std::move(observer);
}

void InferenceWorker::setModelChangeObserver(ModelChangeObserver observer)
{
	std::lock_guard<std::mutex> lock(observerMutex_);
	modelChangeObserver_ = std::move(observer);
}

void InferenceWorker::setBackendFallbackObserver(BackendFallbackObserver observer)
{
	std::lock_guard<std::mutex> lock(observerMutex_);
	backendFallbackObserver_ = std::move(observer);
}

void InferenceWorker::resetTemporal()
{
	std::lock_guard<std::mutex> lock(ctrlMutex_);
	resetTemporalPending_ = true;
}

void InferenceWorker::desiredResolution(uint32_t &w, uint32_t &h) const
{
	w = aiW_.load();
	h = aiH_.load();
}

bool InferenceWorker::wantsFrame() const
{
	if (!enabled_.load() || aiW_.load() == 0)
		return false;
	std::lock_guard<std::mutex> lock(frameMutex_);
	return !frameAvailable_;
}

void InferenceWorker::submitFrame(FrameBuffer &frame)
{
	bool overwrote = false;
	{
		std::lock_guard<std::mutex> lock(frameMutex_);
		overwrote = frameAvailable_;
		frame.seq = nextSeq_++;
		frame.timestampMs = Stopwatch::nowMs();
		std::swap(frame, frameSlot_); // caller gets the old buffer back (no allocation churn)
		frameAvailable_ = true;
	}
	perf_.recordSubmit(overwrote);
	perf_.recordQueueDepth(1);
	cv_.notify_one();
}

bool InferenceWorker::takeMatte(MatteBuffer &out)
{
	if (!matteAvailable_.load())
		return false;
	std::lock_guard<std::mutex> lock(matteMutex_);
	if (matteSlot_.seq <= out.seq || matteSlot_.empty())
		return false;
	std::swap(out, matteSlot_);
	matteAvailable_ = false;
	return true;
}

WorkerStatus InferenceWorker::status() const
{
	std::lock_guard<std::mutex> lock(statusMutex_);
	return status_;
}

PerfStats InferenceWorker::stats() const
{
	return perf_.snapshot();
}

void InferenceWorker::updateAiResolution()
{
	const ModelDescriptor &m = config_.model;
	if (m.tiers.empty()) {
		aiW_ = aiH_ = 0;
		return;
	}
	tier_ = std::clamp(tier_, 0, int(m.tiers.size()) - 1);
	const ResolutionTier &t = m.tiers[size_t(tier_)];
	uint32_t w = 0, h = 0;
	chooseAiResolution(config_.sourceWidth, config_.sourceHeight, t.width, t.height, m.fixedInput, m.alignment, w, h);
	aiW_ = w;
	aiH_ = h;
	ratio_ = t.downsampleRatio;
	{
		std::lock_guard<std::mutex> lock(statusMutex_);
		status_.aiWidth = w;
		status_.aiHeight = h;
		status_.tier = tier_;
		status_.tierLabel = t.label;
		status_.downsampleRatio = t.downsampleRatio;
	}
	if (backend_)
		backend_->resetState();
	temporal_.reset();
	PM_LOG_INFO("AI resolution: %ux%u (tier %d '%s', ratio %.3f) for source %ux%u", w, h, tier_, t.label.c_str(),
		    t.downsampleRatio, config_.sourceWidth, config_.sourceHeight);
}

bool InferenceWorker::initializeBackend(WorkerConfig &cfg, std::string &error)
{
	backend_.reset();
	if (cfg.model.id.empty() || cfg.modelPath.empty()) {
		error = "no model selected";
		return false;
	}
	BackendKind kind = cfg.backend.kind;
	if (kind == BackendKind::Auto)
		kind = resolveAutoBackend();
	std::vector<BackendKind> attempts{kind};
	if (cfg.backend.allowFallback) {
		if (kind == BackendKind::TensorRT)
			attempts.push_back(BackendKind::CUDA);
		if (kind != BackendKind::CPU && kind != BackendKind::DirectML && backendAvailable(BackendKind::DirectML))
			attempts.push_back(BackendKind::DirectML);
		if (kind != BackendKind::CPU)
			attempts.push_back(BackendKind::CPU);
	}
	std::string lastError;
	for (BackendKind k : attempts) {
		if (!backendAvailable(k)) {
			lastError = std::string(backendKindName(k)) + " not available";
			continue;
		}
		auto b = createBackend(k);
		std::string err;
		if (b->initialize(cfg.backend, cfg.model, cfg.modelPath, err)) {
			backend_ = std::move(b);
			if (k != kind)
				PM_LOG_WARN("backend %s failed (%s); fell back to %s", backendKindName(kind), lastError.c_str(),
					    backendKindName(k));
			{
				std::lock_guard<std::mutex> lock(statusMutex_);
				status_.backend = backend_->capabilities();
			}
			return true;
		}
		lastError = err;
		PM_LOG_WARN("backend %s init failed: %s", backendKindName(k), err.c_str());
	}
	error = lastError.empty() ? "no inference backend available" : lastError;
	return false;
}

bool InferenceWorker::applyConfigLocked(WorkerConfig cfg)
{
	// ctrlMutex_ is NOT held here (called from the worker thread after copying).
	const bool needBackend = !backend_ || cfg.model.id != config_.model.id || cfg.modelPath != config_.modelPath ||
				 cfg.backend.kind != config_.backend.kind ||
				 cfg.backend.deviceIndex != config_.backend.deviceIndex ||
				 cfg.backend.cpuThreads != config_.backend.cpuThreads;
	config_ = cfg;
	temporal_.setParams(cfg.temporal);
	{
		std::lock_guard<std::mutex> lock(statusMutex_);
		status_.modelId = cfg.model.id;
		status_.modelName = cfg.model.displayName;
		status_.recurrent = cfg.model.recurrent;
		status_.providesForeground = cfg.model.providesForeground;
		status_.usingFallbackModel = false;
	}
	if (needBackend) {
		setState(WorkerState::Initializing);
		std::string err;
		if (!initializeBackend(config_, err)) {
			backend_.reset();
			setState(cfg.modelPath.empty() ? WorkerState::NoModel : WorkerState::Error, err);
			PM_LOG_ERROR("inference initialisation failed: %s", err.c_str());
			aiW_ = aiH_ = 0;
			return false;
		}
	}
	const bool gpu = backend_ && backend_->capabilities().isGpu;
	perf_.configure(cfg.quality, int(cfg.model.tiers.size()), cfg.sourceFps, gpu);
	tier_ = cfg.manualTier >= 0 ? std::clamp(cfg.manualTier, 0, int(cfg.model.tiers.size()) - 1)
				    : perf_.currentTier();
	if (cfg.manualTier >= 0)
		perf_.forceTier(tier_);
	int dummy;
	perf_.pollTierChange(dummy);
	updateAiResolution();
	setState(WorkerState::Running);
	return true;
}

void InferenceWorker::threadMain()
{
#ifdef _WIN32
	// OBS' render and encoder threads must win any contention for the CPU: on
	// low-core machines a CPU-backend inference otherwise starves the compositor.
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
	PM_LOG_DEBUG("inference worker thread started");
	while (running_) {
		// ---- control messages ------------------------------------------------
		bool haveConfig = false, haveTuning = false, haveSource = false, haveQuality = false, doRelease = false,
		     doResetTemporal = false;
		WorkerConfig cfg;
		TemporalParams tp;
		RefineParams rp;
		uint32_t sw = 0, sh = 0;
		double sfps = 30;
		QualityMode qm = QualityMode::Auto;
		int manualTier = -1;
		{
			std::unique_lock<std::mutex> lock(ctrlMutex_);
			bool frameReady = false;
			{
				std::lock_guard<std::mutex> flock(frameMutex_);
				frameReady = frameAvailable_;
			}
			if (!configPending_ && !tuningPending_ && !sourcePending_ && !qualityPending_ && !releasePending_ &&
			    !resetTemporalPending_ && !(frameReady && enabled_)) {
				cv_.wait_for(lock, std::chrono::milliseconds(50));
			}
			if (configPending_) {
				cfg = pendingConfig_;
				configPending_ = false;
				haveConfig = true;
			}
			if (tuningPending_) {
				tp = pendingTemporal_;
				rp = pendingRefine_;
				tuningPending_ = false;
				haveTuning = true;
			}
			if (sourcePending_) {
				sw = pendingSrcW_;
				sh = pendingSrcH_;
				sfps = pendingSrcFps_;
				sourcePending_ = false;
				haveSource = true;
			}
			if (qualityPending_) {
				qm = pendingQuality_;
				manualTier = pendingManualTier_;
				qualityPending_ = false;
				haveQuality = true;
			}
			if (releasePending_) {
				releasePending_ = false;
				doRelease = true;
			}
			if (resetTemporalPending_) {
				resetTemporalPending_ = false;
				doResetTemporal = true;
			}
		}
		if (!running_)
			break;
		if (doRelease) {
			backend_.reset();
			config_.model = ModelDescriptor{};
			config_.modelPath.clear();
			aiW_ = aiH_ = 0;
			setState(WorkerState::Idle);
		}
		if (haveConfig) {
			if (haveSource) {
				cfg.sourceWidth = sw;
				cfg.sourceHeight = sh;
				cfg.sourceFps = sfps;
				haveSource = false;
			}
			if (haveTuning) {
				cfg.temporal = tp;
				cfg.refine = rp;
				haveTuning = false;
			}
			if (haveQuality) {
				cfg.quality = qm;
				cfg.manualTier = manualTier;
				haveQuality = false;
			}
			applyConfigLocked(cfg);
		}
		if (haveTuning) {
			config_.temporal = tp;
			config_.refine = rp;
			temporal_.setParams(tp);
		}
		if (haveSource && (sw != config_.sourceWidth || sh != config_.sourceHeight ||
				   std::abs(sfps - config_.sourceFps) > 0.5)) {
			config_.sourceWidth = sw;
			config_.sourceHeight = sh;
			config_.sourceFps = sfps;
			if (backend_) {
				const bool gpu = backend_->capabilities().isGpu;
				int keepTier = tier_;
				perf_.configure(config_.quality, int(config_.model.tiers.size()), sfps, gpu);
				if (config_.manualTier >= 0)
					perf_.forceTier(config_.manualTier);
				else
					perf_.forceTier(keepTier);
				int t;
				if (perf_.pollTierChange(t))
					tier_ = t;
				updateAiResolution();
			}
		}
		if (haveQuality && backend_) {
			config_.quality = qm;
			config_.manualTier = manualTier;
			perf_.setMode(qm);
			if (manualTier >= 0)
				perf_.forceTier(manualTier);
			int t;
			if (perf_.pollTierChange(t)) {
				tier_ = t;
				updateAiResolution();
			}
		}
		if (doResetTemporal) {
			temporal_.reset();
			if (backend_)
				backend_->resetState();
		}
		// Adaptive tier changes decided by the performance controller.
		{
			int t;
			if (backend_ && perf_.pollTierChange(t) && t != tier_) {
				tier_ = t;
				updateAiResolution();
			}
		}
		// Automatic model changes (Auto mode only).
		if (backend_ && config_.modelAutoSelected) {
			const bool fallback = perf_.pollModelFallback();
			const bool upgrade = !fallback && perf_.pollModelUpgrade();
			if (fallback || upgrade) {
				const ModelDescriptor &target = fallback ? config_.fallbackModel : config_.upgradeModel;
				const std::string &targetPath = fallback ? config_.fallbackModelPath : config_.upgradeModelPath;
				if (!target.id.empty() && target.id != config_.model.id && !targetPath.empty()) {
					// Remember how the outgoing model performed before switching.
					reportCost(true);
					PM_LOG_INFO("switching model '%s' -> '%s' (%s)", config_.model.id.c_str(),
						    target.id.c_str(), fallback ? "too slow" : "headroom available");
					WorkerConfig cfg = config_;
					cfg.model = target;
					cfg.modelPath = targetPath;
					cfg.fallbackModel = ModelDescriptor{};
					cfg.fallbackModelPath.clear();
					cfg.upgradeModel = ModelDescriptor{};
					cfg.upgradeModelPath.clear();
					applyConfigLocked(cfg);
					{
						std::lock_guard<std::mutex> lock(statusMutex_);
						status_.usingFallbackModel = fallback;
					}
					ModelChangeObserver obs;
					{
						std::lock_guard<std::mutex> lock(observerMutex_);
						obs = modelChangeObserver_;
					}
					if (obs)
						obs(target.id);
				}
			}
		}
		reportCost(false);
		// GPU contention that survives every cheaper option: hand over to the owner.
		if (backend_ && backend_->capabilities().isGpu && config_.backend.kind == BackendKind::Auto &&
		    perf_.pollBackendFallback()) {
			const auto caps = backend_->capabilities();
			BackendFallbackObserver obs;
			{
				std::lock_guard<std::mutex> lock(observerMutex_);
				obs = backendFallbackObserver_;
			}
			if (obs)
				obs(caps.deviceName + "|" + caps.name);
		}

		// ---- frame processing --------------------------------------------------
		bool got = false;
		{
			std::lock_guard<std::mutex> lock(frameMutex_);
			if (frameAvailable_) {
				if (enabled_ && backend_) {
					std::swap(work_, frameSlot_);
					got = true;
				}
				frameAvailable_ = false; // drop when disabled / no backend
			}
		}
		perf_.recordQueueDepth(0);
		if (!got)
			continue;
		if (config_.minIntervalMs > 0 && lastProcessedMs_ >= 0) {
			double since = Stopwatch::nowMs() - lastProcessedMs_;
			if (since < config_.minIntervalMs)
				continue; // throttle: skip this frame
		}
		processOne(work_);
	}
	backend_.reset();
	PM_LOG_DEBUG("inference worker thread exiting");
}

void InferenceWorker::reportCost(bool force)
{
	if (!backend_ || config_.model.id.empty())
		return;
	double cost = 0, budget = 0;
	int tier = 0;
	if (!perf_.measuredCost(cost, budget, tier))
		return;
	const double now = Stopwatch::nowMs();
	if (!force && lastCostReportMs_ >= 0 && now - lastCostReportMs_ < 5000)
		return;
	lastCostReportMs_ = now;
	CostObserver obs;
	{
		std::lock_guard<std::mutex> lock(observerMutex_);
		obs = costObserver_;
	}
	if (!obs)
		return;
	const auto caps = backend_->capabilities();
	obs(config_.model.id, caps.deviceName + "|" + caps.name, cost, budget, tier);
}

void InferenceWorker::processOne(FrameBuffer &frame)
{
	if (frame.empty())
		return;
	Stopwatch total;
	const ModelDescriptor &m = config_.model;

	// Fixed-input models must receive exactly their input size; if the render
	// thread has not caught up with a resolution change, resize on the CPU.
	const uint8_t *pixels = frame.bgra.data();
	uint32_t w = frame.width, h = frame.height, stride = frame.stride();
	uint32_t wantW = aiW_.load(), wantH = aiH_.load();
	if (wantW == 0 || wantH == 0)
		return;
	if (w != wantW || h != wantH) {
		resized_.resize(size_t(wantW) * wantH * 4);
		resizeBgra(pixels, w, h, stride, resized_.data(), wantW, wantH);
		pixels = resized_.data();
		w = wantW;
		h = wantH;
		stride = wantW * 4;
	}

	// Preprocess
	Stopwatch sw;
	const size_t n = size_t(w) * h;
	tensor_.resize(n * 3);
	PreprocessOptions po;
	po.layout = m.inputLayout;
	po.order = m.channelOrder;
	std::copy_n(m.mean, 3, po.mean);
	std::copy_n(m.std, 3, po.std);
	bgraToTensor(pixels, w, h, stride, po, tensor_.data());
	luma_.resize(n);
	bgraToLuma(pixels, w, h, stride, luma_.data());
	const double preMs = sw.elapsedMs();

	// Inference
	sw.reset();
	InferenceInput in;
	in.data = tensor_.data();
	in.width = w;
	in.height = h;
	in.downsampleRatio = ratio_.load();
	in.shape = m.inputLayout == TensorLayout::NCHW ? std::vector<int64_t>{1, 3, int64_t(h), int64_t(w)}
						       : std::vector<int64_t>{1, int64_t(h), int64_t(w), 3};
	in.resetState = false;
	std::string err;
	if (!backend_->processFrame(in, output_, err)) {
		if (running_) {
			PM_LOG_ERROR("inference error: %s", err.c_str());
			setState(WorkerState::Error, err);
		}
		return;
	}
	const double infMs = sw.elapsedMs();

	// Post-process: CPU refinement + temporal stabilisation at AI resolution.
	sw.reset();
	refiner_.process(output_.alpha.data(), w, h, config_.refine);
	double dt = lastProcessedMs_ >= 0 ? frame.timestampMs - lastProcessedMs_ : 33.3;
	temporal_.process(output_.alpha.data(), luma_.data(), w, h, dt, m.recurrent);
	lastProcessedMs_ = frame.timestampMs;

	matteWork_.resize(w, h);
	matteWork_.seq = frame.seq;
	matteWork_.hasForeground = !output_.foreground.empty();
	uint8_t *dst = matteWork_.rgba.data();
	if (matteWork_.hasForeground) {
		const float *r = output_.foreground.data();
		const float *g = r + n;
		const float *b = g + n;
		for (size_t i = 0; i < n; ++i) {
			dst[i * 4 + 0] = uint8_t(r[i] * 255.f + 0.5f);
			dst[i * 4 + 1] = uint8_t(g[i] * 255.f + 0.5f);
			dst[i * 4 + 2] = uint8_t(b[i] * 255.f + 0.5f);
			dst[i * 4 + 3] = uint8_t(std::clamp(output_.alpha[i], 0.f, 1.f) * 255.f + 0.5f);
		}
	} else {
		for (size_t i = 0; i < n; ++i) {
			dst[i * 4 + 0] = dst[i * 4 + 1] = dst[i * 4 + 2] = 0;
			dst[i * 4 + 3] = uint8_t(std::clamp(output_.alpha[i], 0.f, 1.f) * 255.f + 0.5f);
		}
	}
	const double postMs = sw.elapsedMs();
	matteWork_.inferenceMs = infMs;
	matteWork_.totalMs = total.elapsedMs();

	{
		std::lock_guard<std::mutex> lock(matteMutex_);
		std::swap(matteSlot_, matteWork_);
		matteAvailable_ = true;
	}
	perf_.recordInference(infMs, preMs, postMs, total.elapsedMs());
	if (status_.state != WorkerState::Running)
		setState(WorkerState::Running);
}

} // namespace promatte
