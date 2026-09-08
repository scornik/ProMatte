#pragma once
#include <cstdint>
#include <vector>

namespace promatte {

// A CPU-side BGRA8 frame at AI processing resolution (already downscaled on the GPU).
struct FrameBuffer {
	std::vector<uint8_t> bgra; // packed, stride == width * 4
	uint32_t width = 0;
	uint32_t height = 0;
	uint64_t seq = 0;
	double timestampMs = 0;

	void resize(uint32_t w, uint32_t h)
	{
		width = w;
		height = h;
		bgra.resize(size_t(w) * h * 4);
	}
	bool empty() const { return width == 0 || height == 0 || bgra.empty(); }
	uint32_t stride() const { return width * 4; }
};

// Inference result at AI resolution. RGBA8: rgb = predicted clean foreground colour
// (only when the model provides one, e.g. RVM), a = alpha/matte.
struct MatteBuffer {
	std::vector<uint8_t> rgba;
	uint32_t width = 0;
	uint32_t height = 0;
	uint64_t seq = 0;
	bool hasForeground = false;
	double inferenceMs = 0;
	double totalMs = 0;

	void resize(uint32_t w, uint32_t h)
	{
		width = w;
		height = h;
		rgba.resize(size_t(w) * h * 4);
	}
	bool empty() const { return width == 0 || height == 0 || rgba.empty(); }
};

} // namespace promatte
