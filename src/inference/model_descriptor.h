#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace promatte {

enum class TensorLayout { NCHW, NHWC };
enum class ChannelOrder { RGB, BGR };

// How the model's raw output must be interpreted to obtain a foreground alpha.
enum class OutputKind {
	Alpha,             // single channel already in [0,1]
	SigmoidLogit,      // single channel logit -> sigmoid
	TwoClassSoftmax,   // C=2 probabilities, foreground = channel foregroundClass
	TwoClassLogits,    // C=2 logits -> softmax
	MultiClassSoftmax, // C>2 probabilities, alpha = 1 - p(background)
	MultiClassLogits,  // C>2 logits -> softmax, alpha = 1 - p(background)
};

struct ResolutionTier {
	uint32_t width = 0;
	uint32_t height = 0;
	float downsampleRatio = 1.0f; // RVM-style internal downsampling; 1.0 = none
	std::string label;            // "Performance", "Balanced", ...
};

struct ModelDescriptor {
	std::string id;
	std::string displayName;
	std::string fileName;
	std::string family; // "rvm", "mediapipe", "pphumanseg", "modnet", "sinet"
	std::string description;
	std::string license;
	std::string licenseUrl;
	std::string sourceUrl;
	std::string downloadUrl; // empty for bundled-only models
	std::string sha256;      // lowercase hex, may be empty for user-supplied models
	uint64_t sizeBytes = 0;
	bool bundled = false;
	int version = 1;

	TensorLayout inputLayout = TensorLayout::NCHW;
	ChannelOrder channelOrder = ChannelOrder::RGB;
	float mean[3] = {0.f, 0.f, 0.f}; // value = (x/255 - mean) / std
	float std[3] = {1.f, 1.f, 1.f};

	bool fixedInput = true;
	uint32_t alignment = 1; // dynamic models: dims must be multiples of this
	std::vector<ResolutionTier> tiers;

	bool recurrent = false;          // carries temporal state (RVM)
	bool providesForeground = false; // outputs a predicted foreground colour (RVM fgr)

	OutputKind outputKind = OutputKind::Alpha;
	TensorLayout outputLayout = TensorLayout::NCHW;
	int foregroundClass = 1;
	int backgroundClass = 0;

	std::string inputName;  // optional; discovered from the graph if empty
	std::string outputName; // optional

	int qualityRank = 50; // 0..100, from docs/model-evaluation.md
	int costRank = 50;    // 0..100, higher = more expensive
	bool recommendedForCpu = false;
	bool recommendedForGpu = false;

	const ResolutionTier &tier(size_t index) const;
	size_t tierCount() const { return tiers.size(); }
	uint32_t alignDim(uint32_t v) const;
};

// Parses data/models/manifest.json (see docs/architecture.md for the schema).
bool parseModelManifest(const std::string &json, std::vector<ModelDescriptor> &out, std::string &error);

// Converts a raw model output tensor into a float alpha plane [0,1] of size w*h.
// `shape` is the runtime output shape; returns false if the shape is incompatible.
bool decodeModelOutput(const ModelDescriptor &desc, const float *data, const std::vector<int64_t> &shape,
		       uint32_t w, uint32_t h, float *alpha);

const char *tensorLayoutName(TensorLayout l);
const char *outputKindName(OutputKind k);

} // namespace promatte
