#include "image_io.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace promatte::bench {

namespace {
void skipWs(std::FILE *f)
{
	int c;
	while ((c = std::fgetc(f)) != EOF) {
		if (c == '#') {
			while (c != '\n' && c != EOF)
				c = std::fgetc(f);
		} else if (!std::isspace(c)) {
			std::ungetc(c, f);
			return;
		}
	}
}
} // namespace

bool loadPpm(const std::string &path, Image &img)
{
	std::FILE *f = std::fopen(path.c_str(), "rb");
	if (!f)
		return false;
	char magic[3] = {0, 0, 0};
	if (std::fread(magic, 1, 2, f) != 2 || magic[0] != 'P' || (magic[1] != '6' && magic[1] != '5')) {
		std::fclose(f);
		return false;
	}
	const int channels = magic[1] == '6' ? 3 : 1;
	unsigned w = 0, h = 0, maxv = 0;
	skipWs(f);
	if (std::fscanf(f, "%u", &w) != 1)
		return std::fclose(f), false;
	skipWs(f);
	if (std::fscanf(f, "%u", &h) != 1)
		return std::fclose(f), false;
	skipWs(f);
	if (std::fscanf(f, "%u", &maxv) != 1)
		return std::fclose(f), false;
	std::fgetc(f); // single whitespace
	std::vector<uint8_t> raw(size_t(w) * h * channels);
	if (std::fread(raw.data(), 1, raw.size(), f) != raw.size()) {
		std::fclose(f);
		return false;
	}
	std::fclose(f);
	img.width = w;
	img.height = h;
	img.bgra.resize(size_t(w) * h * 4);
	for (size_t i = 0; i < size_t(w) * h; ++i) {
		uint8_t r = raw[i * channels], g = channels == 3 ? raw[i * 3 + 1] : r, b = channels == 3 ? raw[i * 3 + 2] : r;
		img.bgra[i * 4 + 0] = b;
		img.bgra[i * 4 + 1] = g;
		img.bgra[i * 4 + 2] = r;
		img.bgra[i * 4 + 3] = 255;
	}
	return true;
}

bool savePpm(const std::string &path, const Image &img)
{
	std::FILE *f = std::fopen(path.c_str(), "wb");
	if (!f)
		return false;
	std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
	std::vector<uint8_t> row(size_t(img.width) * 3);
	for (uint32_t y = 0; y < img.height; ++y) {
		for (uint32_t x = 0; x < img.width; ++x) {
			const uint8_t *p = &img.bgra[(size_t(y) * img.width + x) * 4];
			row[x * 3] = p[2];
			row[x * 3 + 1] = p[1];
			row[x * 3 + 2] = p[0];
		}
		std::fwrite(row.data(), 1, row.size(), f);
	}
	std::fclose(f);
	return true;
}

bool savePgm(const std::string &path, const std::vector<uint8_t> &gray, uint32_t w, uint32_t h)
{
	std::FILE *f = std::fopen(path.c_str(), "wb");
	if (!f)
		return false;
	std::fprintf(f, "P5\n%u %u\n255\n", w, h);
	std::fwrite(gray.data(), 1, size_t(w) * h, f);
	std::fclose(f);
	return true;
}

namespace {
uint32_t crc32(const uint8_t *data, size_t len, uint32_t crc = 0)
{
	static uint32_t table[256];
	static bool init = false;
	if (!init) {
		for (uint32_t n = 0; n < 256; ++n) {
			uint32_t c = n;
			for (int k = 0; k < 8; ++k)
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[n] = c;
		}
		init = true;
	}
	crc ^= 0xFFFFFFFFu;
	for (size_t i = 0; i < len; ++i)
		crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFu;
}
uint32_t adler32(const uint8_t *data, size_t len)
{
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < len; ++i) {
		a = (a + data[i]) % 65521;
		b = (b + a) % 65521;
	}
	return (b << 16) | a;
}
void put32(std::vector<uint8_t> &v, uint32_t x)
{
	v.push_back(uint8_t(x >> 24));
	v.push_back(uint8_t(x >> 16));
	v.push_back(uint8_t(x >> 8));
	v.push_back(uint8_t(x));
}
void chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data)
{
	put32(out, uint32_t(data.size()));
	std::vector<uint8_t> td(type, type + 4);
	td.insert(td.end(), data.begin(), data.end());
	out.insert(out.end(), td.begin(), td.end());
	put32(out, crc32(td.data(), td.size()));
}
} // namespace

bool savePng(const std::string &path, const Image &img)
{
	// Raw scanlines with filter byte 0, stored in uncompressed deflate blocks.
	std::vector<uint8_t> raw;
	raw.reserve((size_t(img.width) * 4 + 1) * img.height);
	for (uint32_t y = 0; y < img.height; ++y) {
		raw.push_back(0);
		for (uint32_t x = 0; x < img.width; ++x) {
			const uint8_t *p = &img.bgra[(size_t(y) * img.width + x) * 4];
			raw.push_back(p[2]);
			raw.push_back(p[1]);
			raw.push_back(p[0]);
			raw.push_back(p[3]);
		}
	}
	std::vector<uint8_t> z;
	z.push_back(0x78);
	z.push_back(0x01);
	size_t pos = 0;
	while (pos < raw.size()) {
		size_t n = std::min<size_t>(65535, raw.size() - pos);
		bool last = pos + n == raw.size();
		z.push_back(last ? 1 : 0);
		z.push_back(uint8_t(n & 0xFF));
		z.push_back(uint8_t(n >> 8));
		z.push_back(uint8_t(~n & 0xFF));
		z.push_back(uint8_t((~n >> 8) & 0xFF));
		z.insert(z.end(), raw.begin() + long(pos), raw.begin() + long(pos + n));
		pos += n;
	}
	put32(z, adler32(raw.data(), raw.size()));

	std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
	std::vector<uint8_t> ihdr;
	put32(ihdr, img.width);
	put32(ihdr, img.height);
	ihdr.push_back(8); // bit depth
	ihdr.push_back(6); // RGBA
	ihdr.push_back(0);
	ihdr.push_back(0);
	ihdr.push_back(0);
	chunk(out, "IHDR", ihdr);
	chunk(out, "IDAT", z);
	chunk(out, "IEND", {});
	std::FILE *f = std::fopen(path.c_str(), "wb");
	if (!f)
		return false;
	std::fwrite(out.data(), 1, out.size(), f);
	std::fclose(f);
	return true;
}

Image synthesizeFrame(uint32_t w, uint32_t h, uint32_t seed, float motion)
{
	Image img;
	img.width = w;
	img.height = h;
	img.bgra.resize(size_t(w) * h * 4);
	uint32_t rng = seed * 2654435761u + 12345u;
	auto rnd = [&]() {
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		return rng;
	};
	const float cx = w * (0.5f + 0.15f * std::sin(motion)), cy = h * 0.55f;
	const float rx = w * 0.14f, ry = h * 0.42f;
	for (uint32_t y = 0; y < h; ++y) {
		for (uint32_t x = 0; x < w; ++x) {
			uint8_t *p = &img.bgra[(size_t(y) * w + x) * 4];
			float dx = (x - cx) / rx, dy = (y - cy) / ry;
			bool person = dx * dx + dy * dy < 1.f;
			int noise = int(rnd() % 17) - 8;
			if (person) {
				p[0] = uint8_t(std::clamp(90 + noise, 0, 255));
				p[1] = uint8_t(std::clamp(120 + noise, 0, 255));
				p[2] = uint8_t(std::clamp(190 + noise, 0, 255));
			} else {
				int g = int(140 + 60.f * float(y) / h);
				p[0] = uint8_t(std::clamp(g + 40 + noise, 0, 255));
				p[1] = uint8_t(std::clamp(g + 10 + noise, 0, 255));
				p[2] = uint8_t(std::clamp(g - 20 + noise, 0, 255));
			}
			p[3] = 255;
		}
	}
	return img;
}

} // namespace promatte::bench
