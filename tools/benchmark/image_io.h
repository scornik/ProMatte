#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace promatte::bench {

struct Image {
	uint32_t width = 0, height = 0;
	std::vector<uint8_t> bgra;
};

// Minimal PPM/PGM (binary P6/P5) reader/writer plus a PNG writer (uncompressed
// deflate) so the benchmark has zero image-library dependencies.
bool loadPpm(const std::string &path, Image &img);
bool savePpm(const std::string &path, const Image &img);
bool savePgm(const std::string &path, const std::vector<uint8_t> &gray, uint32_t w, uint32_t h);
bool savePng(const std::string &path, const Image &img);

// Synthetic test frame: gradient background with a "person" ellipse + noise,
// deterministic per seed. Only used when no real frames are supplied.
Image synthesizeFrame(uint32_t w, uint32_t h, uint32_t seed, float motion);

} // namespace promatte::bench
