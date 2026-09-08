#include "inference/model_descriptor.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace promatte {

using json = nlohmann::json;

const ResolutionTier &ModelDescriptor::tier(size_t index) const
{
	static const ResolutionTier empty{};
	if (tiers.empty())
		return empty;
	return tiers[std::min(index, tiers.size() - 1)];
}

uint32_t ModelDescriptor::alignDim(uint32_t v) const
{
	if (alignment <= 1)
		return std::max<uint32_t>(v, 1);
	uint32_t r = (v + alignment / 2) / alignment * alignment;
	return std::max<uint32_t>(r, alignment);
}

const char *tensorLayoutName(TensorLayout l)
{
	return l == TensorLayout::NCHW ? "NCHW" : "NHWC";
}

const char *outputKindName(OutputKind k)
{
	switch (k) {
	case OutputKind::Alpha:
		return "alpha";
	case OutputKind::SigmoidLogit:
		return "sigmoid_logit";
	case OutputKind::TwoClassSoftmax:
		return "two_class_softmax";
	case OutputKind::TwoClassLogits:
		return "two_class_logits";
	case OutputKind::MultiClassSoftmax:
		return "multi_class_softmax";
	case OutputKind::MultiClassLogits:
		return "multi_class_logits";
	}
	return "unknown";
}

namespace {

bool parseLayout(const std::string &s, TensorLayout &out)
{
	if (s == "NCHW")
		out = TensorLayout::NCHW;
	else if (s == "NHWC")
		out = TensorLayout::NHWC;
	else
		return false;
	return true;
}

bool parseOutputKind(const std::string &s, OutputKind &out)
{
	if (s == "alpha")
		out = OutputKind::Alpha;
	else if (s == "sigmoid_logit")
		out = OutputKind::SigmoidLogit;
	else if (s == "two_class_softmax")
		out = OutputKind::TwoClassSoftmax;
	else if (s == "two_class_logits")
		out = OutputKind::TwoClassLogits;
	else if (s == "multi_class_softmax")
		out = OutputKind::MultiClassSoftmax;
	else if (s == "multi_class_logits")
		out = OutputKind::MultiClassLogits;
	else
		return false;
	return true;
}

template<typename T> T get(const json &j, const char *key, T def)
{
	auto it = j.find(key);
	if (it == j.end() || it->is_null())
		return def;
	try {
		return it->get<T>();
	} catch (...) {
		return def;
	}
}

} // namespace

bool parseModelManifest(const std::string &text, std::vector<ModelDescriptor> &out, std::string &error)
{
	json root;
	try {
		root = json::parse(text);
	} catch (const std::exception &e) {
		error = std::string("manifest parse error: ") + e.what();
		return false;
	}
	if (!root.is_object() || !root.contains("models") || !root["models"].is_array()) {
		error = "manifest must contain a 'models' array";
		return false;
	}
	std::vector<ModelDescriptor> result;
	for (const auto &m : root["models"]) {
		ModelDescriptor d;
		d.id = get<std::string>(m, "id", "");
		if (d.id.empty()) {
			error = "model entry without id";
			return false;
		}
		d.displayName = get<std::string>(m, "display_name", d.id);
		d.fileName = get<std::string>(m, "file_name", d.id + ".onnx");
		d.family = get<std::string>(m, "family", "");
		d.description = get<std::string>(m, "description", "");
		d.license = get<std::string>(m, "license", "");
		d.licenseUrl = get<std::string>(m, "license_url", "");
		d.sourceUrl = get<std::string>(m, "source_url", "");
		d.downloadUrl = get<std::string>(m, "download_url", "");
		d.sha256 = get<std::string>(m, "sha256", "");
		std::transform(d.sha256.begin(), d.sha256.end(), d.sha256.begin(),
			       [](unsigned char c) { return char(std::tolower(c)); });
		d.sizeBytes = get<uint64_t>(m, "size_bytes", 0);
		d.bundled = get<bool>(m, "bundled", false);
		d.version = get<int>(m, "version", 1);

		std::string layout = get<std::string>(m, "input_layout", "NCHW");
		if (!parseLayout(layout, d.inputLayout)) {
			error = "model " + d.id + ": bad input_layout";
			return false;
		}
		d.channelOrder = get<std::string>(m, "channel_order", "RGB") == "BGR" ? ChannelOrder::BGR
										    : ChannelOrder::RGB;
		if (m.contains("mean") && m["mean"].is_array() && m["mean"].size() == 3)
			for (int i = 0; i < 3; ++i)
				d.mean[i] = m["mean"][i].get<float>();
		if (m.contains("std") && m["std"].is_array() && m["std"].size() == 3)
			for (int i = 0; i < 3; ++i)
				d.std[i] = m["std"][i].get<float>();

		d.fixedInput = get<bool>(m, "fixed_input", true);
		d.alignment = get<uint32_t>(m, "alignment", 1);
		if (m.contains("tiers") && m["tiers"].is_array()) {
			for (const auto &t : m["tiers"]) {
				ResolutionTier tier;
				tier.width = get<uint32_t>(t, "width", 0);
				tier.height = get<uint32_t>(t, "height", 0);
				tier.downsampleRatio = get<float>(t, "downsample_ratio", 1.0f);
				tier.label = get<std::string>(t, "label", "");
				if (tier.width == 0 || tier.height == 0) {
					error = "model " + d.id + ": tier without width/height";
					return false;
				}
				d.tiers.push_back(tier);
			}
		}
		if (d.tiers.empty()) {
			error = "model " + d.id + ": no resolution tiers";
			return false;
		}
		d.recurrent = get<bool>(m, "recurrent", false);
		d.providesForeground = get<bool>(m, "provides_foreground", false);
		std::string ok = get<std::string>(m, "output_kind", "alpha");
		if (!parseOutputKind(ok, d.outputKind)) {
			error = "model " + d.id + ": bad output_kind";
			return false;
		}
		std::string olayout = get<std::string>(m, "output_layout", layout);
		if (!parseLayout(olayout, d.outputLayout)) {
			error = "model " + d.id + ": bad output_layout";
			return false;
		}
		d.foregroundClass = get<int>(m, "foreground_class", 1);
		d.backgroundClass = get<int>(m, "background_class", 0);
		d.inputName = get<std::string>(m, "input_name", "");
		d.outputName = get<std::string>(m, "output_name", "");
		d.qualityRank = get<int>(m, "quality_rank", 50);
		d.costRank = get<int>(m, "cost_rank", 50);
		d.recommendedForCpu = get<bool>(m, "recommended_for_cpu", false);
		d.recommendedForGpu = get<bool>(m, "recommended_for_gpu", false);
		result.push_back(std::move(d));
	}
	out = std::move(result);
	return true;
}

bool decodeModelOutput(const ModelDescriptor &desc, const float *data, const std::vector<int64_t> &shape, uint32_t w,
		       uint32_t h, float *alpha)
{
	if (!data || !alpha || shape.size() < 3)
		return false;

	// Normalise the shape to (C, H, W) semantics, ignoring a leading batch of 1.
	size_t off = shape.size() == 4 ? 1 : 0;
	int64_t d0 = shape[off], d1 = shape[off + 1], d2 = shape[off + 2];
	int64_t C, H, W;
	bool nhwc = desc.outputLayout == TensorLayout::NHWC;
	if (shape.size() == 3 && off == 0) {
		// (C,H,W) or (H,W,C)
		if (nhwc) {
			H = d0;
			W = d1;
			C = d2;
		} else {
			C = d0;
			H = d1;
			W = d2;
		}
	} else {
		if (nhwc) {
			H = d0;
			W = d1;
			C = d2;
		} else {
			C = d0;
			H = d1;
			W = d2;
		}
	}
	if (H != int64_t(h) || W != int64_t(w) || C < 1)
		return false;

	const size_t n = size_t(w) * h;
	auto at = [&](size_t idx, int64_t c) -> float {
		return nhwc ? data[idx * size_t(C) + size_t(c)] : data[size_t(c) * n + idx];
	};

	switch (desc.outputKind) {
	case OutputKind::Alpha:
		for (size_t i = 0; i < n; ++i)
			alpha[i] = std::clamp(at(i, 0), 0.f, 1.f);
		return true;
	case OutputKind::SigmoidLogit:
		for (size_t i = 0; i < n; ++i)
			alpha[i] = 1.f / (1.f + std::exp(-at(i, 0)));
		return true;
	case OutputKind::TwoClassSoftmax: {
		if (C < 2)
			return false;
		int64_t fg = std::clamp<int64_t>(desc.foregroundClass, 0, C - 1);
		for (size_t i = 0; i < n; ++i)
			alpha[i] = std::clamp(at(i, fg), 0.f, 1.f);
		return true;
	}
	case OutputKind::TwoClassLogits: {
		if (C < 2)
			return false;
		int64_t fg = std::clamp<int64_t>(desc.foregroundClass, 0, C - 1);
		int64_t bg = fg == 0 ? 1 : 0;
		for (size_t i = 0; i < n; ++i) {
			float a = at(i, fg), b = at(i, bg);
			float m = std::max(a, b);
			float ea = std::exp(a - m), eb = std::exp(b - m);
			alpha[i] = ea / (ea + eb);
		}
		return true;
	}
	case OutputKind::MultiClassSoftmax: {
		int64_t bg = std::clamp<int64_t>(desc.backgroundClass, 0, C - 1);
		for (size_t i = 0; i < n; ++i)
			alpha[i] = std::clamp(1.f - at(i, bg), 0.f, 1.f);
		return true;
	}
	case OutputKind::MultiClassLogits: {
		int64_t bg = std::clamp<int64_t>(desc.backgroundClass, 0, C - 1);
		for (size_t i = 0; i < n; ++i) {
			float m = at(i, 0);
			for (int64_t c = 1; c < C; ++c)
				m = std::max(m, at(i, c));
			float sum = 0.f;
			for (int64_t c = 0; c < C; ++c)
				sum += std::exp(at(i, c) - m);
			alpha[i] = std::clamp(1.f - std::exp(at(i, bg) - m) / sum, 0.f, 1.f);
		}
		return true;
	}
	}
	return false;
}

} // namespace promatte
