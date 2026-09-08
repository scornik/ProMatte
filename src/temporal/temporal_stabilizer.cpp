#include "temporal/temporal_stabilizer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace promatte {

void TemporalStabilizer::reset()
{
	hasHistory_ = false;
	w_ = h_ = 0;
}

void TemporalStabilizer::computeMotion(const uint8_t *luma, uint32_t w, uint32_t h)
{
	const size_t n = size_t(w) * h;
	motion_.resize(n);
	tmp_.resize(n);
	// |diff| normalised to [0,1]
	for (size_t i = 0; i < n; ++i)
		tmp_[i] = std::abs(int(luma[i]) - int(prevLuma_[i])) * (1.f / 255.f);
	// Separable 5x5 box blur (radius 2) to make the motion map robust to sensor noise.
	const int r = 2;
	// horizontal pass tmp_ -> motion_
	for (uint32_t y = 0; y < h; ++y) {
		const float *row = tmp_.data() + size_t(y) * w;
		float *out = motion_.data() + size_t(y) * w;
		for (uint32_t x = 0; x < w; ++x) {
			float acc = 0.f;
			int cnt = 0;
			for (int dx = -r; dx <= r; ++dx) {
				int xx = int(x) + dx;
				if (xx < 0 || xx >= int(w))
					continue;
				acc += row[xx];
				++cnt;
			}
			out[x] = acc / float(cnt);
		}
	}
	// vertical pass motion_ -> tmp_
	for (uint32_t y = 0; y < h; ++y) {
		float *out = tmp_.data() + size_t(y) * w;
		for (uint32_t x = 0; x < w; ++x) {
			float acc = 0.f;
			int cnt = 0;
			for (int dy = -r; dy <= r; ++dy) {
				int yy = int(y) + dy;
				if (yy < 0 || yy >= int(h))
					continue;
				acc += motion_[size_t(yy) * w + x];
				++cnt;
			}
			out[x] = acc / float(cnt);
		}
	}
	motion_.swap(tmp_);
}

void TemporalStabilizer::process(float *alpha, const uint8_t *luma, uint32_t w, uint32_t h, double dtMs,
				 bool recurrentModel)
{
	const size_t n = size_t(w) * h;
	if (w != w_ || h != h_ || !hasHistory_ || prevAlpha_.size() != n) {
		prevAlpha_.assign(alpha, alpha + n);
		prevLuma_.assign(luma, luma + n);
		motion_.assign(n, 0.f);
		w_ = w;
		h_ = h;
		hasHistory_ = true;
		return;
	}
	if (!params_.enabled) {
		std::copy_n(alpha, n, prevAlpha_.data());
		std::copy_n(luma, n, prevLuma_.data());
		return;
	}

	computeMotion(luma, w, h);

	float stability = std::clamp(params_.stability, 0.f, 1.f);
	if (recurrentModel)
		stability *= std::clamp(params_.recurrentScale, 0.f, 1.f);
	// k_static: blend factor in fully static areas. stability 0 -> 1.0 (off), 1 -> 0.04.
	const float kStatic = std::clamp(1.f - stability * 0.96f, 0.04f, 1.f);
	// Motion threshold: higher motionResponse => smaller luma change needed to unlock.
	const float motionResponse = std::clamp(params_.motionResponse, 0.f, 1.f);
	const float motionLo = 0.006f;
	const float motionHi = motionLo + (0.18f - 0.15f * motionResponse); // 0.18 .. 0.03
	const float invRange = 1.f / std::max(motionHi - motionLo, 1e-4f);

	// Frame-time compensation: if frames were skipped, converge proportionally faster.
	const double frames = std::clamp(dtMs / 33.333, 1.0, 8.0);

	for (size_t i = 0; i < n; ++i) {
		float m = std::clamp((motion_[i] - motionLo) * invRange, 0.f, 1.f);
		m = m * m * (3.f - 2.f * m); // smoothstep
		float k = kStatic + (1.f - kStatic) * m;
		// Large disagreement = genuine change (e.g. arm entering the frame): let it through.
		float prev = prevAlpha_[i];
		float cur = alpha[i];
		float diff = std::abs(cur - prev);
		if (diff > 0.6f)
			k = std::max(k, 0.75f);
		else if (diff > 0.35f)
			k = std::max(k, 0.4f);
		if (frames > 1.0)
			k = 1.f - float(std::pow(1.0 - k, frames));
		float out = prev + k * (cur - prev);
		alpha[i] = out;
		prevAlpha_[i] = out;
	}
	std::copy_n(luma, n, prevLuma_.data());
}

} // namespace promatte
