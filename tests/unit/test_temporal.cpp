#include <doctest.h>

#include <cmath>
#include <vector>

#include "temporal/temporal_stabilizer.h"

using namespace promatte;

TEST_CASE("temporal stabiliser suppresses flicker in static regions")
{
	const uint32_t w = 32, h = 32;
	const size_t n = size_t(w) * h;
	std::vector<uint8_t> luma(n, 128); // static scene
	TemporalStabilizer ts;
	TemporalParams p;
	p.stability = 0.8f;
	p.motionResponse = 0.5f;
	ts.setParams(p);

	std::vector<float> a(n, 0.5f);
	ts.process(a.data(), luma.data(), w, h, 33.3, false); // seeds history
	// Now feed an alternating noisy matte around 0.5 and check the output barely moves.
	float maxDev = 0.f;
	for (int i = 0; i < 20; ++i) {
		float v = (i % 2) ? 0.7f : 0.3f;
		std::vector<float> cur(n, v);
		ts.process(cur.data(), luma.data(), w, h, 33.3, false);
		for (float x : cur)
			maxDev = std::max(maxDev, std::abs(x - 0.5f));
	}
	CHECK(maxDev < 0.08f);
}

TEST_CASE("temporal stabiliser follows moving regions quickly")
{
	const uint32_t w = 32, h = 32;
	const size_t n = size_t(w) * h;
	TemporalStabilizer ts;
	TemporalParams p;
	p.stability = 0.8f;
	p.motionResponse = 0.7f;
	ts.setParams(p);
	std::vector<uint8_t> luma(n, 50);
	std::vector<float> a(n, 0.f);
	ts.process(a.data(), luma.data(), w, h, 33.3, false);
	// Big luma change everywhere (motion) + matte flips to 1.
	std::vector<uint8_t> luma2(n, 200);
	std::vector<float> b(n, 1.f);
	ts.process(b.data(), luma2.data(), w, h, 33.3, false);
	CHECK(b[n / 2] > 0.7f);
}

TEST_CASE("large matte disagreement passes through even without motion")
{
	const uint32_t w = 16, h = 16;
	const size_t n = size_t(w) * h;
	TemporalStabilizer ts;
	TemporalParams p;
	p.stability = 1.0f; // maximum smoothing
	ts.setParams(p);
	std::vector<uint8_t> luma(n, 100);
	std::vector<float> a(n, 0.f);
	ts.process(a.data(), luma.data(), w, h, 33.3, false);
	std::vector<float> b(n, 1.f);
	ts.process(b.data(), luma.data(), w, h, 33.3, false);
	CHECK(b[0] >= 0.7f);
}

TEST_CASE("resolution change resets history without crashing")
{
	TemporalStabilizer ts;
	std::vector<uint8_t> l1(16 * 16, 0);
	std::vector<float> a1(16 * 16, 0.5f);
	ts.process(a1.data(), l1.data(), 16, 16, 33.3, false);
	std::vector<uint8_t> l2(8 * 8, 0);
	std::vector<float> a2(8 * 8, 0.9f);
	ts.process(a2.data(), l2.data(), 8, 8, 33.3, false);
	CHECK(a2[0] == doctest::Approx(0.9f)); // first frame at new size passes unchanged
	CHECK(ts.hasHistory());
}

TEST_CASE("disabled stabiliser passes input through")
{
	TemporalStabilizer ts;
	TemporalParams p;
	p.enabled = false;
	ts.setParams(p);
	std::vector<uint8_t> l(4 * 4, 0);
	std::vector<float> a(4 * 4, 0.2f);
	ts.process(a.data(), l.data(), 4, 4, 33.3, false);
	std::vector<float> b(4 * 4, 0.9f);
	ts.process(b.data(), l.data(), 4, 4, 33.3, false);
	CHECK(b[5] == doctest::Approx(0.9f));
}
