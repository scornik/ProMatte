#include <doctest.h>

#include <cmath>
#include <vector>

#include "inference/model_descriptor.h"
#include "utils/file_utils.h"

using namespace promatte;

TEST_CASE("shipped manifest parses and is well formed")
{
	std::string text;
	REQUIRE(fs::readTextFile(fs::joinPath(PROMATTE_SOURCE_DIR, "data/models/manifest.json"), text));
	std::vector<ModelDescriptor> models;
	std::string err;
	REQUIRE_MESSAGE(parseModelManifest(text, models, err), err);
	CHECK(models.size() >= 5);
	bool hasRvm = false;
	for (const auto &m : models) {
		CHECK(!m.id.empty());
		CHECK(!m.license.empty());
		CHECK(!m.tiers.empty());
		CHECK(!m.fileName.empty());
		if (m.id == "rvm_mobilenetv3") {
			hasRvm = true;
			CHECK(m.recurrent);
			CHECK(m.providesForeground);
			CHECK(m.tiers.size() == 4);
			CHECK(!m.fixedInput);
		}
		if (m.bundled)
			CHECK(m.downloadUrl.empty());
		else
			CHECK(m.downloadUrl.rfind("https://", 0) == 0);
	}
	CHECK(hasRvm);
}

TEST_CASE("manifest rejects malformed input")
{
	std::vector<ModelDescriptor> models;
	std::string err;
	CHECK_FALSE(parseModelManifest("{not json", models, err));
	CHECK_FALSE(parseModelManifest("{\"models\": [{\"display_name\": \"x\"}]}", models, err));
	CHECK_FALSE(parseModelManifest("{\"models\": [{\"id\": \"x\", \"tiers\": []}]}", models, err));
	CHECK_FALSE(parseModelManifest("{\"models\": [{\"id\": \"x\", \"tiers\": [{\"width\": 1, \"height\": 1}], \"output_kind\": \"bogus\"}]}",
				       models, err));
	CHECK(parseModelManifest("{\"models\": [{\"id\": \"x\", \"tiers\": [{\"width\": 8, \"height\": 8}]}]}", models, err));
	CHECK(models.size() == 1);
	CHECK(models[0].fileName == "x.onnx");
}

TEST_CASE("decodeModelOutput handles every output kind and layout")
{
	ModelDescriptor d;
	std::vector<float> alpha(4);

	SUBCASE("alpha NCHW")
	{
		d.outputKind = OutputKind::Alpha;
		d.outputLayout = TensorLayout::NCHW;
		float data[4] = {0.1f, 0.5f, 1.5f, -1.f};
		CHECK(decodeModelOutput(d, data, {1, 1, 2, 2}, 2, 2, alpha.data()));
		CHECK(alpha[0] == doctest::Approx(0.1f));
		CHECK(alpha[2] == 1.f);
		CHECK(alpha[3] == 0.f);
	}
	SUBCASE("alpha NHWC")
	{
		d.outputKind = OutputKind::Alpha;
		d.outputLayout = TensorLayout::NHWC;
		float data[4] = {0.1f, 0.2f, 0.3f, 0.4f};
		CHECK(decodeModelOutput(d, data, {1, 2, 2, 1}, 2, 2, alpha.data()));
		CHECK(alpha[3] == doctest::Approx(0.4f));
	}
	SUBCASE("sigmoid logit")
	{
		d.outputKind = OutputKind::SigmoidLogit;
		float data[4] = {0.f, 10.f, -10.f, 0.f};
		CHECK(decodeModelOutput(d, data, {1, 1, 2, 2}, 2, 2, alpha.data()));
		CHECK(alpha[0] == doctest::Approx(0.5f));
		CHECK(alpha[1] > 0.99f);
		CHECK(alpha[2] < 0.01f);
	}
	SUBCASE("two class softmax NCHW, foreground = class 1")
	{
		d.outputKind = OutputKind::TwoClassSoftmax;
		d.outputLayout = TensorLayout::NCHW;
		d.foregroundClass = 1;
		// C=2, H=2, W=2 -> class0 plane then class1 plane
		float data[8] = {0.9f, 0.1f, 0.5f, 0.0f, 0.1f, 0.9f, 0.5f, 1.0f};
		CHECK(decodeModelOutput(d, data, {1, 2, 2, 2}, 2, 2, alpha.data()));
		CHECK(alpha[0] == doctest::Approx(0.1f));
		CHECK(alpha[1] == doctest::Approx(0.9f));
		CHECK(alpha[3] == doctest::Approx(1.0f));
	}
	SUBCASE("two class softmax NHWC")
	{
		d.outputKind = OutputKind::TwoClassSoftmax;
		d.outputLayout = TensorLayout::NHWC;
		d.foregroundClass = 1;
		float data[8] = {0.9f, 0.1f, 0.2f, 0.8f, 0.5f, 0.5f, 0.0f, 1.0f};
		CHECK(decodeModelOutput(d, data, {1, 2, 2, 2}, 2, 2, alpha.data()));
		CHECK(alpha[1] == doctest::Approx(0.8f));
	}
	SUBCASE("two class logits")
	{
		d.outputKind = OutputKind::TwoClassLogits;
		d.outputLayout = TensorLayout::NCHW;
		float data[8] = {5.f, -5.f, 0.f, 0.f, -5.f, 5.f, 0.f, 0.f};
		CHECK(decodeModelOutput(d, data, {1, 2, 2, 2}, 2, 2, alpha.data()));
		CHECK(alpha[0] < 0.01f);
		CHECK(alpha[1] > 0.99f);
		CHECK(alpha[2] == doctest::Approx(0.5f));
	}
	SUBCASE("multiclass NHWC alpha = 1 - background")
	{
		d.outputKind = OutputKind::MultiClassSoftmax;
		d.outputLayout = TensorLayout::NHWC;
		d.backgroundClass = 0;
		// 1x1 pixel, 6 classes
		float data[6] = {0.25f, 0.5f, 0.1f, 0.05f, 0.05f, 0.05f};
		std::vector<float> a1(1);
		CHECK(decodeModelOutput(d, data, {1, 1, 1, 6}, 1, 1, a1.data()));
		CHECK(a1[0] == doctest::Approx(0.75f));
	}
	SUBCASE("multiclass logits NHWC")
	{
		d.outputKind = OutputKind::MultiClassLogits;
		d.outputLayout = TensorLayout::NHWC;
		d.backgroundClass = 0;
		float data[3] = {0.f, 0.f, 0.f}; // uniform -> p(bg) = 1/3
		std::vector<float> a1(1);
		CHECK(decodeModelOutput(d, data, {1, 1, 1, 3}, 1, 1, a1.data()));
		CHECK(a1[0] == doctest::Approx(2.f / 3.f));
		float data2[3] = {10.f, 0.f, 0.f};
		CHECK(decodeModelOutput(d, data2, {1, 1, 1, 3}, 1, 1, a1.data()));
		CHECK(a1[0] < 0.01f);
	}
	SUBCASE("shape mismatch is rejected")
	{
		d.outputKind = OutputKind::Alpha;
		float data[4] = {0, 0, 0, 0};
		CHECK_FALSE(decodeModelOutput(d, data, {1, 1, 4, 1}, 2, 2, alpha.data()));
	}
}

TEST_CASE("alignDim rounds to model alignment")
{
	ModelDescriptor d;
	d.alignment = 32;
	CHECK(d.alignDim(500) == 512);
	CHECK(d.alignDim(10) == 32);
	d.alignment = 1;
	CHECK(d.alignDim(501) == 501);
}
