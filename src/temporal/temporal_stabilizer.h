#pragma once
#include <cstdint>
#include <vector>

namespace promatte {

struct TemporalParams {
	bool enabled = true;
	float stability = 0.6f;      // 0..1: how strongly static regions are smoothed
	float motionResponse = 0.6f; // 0..1: how quickly moving regions follow the new matte
	// Models with built-in recurrence (RVM) already stabilise; scale down our smoothing.
	float recurrentScale = 0.5f;
};

// Motion-adaptive temporal smoothing of the matte at AI resolution.
//
//   motion(x)  = blur(|luma_t - luma_t-1|)               (per-pixel, 5x5 box)
//   k(x)       = k_static + (1 - k_static) * f(motion)   (blend factor, 1 = no smoothing)
//   alpha_t(x) = alpha_t-1(x) + k(x) * (alpha_raw(x) - alpha_t-1(x))
//
// Static edges are heavily stabilised (no flicker/crawl); moving edges follow
// immediately (no ghosting). Large disagreements between consecutive raw mattes
// are treated as genuine changes and pass through quickly.
class TemporalStabilizer {
public:
	void setParams(const TemporalParams &p) { params_ = p; }
	const TemporalParams &params() const { return params_; }
	void reset();

	// alpha: w*h floats in [0,1], modified in place. luma: w*h bytes for this frame.
	// dtMs: time since the previous processed frame (used to compensate skipped frames).
	// recurrentModel: true when the model already carries temporal state.
	void process(float *alpha, const uint8_t *luma, uint32_t w, uint32_t h, double dtMs, bool recurrentModel);

	bool hasHistory() const { return hasHistory_; }
	const std::vector<float> &motionMap() const { return motion_; }

private:
	void computeMotion(const uint8_t *luma, uint32_t w, uint32_t h);

	TemporalParams params_;
	std::vector<float> prevAlpha_;
	std::vector<uint8_t> prevLuma_;
	std::vector<float> motion_;
	std::vector<float> tmp_;
	uint32_t w_ = 0, h_ = 0;
	bool hasHistory_ = false;
};

} // namespace promatte
