#include <doctest.h>

#include <vector>

#include "preprocessing/preprocess.h"

using namespace promatte;

TEST_CASE("bgraToTensor NCHW RGB with normalisation")
{
	// one pixel: B=0, G=128, R=255
	uint8_t px[4] = {0, 128, 255, 255};
	PreprocessOptions o;
	o.layout = TensorLayout::NCHW;
	o.order = ChannelOrder::RGB;
	o.mean[0] = o.mean[1] = o.mean[2] = 0.5f;
	o.std[0] = o.std[1] = o.std[2] = 0.5f;
	float out[3];
	bgraToTensor(px, 1, 1, 4, o, out);
	CHECK(out[0] == doctest::Approx(1.0f));                    // R
	CHECK(out[1] == doctest::Approx((128 / 255.f - 0.5f) * 2)); // G
	CHECK(out[2] == doctest::Approx(-1.0f));                   // B
}

TEST_CASE("bgraToTensor NHWC BGR keeps interleaved order")
{
	uint8_t px[8] = {10, 20, 30, 255, 40, 50, 60, 255};
	PreprocessOptions o;
	o.layout = TensorLayout::NHWC;
	o.order = ChannelOrder::BGR;
	float out[6];
	bgraToTensor(px, 2, 1, 8, o, out);
	CHECK(out[0] == doctest::Approx(10 / 255.f));
	CHECK(out[1] == doctest::Approx(20 / 255.f));
	CHECK(out[2] == doctest::Approx(30 / 255.f));
	CHECK(out[3] == doctest::Approx(40 / 255.f));
}

TEST_CASE("luma conversion")
{
	uint8_t white[4] = {255, 255, 255, 255}, black[4] = {0, 0, 0, 255};
	uint8_t l;
	bgraToLuma(white, 1, 1, 4, &l);
	CHECK(l >= 254);
	bgraToLuma(black, 1, 1, 4, &l);
	CHECK(l == 0);
}

TEST_CASE("resizeBgra box downscale averages")
{
	// 2x2 -> 1x1
	uint8_t src[16] = {0, 0, 0, 255, 100, 100, 100, 255, 200, 200, 200, 255, 100, 100, 100, 255};
	uint8_t dst[4];
	resizeBgra(src, 2, 2, 8, dst, 1, 1);
	CHECK(dst[0] == 100);
	CHECK(dst[3] == 255);
}

TEST_CASE("resizeBgra upscale is bilinear-ish and in range")
{
	uint8_t src[8] = {0, 0, 0, 255, 200, 200, 200, 255};
	std::vector<uint8_t> dst(4 * 4);
	resizeBgra(src, 2, 1, 8, dst.data(), 4, 1);
	CHECK(dst[0] <= dst[4]);
	CHECK(dst[4] <= dst[8]);
	CHECK(dst[8] <= dst[12]);
}

TEST_CASE("chooseAiResolution keeps aspect and alignment for dynamic models")
{
	uint32_t w, h;
	chooseAiResolution(1920, 1080, 512, 288, false, 32, w, h);
	CHECK(w % 32 == 0);
	CHECK(h % 32 == 0);
	CHECK(double(w) / h == doctest::Approx(16.0 / 9.0).epsilon(0.15));
	CHECK(w * h <= 512 * 288 * 1.3);
	CHECK(w * h >= 512 * 288 * 0.7);

	// portrait source
	chooseAiResolution(720, 1280, 512, 288, false, 32, w, h);
	CHECK(h > w);

	// fixed model ignores the source
	chooseAiResolution(1920, 1080, 256, 144, true, 1, w, h);
	CHECK(w == 256);
	CHECK(h == 144);

	// never upscale beyond source
	chooseAiResolution(320, 180, 1280, 704, false, 32, w, h);
	CHECK(w <= 320);
	CHECK(h <= 180);
}
