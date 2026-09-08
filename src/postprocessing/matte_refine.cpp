#include "postprocessing/matte_refine.h"

#include <algorithm>
#include <cmath>

namespace promatte {

void MatteRefiner::process(float *alpha, uint32_t w, uint32_t h, const RefineParams &p)
{
	const size_t n = size_t(w) * h;
	if (n == 0)
		return;
	if (p.minComponentArea > 0.f || p.maxHoleArea > 0.f)
		filterComponents(alpha, w, h, p.minComponentArea, p.maxHoleArea);
	if (p.shiftPixels != 0)
		morphShift(alpha, w, h, p.shiftPixels);
	applyConfidenceCurve(alpha, n, p.threshold, p.softness);
}

void MatteRefiner::applyConfidenceCurve(float *alpha, size_t n, float threshold, float softness)
{
	threshold = std::clamp(threshold, 0.01f, 0.99f);
	softness = std::clamp(softness, 0.0f, 0.5f);
	if (softness < 1e-4f) {
		for (size_t i = 0; i < n; ++i)
			alpha[i] = alpha[i] >= threshold ? 1.f : 0.f;
		return;
	}
	const float lo = threshold - softness;
	const float inv = 1.f / (2.f * softness);
	for (size_t i = 0; i < n; ++i) {
		float t = std::clamp((alpha[i] - lo) * inv, 0.f, 1.f);
		alpha[i] = t * t * (3.f - 2.f * t);
	}
}

void MatteRefiner::morphShift(float *alpha, uint32_t w, uint32_t h, int shiftPixels)
{
	const size_t n = size_t(w) * h;
	tmp_.resize(n);
	const bool dilate = shiftPixels > 0;
	int iterations = std::min(std::abs(shiftPixels), 8);
	for (int it = 0; it < iterations; ++it) {
		// Separable 3x3 min/max: horizontal into tmp_, vertical back into alpha.
		for (uint32_t y = 0; y < h; ++y) {
			const float *row = alpha + size_t(y) * w;
			float *out = tmp_.data() + size_t(y) * w;
			for (uint32_t x = 0; x < w; ++x) {
				float v = row[x];
				if (x > 0)
					v = dilate ? std::max(v, row[x - 1]) : std::min(v, row[x - 1]);
				if (x + 1 < w)
					v = dilate ? std::max(v, row[x + 1]) : std::min(v, row[x + 1]);
				out[x] = v;
			}
		}
		for (uint32_t y = 0; y < h; ++y) {
			float *out = alpha + size_t(y) * w;
			const float *cur = tmp_.data() + size_t(y) * w;
			const float *up = y > 0 ? tmp_.data() + size_t(y - 1) * w : nullptr;
			const float *dn = y + 1 < h ? tmp_.data() + size_t(y + 1) * w : nullptr;
			for (uint32_t x = 0; x < w; ++x) {
				float v = cur[x];
				if (up)
					v = dilate ? std::max(v, up[x]) : std::min(v, up[x]);
				if (dn)
					v = dilate ? std::max(v, dn[x]) : std::min(v, dn[x]);
				out[x] = v;
			}
		}
	}
}

int MatteRefiner::filterComponents(float *alpha, uint32_t w, uint32_t h, float minComponentArea, float maxHoleArea)
{
	const size_t n = size_t(w) * h;
	labels_.assign(n, 0);
	stack_.clear();
	stack_.reserve(n / 4 + 16);

	// Labels: 0 = unvisited. Foreground components get positive ids, background
	// components negative ids. We record area and border contact per component.
	std::vector<uint32_t> areas;
	std::vector<uint8_t> touchesBorder;
	areas.push_back(0); // index 0 unused
	touchesBorder.push_back(0);

	auto isFg = [&](size_t i) { return alpha[i] > 0.5f; };

	int nextId = 1;
	for (size_t start = 0; start < n; ++start) {
		if (labels_[start] != 0)
			continue;
		const bool fg = isFg(start);
		const int id = nextId++;
		areas.push_back(0);
		touchesBorder.push_back(0);
		stack_.push_back(uint32_t(start));
		labels_[start] = fg ? id : -id;
		while (!stack_.empty()) {
			uint32_t i = stack_.back();
			stack_.pop_back();
			++areas[size_t(id)];
			uint32_t x = i % w, y = i / w;
			if (x == 0 || y == 0 || x == w - 1 || y == h - 1)
				touchesBorder[size_t(id)] = 1;
			const uint32_t nb[4] = {x > 0 ? i - 1 : UINT32_MAX, x + 1 < w ? i + 1 : UINT32_MAX,
						y > 0 ? i - w : UINT32_MAX, y + 1 < h ? i + w : UINT32_MAX};
			for (uint32_t j : nb) {
				if (j == UINT32_MAX || labels_[j] != 0 || isFg(j) != fg)
					continue;
				labels_[j] = fg ? id : -id;
				stack_.push_back(j);
			}
		}
	}

	// Decide which components to flip.
	const float total = float(n);
	std::vector<uint8_t> flip(areas.size(), 0);
	int removed = 0;
	// Keep the largest foreground component always (the person).
	int largestFg = 0;
	uint32_t largestArea = 0;
	std::vector<int8_t> sign(areas.size(), 0);
	for (size_t i = 0; i < n; ++i) {
		int l = labels_[i];
		sign[size_t(std::abs(l))] = l > 0 ? 1 : -1;
	}
	for (int id = 1; id < nextId; ++id) {
		if (sign[size_t(id)] > 0 && areas[size_t(id)] > largestArea) {
			largestArea = areas[size_t(id)];
			largestFg = id;
		}
	}
	for (int id = 1; id < nextId; ++id) {
		float frac = float(areas[size_t(id)]) / total;
		if (sign[size_t(id)] > 0) {
			if (minComponentArea > 0.f && id != largestFg && frac < minComponentArea) {
				flip[size_t(id)] = 1;
				++removed;
			}
		} else {
			if (maxHoleArea > 0.f && !touchesBorder[size_t(id)] && frac < maxHoleArea)
				flip[size_t(id)] = 1;
		}
	}
	for (size_t i = 0; i < n; ++i) {
		int l = labels_[i];
		if (flip[size_t(std::abs(l))])
			alpha[i] = l > 0 ? 0.f : 1.f;
	}
	return removed;
}

} // namespace promatte
