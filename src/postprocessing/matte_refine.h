#pragma once
#include <cstdint>
#include <vector>

namespace promatte {

struct RefineParams {
	// Foreground/background separation: alpha values are re-mapped through a soft
	// threshold centred on `threshold` with half-width `softness`. Values well below
	// become 0, well above become 1, the band in between keeps a smooth ramp (hair).
	float threshold = 0.5f; // 0.05..0.95
	float softness = 0.2f;  // 0..0.5 (0 = hard cut)
	// Morphological shift in AI-resolution pixels: negative erodes (removes halo),
	// positive dilates (protects thin structures).
	int shiftPixels = 0; // -4..4
	// Remove disconnected foreground blobs smaller than this fraction of the frame.
	float minComponentArea = 0.0f; // 0 = disabled, e.g. 0.01
	// Fill enclosed background holes smaller than this fraction of the frame.
	float maxHoleArea = 0.0f; // 0 = disabled
};

// CPU refinement of the matte at AI resolution (cheap: ~150-500k pixels).
// Full-resolution, edge-aware refinement happens on the GPU afterwards.
class MatteRefiner {
public:
	void process(float *alpha, uint32_t w, uint32_t h, const RefineParams &p);

	// Individual stages (public for unit tests).
	static void applyConfidenceCurve(float *alpha, size_t n, float threshold, float softness);
	void morphShift(float *alpha, uint32_t w, uint32_t h, int shiftPixels);
	// Connected-component filtering on the binarised matte (alpha > 0.5).
	// Returns the number of removed foreground components.
	int filterComponents(float *alpha, uint32_t w, uint32_t h, float minComponentArea, float maxHoleArea);

private:
	std::vector<float> tmp_;
	std::vector<int32_t> labels_;
	std::vector<uint32_t> stack_;
};

} // namespace promatte
