#pragma once
#include <cstdint>
#include <mutex>
#include <string>

#include "utils/timer.h"

namespace promatte {

enum class QualityMode { Auto = 0, Performance, Balanced, Quality, Ultra };

struct PerfStats {
	double inferenceMs = 0;   // model run only (EMA)
	double preprocessMs = 0;  // EMA
	double postprocessMs = 0; // refine + temporal (EMA)
	double totalMs = 0;       // worker time per frame (EMA)
	double aiFps = 0;         // processed mattes per second
	double renderMs = 0;      // GPU-side filter render time (CPU-measured, EMA)
	double submitFps = 0;     // frames offered to the worker per second
	double dropRatio = 0;     // fraction of submitted frames overwritten before processing
	uint64_t framesSubmitted = 0;
	uint64_t framesProcessed = 0;
	uint64_t framesDropped = 0;
	int queueDepth = 0; // 0 or 1 (single-slot latest-frame-wins)
	int tierIndex = 0;
	std::string tierLabel;
	bool degraded = false; // controller stepped down from the requested quality
	std::string message;   // human readable status line
};

// Adaptive quality control. Owns nothing but numbers; thread-safe.
//
// Auto mode picks a starting tier from the backend type and then walks the tier
// ladder with hysteresis:
//   - inference EMA > budget * 1.15 for `downgradeAfterMs`  -> step down
//   - inference EMA < budget * 0.55 for `upgradeAfterMs`    -> step up (bounded by ceiling)
// Every downgrade lowers a ceiling that only relaxes after `ceilingRelaxMs`, so the
// controller cannot oscillate between two tiers.
class PerformanceController {
public:
	void configure(QualityMode mode, int tierCount, double sourceFps, bool gpuBackend);
	void setMode(QualityMode mode);
	QualityMode mode() const;

	void recordInference(double inferenceMs, double preMs, double postMs, double totalMs);
	void recordSubmit(bool overwroteUnread);
	void recordRender(double ms);
	void recordQueueDepth(int depth);

	int currentTier() const;
	// Returns true (and the new tier) at most once per change; call from the worker loop.
	bool pollTierChange(int &newTier);
	void forceTier(int tier);
	// True once (per configure) when the lowest tier is still far over budget in
	// Auto mode: the caller should switch to a cheaper model if one exists.
	bool pollModelFallback();
	// True once when the model has run comfortably inside its budget at the top
	// tier for a while: the caller may try a better model.
	bool pollModelUpgrade();
	// True once when GPU inference keeps starving the compositor even at the
	// cheapest tier/model: the caller should move inference to the CPU.
	bool pollBackendFallback();
	// Measured worker cost per frame (EMA) and the current budget, for the
	// capability store. Returns false until enough samples exist.
	bool measuredCost(double &costMs, double &budgetMs, int &tier) const;

	double budgetMs() const;
	PerfStats snapshot() const;
	void resetStats();

	static int tierForMode(QualityMode mode, int tierCount, bool gpuBackend);

	// Tunables (public for tests)
	double downgradeAfterMs = 2000;
	double upgradeAfterMs = 6000;
	double ceilingRelaxMs = 90000;
	double warmupMs = 1500;

private:
	mutable std::mutex m_;
	QualityMode mode_ = QualityMode::Auto;
	int tierCount_ = 1;
	int tier_ = 0;
	int ceiling_ = 0;
	bool gpu_ = false;
	double sourceFps_ = 30;
	double budgetMs_ = 33.3;
	bool pendingChange_ = false;
	int pendingTier_ = 0;
	double overBudgetSince_ = -1;
	double underBudgetSince_ = -1;
	double lastDowngradeAt_ = -1;
	double configuredAt_ = 0;
	bool degraded_ = false;
	bool fallbackPending_ = false;
	bool fallbackUsed_ = false;
	double stuckSince_ = -1;
	double renderPressureSince_ = -1;
	bool upgradePending_ = false;
	bool backendFallbackPending_ = false;
	bool backendFallbackUsed_ = false;
	bool upgradeUsed_ = false;
	double comfortableSince_ = -1;

	Ema inferenceEma_{0.15};
	Ema preEma_{0.15};
	Ema postEma_{0.15};
	Ema totalEma_{0.15};
	Ema renderEma_{0.1};
	RateCounter aiRate_;
	RateCounter submitRate_;
	uint64_t submitted_ = 0, processed_ = 0, dropped_ = 0;
	int queueDepth_ = 0;
	std::string message_;

	void evaluateLocked(double nowMs);
};

const char *qualityModeName(QualityMode m);
QualityMode qualityModeFromString(const std::string &s);

} // namespace promatte
