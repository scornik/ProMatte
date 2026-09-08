#include <doctest.h>

#include <thread>

#include "performance/performance_controller.h"

using namespace promatte;

TEST_CASE("tier mapping for manual quality modes")
{
	CHECK(PerformanceController::tierForMode(QualityMode::Performance, 4, true) == 0);
	CHECK(PerformanceController::tierForMode(QualityMode::Balanced, 4, true) == 1);
	CHECK(PerformanceController::tierForMode(QualityMode::Quality, 4, true) == 2);
	CHECK(PerformanceController::tierForMode(QualityMode::Ultra, 4, true) == 3);
	CHECK(PerformanceController::tierForMode(QualityMode::Ultra, 1, true) == 0);
	CHECK(PerformanceController::tierForMode(QualityMode::Balanced, 1, false) == 0);
	CHECK(PerformanceController::tierForMode(QualityMode::Auto, 4, true) == 0);
	CHECK(PerformanceController::tierForMode(QualityMode::Auto, 4, false) == 0);
	CHECK(PerformanceController::tierForMode(QualityMode::Quality, 3, true) == 2);
}

TEST_CASE("auto mode steps down when over budget and back up with hysteresis")
{
	PerformanceController pc;
	pc.warmupMs = 0;
	pc.downgradeAfterMs = 30;
	pc.upgradeAfterMs = 30;
	pc.ceilingRelaxMs = 1500;
	pc.configure(QualityMode::Auto, 4, 30.0, true);
	CHECK(pc.currentTier() == 0);
	pc.forceTier(1); // start from the middle so a downgrade is possible
	int t0;
	pc.pollTierChange(t0);
	const double budget = pc.budgetMs();
	CHECK(budget == doctest::Approx(1000.0 / 30.0));

	// Heavily over budget for a while -> downgrade to tier 0.
	int t = -1;
	for (int i = 0; i < 40 && !pc.pollTierChange(t); ++i) {
		pc.recordInference(budget * 2, 0.5, 0.5, budget * 2 + 1);
		std::this_thread::sleep_for(std::chrono::milliseconds(3));
	}
	CHECK(t == 0);
	CHECK(pc.snapshot().degraded);

	// Fast now, but the ceiling stays at 0 until it relaxes (~1.5 s).
	t = -1;
	auto start = std::chrono::steady_clock::now();
	while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(400) && !pc.pollTierChange(t)) {
		pc.recordInference(budget * 0.2, 0.2, 0.2, budget * 0.2 + 0.4);
		std::this_thread::sleep_for(std::chrono::milliseconds(3));
	}
	CHECK(t == -1);

	// After the ceiling relaxes an upgrade is allowed.
	std::this_thread::sleep_for(std::chrono::milliseconds(1300));
	for (int i = 0; i < 200 && !pc.pollTierChange(t); ++i) {
		pc.recordInference(budget * 0.2, 0.2, 0.2, budget * 0.2 + 0.4);
		std::this_thread::sleep_for(std::chrono::milliseconds(3));
	}
	CHECK(t == 1);
}

TEST_CASE("a hopelessly slow model is stepped down immediately, then swapped out")
{
	PerformanceController pc;
	pc.warmupMs = 0;
	pc.configure(QualityMode::Auto, 2, 30.0, true);
	pc.forceTier(1);
	int t = -1;
	pc.pollTierChange(t);
	const double budget = pc.budgetMs();
	// 5x over budget: the fast path must react within a handful of frames.
	for (int i = 0; i < 5 && !pc.pollTierChange(t); ++i)
		pc.recordInference(budget * 5, 1, 1, budget * 5 + 2);
	CHECK(t == 0);
	CHECK_FALSE(pc.pollModelFallback());
	for (int i = 0; i < 6 && !pc.pollModelFallback(); ++i)
		pc.recordInference(budget * 5, 1, 1, budget * 5 + 2);
	CHECK(pc.snapshot().degraded);
	// The fallback is offered only once.
	for (int i = 0; i < 10; ++i)
		pc.recordInference(budget * 5, 1, 1, budget * 5 + 2);
	CHECK_FALSE(pc.pollModelFallback());
}

TEST_CASE("manual modes never adapt")
{
	PerformanceController pc;
	pc.warmupMs = 0;
	pc.downgradeAfterMs = 1;
	pc.configure(QualityMode::Ultra, 4, 30.0, true);
	CHECK(pc.currentTier() == 3);
	int t = -1;
	for (int i = 0; i < 30; ++i) {
		pc.recordInference(500, 1, 1, 502);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	CHECK_FALSE(pc.pollTierChange(t));
	CHECK(pc.currentTier() == 3);
}

TEST_CASE("statistics accumulate drops and rates")
{
	PerformanceController pc;
	pc.configure(QualityMode::Auto, 2, 30.0, false);
	pc.recordSubmit(false);
	pc.recordSubmit(true);
	pc.recordSubmit(true);
	pc.recordInference(10, 1, 1, 12);
	pc.recordRender(0.5);
	auto s = pc.snapshot();
	CHECK(s.framesSubmitted == 3);
	CHECK(s.framesDropped == 2);
	CHECK(s.dropRatio == doctest::Approx(2.0 / 3.0));
	CHECK(s.framesProcessed == 1);
	CHECK(s.inferenceMs == doctest::Approx(10));
	CHECK(s.renderMs == doctest::Approx(0.5));
	pc.resetStats();
	CHECK(pc.snapshot().framesSubmitted == 0);
}

TEST_CASE("mode switch applies pending tier")
{
	PerformanceController pc;
	pc.configure(QualityMode::Auto, 4, 30.0, true);
	pc.setMode(QualityMode::Ultra);
	int t = -1;
	CHECK(pc.pollTierChange(t));
	CHECK(t == 3);
	pc.forceTier(0);
	CHECK(pc.pollTierChange(t));
	CHECK(t == 0);
	CHECK_FALSE(pc.pollTierChange(t));
}
