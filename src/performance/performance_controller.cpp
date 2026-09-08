#include "performance/performance_controller.h"

#include <algorithm>
#include <cstdio>

#include "utils/logging.h"

namespace promatte {

const char *qualityModeName(QualityMode m)
{
	switch (m) {
	case QualityMode::Auto:
		return "auto";
	case QualityMode::Performance:
		return "performance";
	case QualityMode::Balanced:
		return "balanced";
	case QualityMode::Quality:
		return "quality";
	case QualityMode::Ultra:
		return "ultra";
	}
	return "auto";
}

QualityMode qualityModeFromString(const std::string &s)
{
	if (s == "performance")
		return QualityMode::Performance;
	if (s == "balanced")
		return QualityMode::Balanced;
	if (s == "quality")
		return QualityMode::Quality;
	if (s == "ultra")
		return QualityMode::Ultra;
	return QualityMode::Auto;
}

int PerformanceController::tierForMode(QualityMode mode, int tierCount, bool gpuBackend)
{
	if (tierCount <= 1)
		return 0;
	const int last = tierCount - 1;
	switch (mode) {
	case QualityMode::Performance:
		return 0;
	case QualityMode::Balanced:
		return std::clamp((last + 1) / 3, 1, last); // round(last / 3), at least one step above Performance
	case QualityMode::Quality:
		return std::min(last, (2 * last + 2) / 3); // ceil(2 * last / 3)
	case QualityMode::Ultra:
		return last;
	case QualityMode::Auto:
		// Always start at the cheapest tier and climb once measurements show
		// headroom. Starting high on a weak GPU produces multi-hundred-millisecond
		// inference dispatches that stall the compositor before any feedback exists.
		(void)gpuBackend;
		return 0;
	}
	return 0;
}

void PerformanceController::configure(QualityMode mode, int tierCount, double sourceFps, bool gpuBackend)
{
	std::lock_guard<std::mutex> lock(m_);
	mode_ = mode;
	tierCount_ = std::max(tierCount, 1);
	gpu_ = gpuBackend;
	sourceFps_ = sourceFps > 1 ? sourceFps : 30;
	// AI frame budget: match the source up to 30 fps; below that, aim for 20 fps AI
	// on GPU and 15 fps on CPU so the matte still feels live.
	double targetFps = std::clamp(sourceFps_, 15.0, 30.0);
	if (!gpu_)
		targetFps = std::min(targetFps, 20.0);
	budgetMs_ = 1000.0 / targetFps;
	tier_ = tierForMode(mode_, tierCount_, gpu_);
	ceiling_ = tierCount_ - 1;
	pendingChange_ = false;
	overBudgetSince_ = underBudgetSince_ = -1;
	lastDowngradeAt_ = -1;
	degraded_ = false;
	fallbackPending_ = false;
	upgradePending_ = false;
	backendFallbackPending_ = false;
	stuckSince_ = -1;
	comfortableSince_ = -1;
	renderPressureSince_ = -1;
	renderEma_.reset();
	configuredAt_ = Stopwatch::nowMs();
	inferenceEma_.reset();
	preEma_.reset();
	postEma_.reset();
	totalEma_.reset();
	message_.clear();
}

void PerformanceController::setMode(QualityMode mode)
{
	std::lock_guard<std::mutex> lock(m_);
	if (mode == mode_)
		return;
	mode_ = mode;
	int t = tierForMode(mode_, tierCount_, gpu_);
	if (t != tier_) {
		pendingTier_ = t;
		pendingChange_ = true;
	}
	ceiling_ = tierCount_ - 1;
	degraded_ = false;
	overBudgetSince_ = underBudgetSince_ = -1;
	configuredAt_ = Stopwatch::nowMs();
}

QualityMode PerformanceController::mode() const
{
	std::lock_guard<std::mutex> lock(m_);
	return mode_;
}

void PerformanceController::forceTier(int tier)
{
	std::lock_guard<std::mutex> lock(m_);
	tier = std::clamp(tier, 0, tierCount_ - 1);
	if (tier != tier_) {
		pendingTier_ = tier;
		pendingChange_ = true;
	}
}

void PerformanceController::recordInference(double inferenceMs, double preMs, double postMs, double totalMs)
{
	std::lock_guard<std::mutex> lock(m_);
	inferenceEma_.add(inferenceMs);
	preEma_.add(preMs);
	postEma_.add(postMs);
	totalEma_.add(totalMs);
	++processed_;
	double now = Stopwatch::nowMs();
	aiRate_.tick(now);
	evaluateLocked(now);
}

void PerformanceController::recordSubmit(bool overwroteUnread)
{
	std::lock_guard<std::mutex> lock(m_);
	++submitted_;
	if (overwroteUnread)
		++dropped_;
	submitRate_.tick(Stopwatch::nowMs());
}

void PerformanceController::recordRender(double ms)
{
	std::lock_guard<std::mutex> lock(m_);
	renderEma_.add(ms);
	// GPU inference competes with OBS' own rendering. If the filter's render-thread
	// time grows past a quarter of the frame budget the AI work is stealing GPU
	// time from the compositor, which the user sees as OBS lag - treat it as being
	// over budget even when the inference itself looks affordable.
	if (gpu_ && renderEma_.count() > 20 && renderEma_.value() > budgetMs_ * 0.25) {
		double now = Stopwatch::nowMs();
		if (renderPressureSince_ < 0)
			renderPressureSince_ = now;
	} else {
		renderPressureSince_ = -1;
	}
}

void PerformanceController::recordQueueDepth(int depth)
{
	std::lock_guard<std::mutex> lock(m_);
	queueDepth_ = depth;
}

bool PerformanceController::pollBackendFallback()
{
	std::lock_guard<std::mutex> lock(m_);
	if (!backendFallbackPending_)
		return false;
	backendFallbackPending_ = false;
	backendFallbackUsed_ = true;
	return true;
}

bool PerformanceController::pollModelUpgrade()
{
	std::lock_guard<std::mutex> lock(m_);
	if (!upgradePending_)
		return false;
	upgradePending_ = false;
	upgradeUsed_ = true;
	return true;
}

bool PerformanceController::measuredCost(double &costMs, double &budgetMs, int &tier) const
{
	std::lock_guard<std::mutex> lock(m_);
	if (inferenceEma_.count() < 8)
		return false;
	costMs = totalEma_.initialized() ? totalEma_.value() : inferenceEma_.value();
	budgetMs = budgetMs_;
	tier = tier_;
	return true;
}

bool PerformanceController::pollModelFallback()
{
	std::lock_guard<std::mutex> lock(m_);
	if (!fallbackPending_)
		return false;
	fallbackPending_ = false;
	fallbackUsed_ = true;
	return true;
}

int PerformanceController::currentTier() const
{
	std::lock_guard<std::mutex> lock(m_);
	return pendingChange_ ? pendingTier_ : tier_;
}

double PerformanceController::budgetMs() const
{
	std::lock_guard<std::mutex> lock(m_);
	return budgetMs_;
}

bool PerformanceController::pollTierChange(int &newTier)
{
	std::lock_guard<std::mutex> lock(m_);
	if (!pendingChange_)
		return false;
	pendingChange_ = false;
	tier_ = pendingTier_;
	newTier = tier_;
	inferenceEma_.reset();
	totalEma_.reset();
	overBudgetSince_ = underBudgetSince_ = -1;
	configuredAt_ = Stopwatch::nowMs();
	return true;
}

void PerformanceController::evaluateLocked(double now)
{
	if (mode_ != QualityMode::Auto || pendingChange_)
		return;
	// Fast path: a model that is dramatically too slow (e.g. a heavy matting network
	// on an entry-level GPU) is stepped down immediately instead of after the normal
	// hysteresis - one such inference can stall the whole GPU for hundreds of ms.
	if (inferenceEma_.count() >= 3 && totalEma_.value() > budgetMs_ * 3.0) {
		if (tier_ > 0) {
			pendingTier_ = tier_ - 1;
			pendingChange_ = true;
			ceiling_ = std::min(ceiling_, tier_ - 1);
			lastDowngradeAt_ = now;
			degraded_ = true;
			overBudgetSince_ = stuckSince_ = -1;
			char buf[192];
			std::snprintf(buf, sizeof(buf), "Performance limited - reducing AI resolution (%.0f ms > %.0f ms budget)",
				      totalEma_.value(), budgetMs_);
			message_ = buf;
			PM_LOG_INFO("%s", message_.c_str());
			return;
		}
		if (!fallbackUsed_) {
			fallbackPending_ = true;
			degraded_ = true;
			stuckSince_ = -1;
			message_ = "Performance limited - switching to a faster model";
			PM_LOG_INFO("%s (%.0f ms > %.0f ms budget)", message_.c_str(), totalEma_.value(), budgetMs_);
			return;
		}
	}
	if (tierCount_ <= 1 && fallbackUsed_)
		return;
	if (now - configuredAt_ < warmupMs || inferenceEma_.count() < 10)
		return;
	if (tierCount_ <= 1 && tier_ == 0 && fallbackUsed_)
		return;
	// The worker's whole per-frame cost (not just the model) must fit the budget.
	const double cost = totalEma_.initialized() ? totalEma_.value() : inferenceEma_.value();
	// Sustained render-thread pressure (GPU inference starving the compositor)
	// counts as being over budget even if the inference time alone looks fine.
	const bool renderPressure = renderPressureSince_ >= 0 && now - renderPressureSince_ >= downgradeAfterMs;
	// The GPU keeps starving the compositor although we are already at the
	// cheapest tier (and there is nothing cheaper to switch to): inference has to
	// leave this GPU. Reported once; the caller decides (CPU backend).
	if (gpu_ && tier_ == 0 && !backendFallbackUsed_ && renderPressureSince_ >= 0 &&
	    now - renderPressureSince_ >= downgradeAfterMs * 3) {
		backendFallbackPending_ = true;
		renderPressureSince_ = -1;
		degraded_ = true;
		message_ = "GPU contention with OBS rendering - moving AI processing to the CPU";
		PM_LOG_INFO("%s (render %.1f ms, budget %.0f ms)", message_.c_str(), renderEma_.value(), budgetMs_);
		return;
	}

	if (cost > budgetMs_ * 1.15 || renderPressure) {
		underBudgetSince_ = -1;
		// Lowest tier and still far over budget: ask for a cheaper model (once).
		if (tier_ == 0 && !fallbackUsed_ && cost > budgetMs_ * 1.6) {
			if (stuckSince_ < 0)
				stuckSince_ = now;
			else if (now - stuckSince_ >= downgradeAfterMs * 2) {
				fallbackPending_ = true;
				stuckSince_ = -1;
				degraded_ = true;
				message_ = "Performance limited - switching to a faster model";
				PM_LOG_INFO("%s (cost %.0f ms, budget %.0f ms)", message_.c_str(), cost, budgetMs_);
			}
		}
		if (overBudgetSince_ < 0)
			overBudgetSince_ = now;
		else if (now - overBudgetSince_ >= downgradeAfterMs && tier_ > 0) {
			pendingTier_ = tier_ - 1;
			pendingChange_ = true;
			ceiling_ = std::min(ceiling_, tier_ - 1);
			lastDowngradeAt_ = now;
			degraded_ = true;
			overBudgetSince_ = -1;
			renderPressureSince_ = -1;
			char buf[192];
			if (renderPressure)
				std::snprintf(buf, sizeof(buf),
					      "Performance limited - reducing AI resolution (GPU contention: %.1f ms render time)",
					      renderEma_.value());
			else
				std::snprintf(buf, sizeof(buf),
					      "Performance limited - reducing AI resolution (%.0f ms > %.0f ms budget)", cost,
					      budgetMs_);
			message_ = buf;
			PM_LOG_INFO("%s", message_.c_str());
		}
	} else if (cost < budgetMs_ * 0.55) {
		overBudgetSince_ = -1;
		stuckSince_ = -1;
		// Relax the ceiling after a long stable period.
		if (lastDowngradeAt_ >= 0 && now - lastDowngradeAt_ > ceilingRelaxMs)
			ceiling_ = tierCount_ - 1;
		if (underBudgetSince_ < 0)
			underBudgetSince_ = now;
		else if (now - underBudgetSince_ >= upgradeAfterMs && tier_ < ceiling_) {
			pendingTier_ = tier_ + 1;
			pendingChange_ = true;
			underBudgetSince_ = -1;
			message_ = "Headroom available - increasing AI resolution";
			PM_LOG_INFO("%s (tier %d, %.0f ms of %.0f ms budget)", message_.c_str(), pendingTier_, cost, budgetMs_);
		}
		// Comfortable at the highest tier for a long time: a better model may fit.
		if (tier_ >= tierCount_ - 1 && !upgradeUsed_ && !fallbackUsed_ && cost < budgetMs_ * 0.4) {
			if (comfortableSince_ < 0)
				comfortableSince_ = now;
			else if (now - comfortableSince_ >= upgradeAfterMs * 1.5) {
				upgradePending_ = true;
				comfortableSince_ = -1;
				message_ = "Headroom available - trying a higher quality model";
				PM_LOG_INFO("%s (%.0f ms of %.0f ms budget)", message_.c_str(), cost, budgetMs_);
			}
		} else {
			comfortableSince_ = -1;
		}
	} else {
		overBudgetSince_ = underBudgetSince_ = -1;
		stuckSince_ = -1;
	}
}

PerfStats PerformanceController::snapshot() const
{
	std::lock_guard<std::mutex> lock(m_);
	PerfStats s;
	s.inferenceMs = inferenceEma_.value();
	s.preprocessMs = preEma_.value();
	s.postprocessMs = postEma_.value();
	s.totalMs = totalEma_.value();
	double now = Stopwatch::nowMs();
	const_cast<RateCounter &>(aiRate_).idle(now);
	const_cast<RateCounter &>(submitRate_).idle(now);
	s.aiFps = aiRate_.rate();
	s.submitFps = submitRate_.rate();
	s.renderMs = renderEma_.value();
	s.framesSubmitted = submitted_;
	s.framesProcessed = processed_;
	s.framesDropped = dropped_;
	s.dropRatio = submitted_ ? double(dropped_) / double(submitted_) : 0.0;
	s.queueDepth = queueDepth_;
	s.tierIndex = tier_;
	s.degraded = degraded_;
	s.message = message_;
	return s;
}

void PerformanceController::resetStats()
{
	std::lock_guard<std::mutex> lock(m_);
	submitted_ = processed_ = dropped_ = 0;
	aiRate_.reset();
	submitRate_.reset();
	inferenceEma_.reset();
	preEma_.reset();
	postEma_.reset();
	totalEma_.reset();
	renderEma_.reset();
	message_.clear();
}

} // namespace promatte
