#include "preprocessing/preprocess.h"

#include <algorithm>
#include <cmath>

namespace promatte {

void bgraToTensor(const uint8_t *bgra, uint32_t w, uint32_t h, uint32_t stride, const PreprocessOptions &opt, float *out)
{
	const size_t n = size_t(w) * h;
	// Precompute per-channel lookup tables (256 entries) — avoids per-pixel divides.
	float lut[3][256];
	for (int c = 0; c < 3; ++c) {
		float inv = opt.std[c] != 0.f ? 1.f / opt.std[c] : 1.f;
		for (int v = 0; v < 256; ++v)
			lut[c][v] = (float(v) / 255.f - opt.mean[c]) * inv;
	}
	// Source is BGRA: index 0 = B, 1 = G, 2 = R. Map to tensor channel order.
	const int srcIdx[3] = {opt.order == ChannelOrder::RGB ? 2 : 0, 1, opt.order == ChannelOrder::RGB ? 0 : 2};

	if (opt.layout == TensorLayout::NCHW) {
		float *c0 = out, *c1 = out + n, *c2 = out + 2 * n;
		for (uint32_t y = 0; y < h; ++y) {
			const uint8_t *row = bgra + size_t(y) * stride;
			size_t base = size_t(y) * w;
			for (uint32_t x = 0; x < w; ++x) {
				const uint8_t *p = row + size_t(x) * 4;
				c0[base + x] = lut[0][p[srcIdx[0]]];
				c1[base + x] = lut[1][p[srcIdx[1]]];
				c2[base + x] = lut[2][p[srcIdx[2]]];
			}
		}
	} else {
		for (uint32_t y = 0; y < h; ++y) {
			const uint8_t *row = bgra + size_t(y) * stride;
			float *o = out + size_t(y) * w * 3;
			for (uint32_t x = 0; x < w; ++x) {
				const uint8_t *p = row + size_t(x) * 4;
				o[x * 3 + 0] = lut[0][p[srcIdx[0]]];
				o[x * 3 + 1] = lut[1][p[srcIdx[1]]];
				o[x * 3 + 2] = lut[2][p[srcIdx[2]]];
			}
		}
	}
}

void bgraToLuma(const uint8_t *bgra, uint32_t w, uint32_t h, uint32_t stride, uint8_t *luma)
{
	for (uint32_t y = 0; y < h; ++y) {
		const uint8_t *row = bgra + size_t(y) * stride;
		uint8_t *o = luma + size_t(y) * w;
		for (uint32_t x = 0; x < w; ++x) {
			const uint8_t *p = row + size_t(x) * 4;
			// BT.601 integer approximation: (77R + 150G + 29B) >> 8
			o[x] = uint8_t((77u * p[2] + 150u * p[1] + 29u * p[0]) >> 8);
		}
	}
}

void resizeBgra(const uint8_t *src, uint32_t sw, uint32_t sh, uint32_t sstride, uint8_t *dst, uint32_t dw, uint32_t dh)
{
	if (dw == 0 || dh == 0 || sw == 0 || sh == 0)
		return;
	if (dw == sw && dh == sh) {
		for (uint32_t y = 0; y < sh; ++y)
			std::copy_n(src + size_t(y) * sstride, size_t(sw) * 4, dst + size_t(y) * dw * 4);
		return;
	}
	const bool down = dw <= sw && dh <= sh;
	if (down) {
		// Box filter: average all source pixels covering each destination pixel.
		for (uint32_t y = 0; y < dh; ++y) {
			uint32_t y0 = uint32_t(uint64_t(y) * sh / dh);
			uint32_t y1 = std::max(y0 + 1, uint32_t(uint64_t(y + 1) * sh / dh));
			for (uint32_t x = 0; x < dw; ++x) {
				uint32_t x0 = uint32_t(uint64_t(x) * sw / dw);
				uint32_t x1 = std::max(x0 + 1, uint32_t(uint64_t(x + 1) * sw / dw));
				uint32_t acc[4] = {0, 0, 0, 0};
				uint32_t cnt = 0;
				for (uint32_t yy = y0; yy < y1; ++yy) {
					const uint8_t *row = src + size_t(yy) * sstride;
					for (uint32_t xx = x0; xx < x1; ++xx) {
						const uint8_t *p = row + size_t(xx) * 4;
						acc[0] += p[0];
						acc[1] += p[1];
						acc[2] += p[2];
						acc[3] += p[3];
						++cnt;
					}
				}
				uint8_t *o = dst + (size_t(y) * dw + x) * 4;
				for (int c = 0; c < 4; ++c)
					o[c] = uint8_t((acc[c] + cnt / 2) / cnt);
			}
		}
	} else {
		// Bilinear.
		for (uint32_t y = 0; y < dh; ++y) {
			float fy = (float(y) + 0.5f) * float(sh) / float(dh) - 0.5f;
			int y0 = std::clamp(int(std::floor(fy)), 0, int(sh) - 1);
			int y1 = std::min(y0 + 1, int(sh) - 1);
			float wy = std::clamp(fy - float(y0), 0.f, 1.f);
			for (uint32_t x = 0; x < dw; ++x) {
				float fx = (float(x) + 0.5f) * float(sw) / float(dw) - 0.5f;
				int x0 = std::clamp(int(std::floor(fx)), 0, int(sw) - 1);
				int x1 = std::min(x0 + 1, int(sw) - 1);
				float wx = std::clamp(fx - float(x0), 0.f, 1.f);
				const uint8_t *p00 = src + size_t(y0) * sstride + size_t(x0) * 4;
				const uint8_t *p01 = src + size_t(y0) * sstride + size_t(x1) * 4;
				const uint8_t *p10 = src + size_t(y1) * sstride + size_t(x0) * 4;
				const uint8_t *p11 = src + size_t(y1) * sstride + size_t(x1) * 4;
				uint8_t *o = dst + (size_t(y) * dw + x) * 4;
				for (int c = 0; c < 4; ++c) {
					float top = p00[c] + (p01[c] - p00[c]) * wx;
					float bot = p10[c] + (p11[c] - p10[c]) * wx;
					o[c] = uint8_t(std::clamp(top + (bot - top) * wy + 0.5f, 0.f, 255.f));
				}
			}
		}
	}
}

void chooseAiResolution(uint32_t sw, uint32_t sh, uint32_t tierW, uint32_t tierH, bool fixedInput, uint32_t alignment,
			uint32_t &outW, uint32_t &outH)
{
	if (fixedInput || sw == 0 || sh == 0) {
		outW = tierW;
		outH = tierH;
		return;
	}
	// Keep the tier's pixel budget, follow the source aspect ratio.
	double budget = double(tierW) * double(tierH);
	double aspect = double(sw) / double(sh);
	double h = std::sqrt(budget / aspect);
	double w = h * aspect;
	uint32_t al = std::max<uint32_t>(alignment, 1);
	auto align = [al](double v) {
		uint32_t r = uint32_t((v + al / 2.0) / al) * al;
		return std::max<uint32_t>(r, al);
	};
	outW = align(w);
	outH = align(h);
	// Never upscale beyond the source.
	if (outW > sw || outH > sh) {
		outW = std::max<uint32_t>(al, (sw / al) * al);
		outH = std::max<uint32_t>(al, (sh / al) * al);
	}
}

} // namespace promatte
